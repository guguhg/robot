#include "rclcpp/rclcpp.hpp"
#include "geometry_msgs/msg/twist.hpp"
#include "interfaces/msg/motor_cmd.hpp"
#include "algorithms/inverse_kinematics.hpp"
#include "common/config_loader/config_loader.hpp"

#include <memory>
#include <string>
#include <chrono>
#include <mutex>

using namespace std::chrono_literals;

/**
 * @brief 逆运动学节点
 * 
 * 订阅 /cmd_vel_compensated（来自 attitude_compensator）
 * 处理：Twist → 四轮速度 (麦轮/差速/普通四轮 IK)
 * 发布 /chassis/motor_cmd（给 chassis_dri）
 */
class IKNode : public rclcpp::Node
{
public:
    IKNode()
        : Node("ik_node"),
          last_cmd_time_(this->get_clock()->now())
    {
        RCLCPP_INFO(this->get_logger(), "=== IKNode Starting ===");

        loadConfig();

        // 设置逆运动学参数
        ik_.setChassisType(config_.chassis_type);
        ik_.setParams(
            config_.wheel_base,
            config_.wheel_track,
            config_.wheel_radius,
            config_.max_speed,
            config_.min_speed
        );

        // 订阅 /cmd_vel_compensated (来自 attitude_compensator)
        cmd_sub_ = this->create_subscription<geometry_msgs::msg::Twist>(
            config_.input_topic,
            1,  // 队列大小 1：只保留最新指令
            std::bind(&IKNode::onCmdVel, this, std::placeholders::_1));

        // 发布 /chassis/motor_cmd (给 chassis_dri)
        motor_pub_ = this->create_publisher<interfaces::msg::MotorCmd>(
            config_.output_topic,
            10);

        // 定时器：50Hz 发布电机指令
        int interval_ms = 20;  // 50Hz
        publish_timer_ = this->create_wall_timer(
            std::chrono::milliseconds(interval_ms),
            std::bind(&IKNode::publishMotorCmd, this));

        RCLCPP_INFO(this->get_logger(), "IKNode initialized");
        RCLCPP_INFO(this->get_logger(), "  input:       %s", config_.input_topic.c_str());
        RCLCPP_INFO(this->get_logger(), "  output:      %s", config_.output_topic.c_str());
        RCLCPP_INFO(this->get_logger(), "  chassis:     %s", 
                    algorithms::InverseKinematics::chassisTypeToString(config_.chassis_type).c_str());
        RCLCPP_INFO(this->get_logger(), "  wheel_base:  %.3f m", config_.wheel_base);
        RCLCPP_INFO(this->get_logger(), "  wheel_track: %.3f m", config_.wheel_track);
        RCLCPP_INFO(this->get_logger(), "  max_speed:   %.2f r/s", config_.max_speed);
    }

private:
    struct Config {
        std::string input_topic = "/cmd_vel_compensated";
        std::string output_topic = "/chassis/motor_cmd";
        algorithms::ChassisType chassis_type = algorithms::ChassisType::MECANUM;
        float wheel_base = 0.3f;
        float wheel_track = 0.25f;
        float wheel_radius = 0.05f;
        float max_speed = 1.33f;
        float min_speed = -1.33f;
        float cmd_timeout = 0.5f;
    } config_;

    rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr cmd_sub_;
    rclcpp::Publisher<interfaces::msg::MotorCmd>::SharedPtr motor_pub_;
    rclcpp::TimerBase::SharedPtr publish_timer_;

    algorithms::InverseKinematics ik_;

    // 最新接收到的指令
    geometry_msgs::msg::Twist latest_cmd_;
    rclcpp::Time last_cmd_time_;
    std::mutex cmd_mutex_;

    /**
     * @brief 加载配置文件
     */
    void loadConfig()
    {
        try {
            YAML::Node config = common::ConfigLoader::loadDefault();
            auto ik_config = config["algorithms"]["inverse_kinematics"];

            if (ik_config) {
                // ---------- 话题配置 ----------
                auto topics = ik_config["topics"];
                if (topics) {
                    config_.input_topic = topics["input"].as<std::string>("/cmd_vel_compensated");
                    config_.output_topic = topics["output"].as<std::string>("/chassis/motor_cmd");
                }

                // ---------- 底盘类型 ----------
                std::string type_str = ik_config["chassis_type"].as<std::string>("mecanum");
                if (type_str == "mecanum") {
                    config_.chassis_type = algorithms::ChassisType::MECANUM;
                } else if (type_str == "differential") {
                    config_.chassis_type = algorithms::ChassisType::DIFFERENTIAL;
                } else if (type_str == "4wd_standard" || type_str == "4wd") {
                    config_.chassis_type = algorithms::ChassisType::FOUR_WD_STANDARD;
                } else {
                    RCLCPP_WARN(this->get_logger(), "Unknown chassis_type: %s, using mecanum", type_str.c_str());
                    config_.chassis_type = algorithms::ChassisType::MECANUM;
                }

                // ---------- 底盘参数 ----------
                auto params = ik_config["params"];
                if (params) {
                    config_.wheel_base = params["wheel_base"].as<float>(0.3f);
                    config_.wheel_track = params["wheel_track"].as<float>(0.25f);
                    config_.wheel_radius = params["wheel_radius"].as<float>(0.05f);
                    config_.max_speed = params["max_speed"].as<float>(1.33f);
                    config_.min_speed = params["min_speed"].as<float>(-1.33f);
                }

                // ---------- 超时配置 ----------
                config_.cmd_timeout = ik_config["cmd_timeout"].as<float>(0.5f);

                // ---------- 参数校验 ----------
                config_.wheel_base = std::max(0.01f, config_.wheel_base);
                config_.wheel_track = std::max(0.01f, config_.wheel_track);
                config_.wheel_radius = std::max(0.001f, config_.wheel_radius);
                config_.cmd_timeout = std::max(0.1f, config_.cmd_timeout);

                RCLCPP_INFO(this->get_logger(), "Config loaded successfully");
            } else {
                RCLCPP_WARN(this->get_logger(), "No 'inverse_kinematics' config found, using defaults");
            }
        } catch (const std::exception& e) {
            RCLCPP_WARN(this->get_logger(), "Failed to load config: %s", e.what());
            RCLCPP_WARN(this->get_logger(), "Using default values");
        }
    }

    /**
     * @brief /cmd_vel_compensated 消息订阅回调
     */
    void onCmdVel(const geometry_msgs::msg::Twist::SharedPtr msg)
    {
        std::lock_guard<std::mutex> lock(cmd_mutex_);
        latest_cmd_ = *msg;
        last_cmd_time_ = this->now();
    }

    /**
     * @brief 定时器回调：50Hz 发布电机指令
     * 
     * 1. 检查超时（超时自动归零）
     * 2. 逆运动学计算
     * 3. 发布电机指令
     */
    void publishMotorCmd()
    {
        float linear_x, linear_y, angular_z;
        bool is_timeout = false;

        {
            std::lock_guard<std::mutex> lock(cmd_mutex_);
            auto elapsed = (this->now() - last_cmd_time_).seconds();
            if (elapsed > config_.cmd_timeout) {
                // 超时！自动归零（安全保护）
                linear_x = 0.0f;
                linear_y = 0.0f;
                angular_z = 0.0f;
                is_timeout = true;
            } else {
                linear_x = latest_cmd_.linear.x;
                linear_y = latest_cmd_.linear.y;
                angular_z = latest_cmd_.angular.z;
            }
        }

        // 逆运动学计算
        auto result = ik_.process(linear_x, linear_y, angular_z);

        if (!result.valid) {
            return;
        }

        // 构建并发布 MotorCmd
        auto motor_cmd = interfaces::msg::MotorCmd();
        motor_cmd.left_front = result.wheel_speeds[0];
        motor_cmd.right_front = result.wheel_speeds[1];
        motor_cmd.left_rear = result.wheel_speeds[2];
        motor_cmd.right_rear = result.wheel_speeds[3];

        motor_pub_->publish(motor_cmd);

        // 调试日志
        if (std::abs(linear_x) > 0.01f || std::abs(angular_z) > 0.01f) {
            RCLCPP_DEBUG(this->get_logger(),
                "IK: chassis=%s, vx=%.2f, vz=%.2f → LF=%.2f, RF=%.2f, LR=%.2f, RR=%.2f",
                algorithms::InverseKinematics::chassisTypeToString(config_.chassis_type).c_str(),
                linear_x, angular_z,
                motor_cmd.left_front, motor_cmd.right_front,
                motor_cmd.left_rear, motor_cmd.right_rear);
        }
    }
};

int main(int argc, char** argv)
{
    rclcpp::init(argc, argv);
    auto node = std::make_shared<IKNode>();
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}