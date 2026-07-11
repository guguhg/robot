#include "rclcpp/rclcpp.hpp"
#include "geometry_msgs/msg/twist.hpp"
#include "sensor_msgs/msg/imu.hpp"
#include "algorithms/attitude_compensator.hpp"
#include "common/config_loader/config_loader.hpp"

#include <memory>
#include <string>
#include <chrono>
#include <mutex>

using namespace std::chrono_literals;

/**
 * @brief 姿态补偿节点
 * 
 * 订阅 /cmd_vel_limited（来自 twist_node）+ /imu/data（来自 imu_tools_node）
 * 处理：利用 IMU 姿态对速度指令进行重力补偿（坡道/倾角修正）
 * 发布 /cmd_vel_compensated（给 inverse_kinematics）
 */
class AttitudeCompNode : public rclcpp::Node
{
public:
    AttitudeCompNode()
        : Node("attitude_comp_node"),
          last_cmd_time_(this->get_clock()->now()),
          last_imu_time_(this->get_clock()->now())
    {
        RCLCPP_INFO(this->get_logger(), "=== AttitudeCompNode Starting ===");

        loadConfig();

        // 设置姿态补偿器参数
        attitude_compensator_.setCompensationEnabled(config_.enabled);
        attitude_compensator_.setGravityCompensation(config_.gravity_strength);
        attitude_compensator_.setMaxPitchAngle(config_.max_pitch_angle);
        attitude_compensator_.setMaxRollAngle(config_.max_roll_angle);

        // 订阅 /cmd_vel_limited (来自 twist_node)
        cmd_sub_ = this->create_subscription<geometry_msgs::msg::Twist>(
            config_.input_topic,
            1,  // 队列大小 1：只保留最新指令
            std::bind(&AttitudeCompNode::onCmdVel, this, std::placeholders::_1));

        // 订阅 /imu/data (来自 imu_tools_node)
        imu_sub_ = this->create_subscription<sensor_msgs::msg::Imu>(
            config_.imu_topic,
            1,  // 队列大小 1：只保留最新数据
            std::bind(&AttitudeCompNode::onImu, this, std::placeholders::_1));

        // 发布 /cmd_vel_compensated
        cmd_pub_ = this->create_publisher<geometry_msgs::msg::Twist>(
            config_.output_topic,
            10);

        // 定时器：50Hz 发布处理后的指令
        int interval_ms = 1000 / config_.publish_rate;
        publish_timer_ = this->create_wall_timer(
            std::chrono::milliseconds(interval_ms),
            std::bind(&AttitudeCompNode::publishCmd, this));

        RCLCPP_INFO(this->get_logger(), "AttitudeCompNode initialized");
        RCLCPP_INFO(this->get_logger(), "  input:      %s", config_.input_topic.c_str());
        RCLCPP_INFO(this->get_logger(), "  imu:        %s", config_.imu_topic.c_str());
        RCLCPP_INFO(this->get_logger(), "  output:     %s", config_.output_topic.c_str());
        RCLCPP_INFO(this->get_logger(), "  enabled:    %s", config_.enabled ? "true" : "false");
        RCLCPP_INFO(this->get_logger(), "  gravity:    %.2f", config_.gravity_strength);
        RCLCPP_INFO(this->get_logger(), "  deadband:   %.3f", config_.deadband);
    }

private:
    struct Config {
        std::string input_topic = "/cmd_vel_limited";
        std::string output_topic = "/cmd_vel_compensated";
        std::string imu_topic = "/imu/data";
        bool enabled = true;
        float gravity_strength = 0.5f;
        float max_pitch_angle = 0.523f;
        float max_roll_angle = 0.523f;
        int publish_rate = 50;
        float imu_timeout = 1.0f;
        float deadband = 0.05f;
    } config_;

    rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr cmd_sub_;
    rclcpp::Subscription<sensor_msgs::msg::Imu>::SharedPtr imu_sub_;
    rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr cmd_pub_;
    rclcpp::TimerBase::SharedPtr publish_timer_;

    algorithms::AttitudeCompensator attitude_compensator_;

    // 最新收到的速度指令
    geometry_msgs::msg::Twist latest_cmd_;
    rclcpp::Time last_cmd_time_;
    std::mutex cmd_mutex_;

    // 最新收到的 IMU 数据
    sensor_msgs::msg::Imu latest_imu_;
    rclcpp::Time last_imu_time_;
    bool has_imu_ = false;
    std::mutex imu_mutex_;

    bool has_cmd_timeout_ = false;

    /**
     * @brief 加载配置文件
     */
    void loadConfig()
    {
        try {
            YAML::Node config = common::ConfigLoader::loadDefault();
            auto ac_config = config["algorithms"]["attitude_compensator"];

            if (ac_config) {
                // ---------- 话题配置 ----------
                auto topics = ac_config["topics"];
                if (topics) {
                    config_.input_topic = topics["input"].as<std::string>("/cmd_vel_limited");
                    config_.output_topic = topics["output"].as<std::string>("/cmd_vel_compensated");
                    config_.imu_topic = topics["imu"].as<std::string>("/imu/data");
                }

                // ---------- 补偿配置 ----------
                config_.enabled = ac_config["enabled"].as<bool>(true);
                config_.gravity_strength = ac_config["gravity_strength"].as<float>(0.5f);
                config_.max_pitch_angle = ac_config["max_pitch_angle"].as<float>(0.523f);
                config_.max_roll_angle = ac_config["max_roll_angle"].as<float>(0.523f);

                // ---------- 其他配置 ----------
                config_.publish_rate = ac_config["publish_rate"].as<int>(50);
                config_.imu_timeout = ac_config["imu_timeout"].as<float>(1.0f);
                config_.deadband = ac_config["deadband"].as<float>(0.05f);

                // ---------- 参数校验 ----------
                config_.gravity_strength = std::clamp(config_.gravity_strength, 0.0f, 1.0f);
                config_.max_pitch_angle = std::max(0.01f, config_.max_pitch_angle);
                config_.max_roll_angle = std::max(0.01f, config_.max_roll_angle);
                config_.publish_rate = std::max(1, config_.publish_rate);
                config_.imu_timeout = std::max(0.1f, config_.imu_timeout);
                config_.deadband = std::clamp(config_.deadband, 0.001f, 0.1f);

                RCLCPP_INFO(this->get_logger(), "Config loaded successfully");
            } else {
                RCLCPP_WARN(this->get_logger(), "No 'attitude_compensator' config found, using defaults");
            }
        } catch (const std::exception& e) {
            RCLCPP_WARN(this->get_logger(), "Failed to load config: %s", e.what());
            RCLCPP_WARN(this->get_logger(), "Using default values");
        }
    }

    /**
     * @brief /cmd_vel_limited 消息订阅回调
     */
    void onCmdVel(const geometry_msgs::msg::Twist::SharedPtr msg)
    {
        std::lock_guard<std::mutex> lock(cmd_mutex_);
        latest_cmd_ = *msg;
        last_cmd_time_ = this->get_clock()->now();
        has_cmd_timeout_ = false;
    }

    /**
     * @brief /imu/data 消息订阅回调
     */
    void onImu(const sensor_msgs::msg::Imu::SharedPtr msg)
    {
        std::lock_guard<std::mutex> lock(imu_mutex_);
        latest_imu_ = *msg;
        last_imu_time_ = this->get_clock()->now();
        has_imu_ = true;
    }

    /**
     * @brief 定时器回调
     * 
     * 1. 获取速度指令（超时归零）
     * 2. 获取 IMU 四元数（超时降级透传）
     * 3. 姿态补偿
     * 4. 死区处理
     * 5. 发布补偿后的速度指令
     */
    void publishCmd()
    {
        float dt = 1.0f / config_.publish_rate;

        // 1. 获取最新的速度指令
        float linear_x, linear_y, angular_z;
        {
            std::lock_guard<std::mutex> lock(cmd_mutex_);
            auto elapsed = (this->get_clock()->now() - last_cmd_time_).seconds();
            if (elapsed > 1.0f) {
                // 超时归零
                linear_x = 0.0f;
                linear_y = 0.0f;
                angular_z = 0.0f;
                has_cmd_timeout_ = true;
            } else {
                linear_x = latest_cmd_.linear.x;
                linear_y = latest_cmd_.linear.y;
                angular_z = latest_cmd_.angular.z;
                has_cmd_timeout_ = false;
            }
        }

        // 2. 获取 IMU 四元数
        Eigen::Quaternionf quat = Eigen::Quaternionf::Identity();
        bool has_imu = false;
        {
            std::lock_guard<std::mutex> lock(imu_mutex_);
            auto elapsed = (this->get_clock()->now() - last_imu_time_).seconds();
            if (has_imu_ && elapsed < config_.imu_timeout) {
                quat = Eigen::Quaternionf(
                    latest_imu_.orientation.w,
                    latest_imu_.orientation.x,
                    latest_imu_.orientation.y,
                    latest_imu_.orientation.z
                );
                has_imu = true;
            }
        }

        // 3. 如果 IMU 超时，降级为直接透传
        if (!has_imu) {
            auto pub_msg = geometry_msgs::msg::Twist();
            pub_msg.linear.x = linear_x;
            pub_msg.linear.y = linear_y;
            pub_msg.angular.z = angular_z;
            cmd_pub_->publish(pub_msg);

            if (!has_imu_ || (this->get_clock()->now() - last_imu_time_).seconds() > 5.0f) {
                RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                    "No IMU data, skipping compensation");
            }
            return;
        }

        // 4. 姿态补偿
        auto result = attitude_compensator_.process(linear_x, linear_y, angular_z, quat);

        if (!result.valid) {
            return;
        }

        // ============ 死区处理（消除 IMU 噪声导致的微动） ============
        float cmd_x = result.linear_x;
        float cmd_y = result.linear_y;
        float cmd_z = result.angular_z;

        if (std::abs(cmd_x) < config_.deadband) cmd_x = 0.0f;
        if (std::abs(cmd_y) < config_.deadband) cmd_y = 0.0f;
        if (std::abs(cmd_z) < config_.deadband) cmd_z = 0.0f;

        // 5. 构建并发布消息
        auto pub_msg = geometry_msgs::msg::Twist();
        pub_msg.linear.x = cmd_x;
        pub_msg.linear.y = cmd_y;
        pub_msg.angular.z = cmd_z;

        cmd_pub_->publish(pub_msg);

        // 调试日志（仅在补偿生效时打印）
        if (result.has_imu && 
            (std::abs(result.pitch_compensation) > 0.01f || 
             std::abs(result.roll_compensation) > 0.01f)) {
            RCLCPP_DEBUG(this->get_logger(),
                "Compensated: lin=(%.2f, %.2f), ang=%.2f, pitch_comp=%.3f, roll_comp=%.3f",
                cmd_x, cmd_y, cmd_z,
                result.pitch_compensation, result.roll_compensation);
        }
    }
};

int main(int argc, char** argv)
{
    rclcpp::init(argc, argv);
    auto node = std::make_shared<AttitudeCompNode>();
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}