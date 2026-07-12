#include "rclcpp/rclcpp.hpp"
#include "geometry_msgs/msg/twist.hpp"
#include "algorithms/twist_handler.hpp"
#include "common/config_loader/config_loader.hpp"

#include <memory>
#include <string>
#include <chrono>

using namespace std::chrono_literals;

/**
 * @brief Twist 处理节点
 * 
 * 订阅 /cmd_vel（来自大脑）
 * 处理：速度限幅 + 平滑滤波（梯形速度规划）+ 超时保护
 * 发布 /cmd_vel_limited
 */
class TwistNode : public rclcpp::Node
{
public:
    TwistNode()
        : Node("twist_node"),
          last_cmd_time_(this->get_clock()->now())
    {
        RCLCPP_INFO(this->get_logger(), "=== TwistNode Starting ===");

        loadConfig();

        // 设置 TwistHandler 参数
        twist_handler_.setLimits(
            config_.max_linear_x,
            config_.max_angular_z,
            config_.max_linear_y
        );
        twist_handler_.setAccelLimits(
            config_.max_accel,
            config_.max_decel
        );
        twist_handler_.setAngularAccelLimit(
            config_.max_angular_accel
        );

        // 订阅 /cmd_vel（来自大脑）
        cmd_sub_ = this->create_subscription<geometry_msgs::msg::Twist>(
            config_.input_topic,
            1,  // 队列大小 1：只保留最新指令
            std::bind(&TwistNode::onCmdVel, this, std::placeholders::_1));

        // 发布 /cmd_vel_limited（给 attitude_compensator）
        cmd_pub_ = this->create_publisher<geometry_msgs::msg::Twist>(
            config_.output_topic,
            10);

        // 定时器：50Hz 发布处理后的指令
        int interval_ms = 1000 / config_.publish_rate;
        publish_timer_ = this->create_wall_timer(
            std::chrono::milliseconds(interval_ms),
            std::bind(&TwistNode::publishCmd, this));

        RCLCPP_INFO(this->get_logger(), "TwistNode initialized");
        RCLCPP_INFO(this->get_logger(), "  input:  %s", config_.input_topic.c_str());
        RCLCPP_INFO(this->get_logger(), "  output: %s", config_.output_topic.c_str());
        RCLCPP_INFO(this->get_logger(), "  max_linear_x: %.2f m/s", config_.max_linear_x);
        RCLCPP_INFO(this->get_logger(), "  max_angular_z: %.2f rad/s", config_.max_angular_z);
        RCLCPP_INFO(this->get_logger(), "  max_accel: %.2f m/s²", config_.max_accel);
        RCLCPP_INFO(this->get_logger(), "  cmd_timeout: %.2f s", config_.cmd_timeout);
    }

private:
    struct Config {
        std::string input_topic = "/cmd_vel";
        std::string output_topic = "/cmd_vel_limited";
        float max_linear_x = 0.5f;
        float max_linear_y = 0.4f;
        float max_angular_z = 0.8f;
        float max_accel = 1.0f;
        float max_decel = 1.0f;
        float max_angular_accel = 2.0f;
        float cmd_timeout = 0.5f;      // 无新指令超时时间 (秒)，超时后自动归零
        int publish_rate = 50;
    } config_;

    rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr cmd_sub_;
    rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr cmd_pub_;
    rclcpp::TimerBase::SharedPtr publish_timer_;

    algorithms::TwistHandler twist_handler_;
    
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
            auto twist_config = config["algorithms"]["twist_handler"];

            if (twist_config) {
                // ---------- 话题配置 ----------
                auto topics = twist_config["topics"];
                if (topics) {
                    config_.input_topic = topics["input"].as<std::string>("/cmd_vel");
                    config_.output_topic = topics["output"].as<std::string>("/cmd_vel_limited");
                }

                // ---------- 速度限制 ----------
                auto limits = twist_config["limits"];
                if (limits) {
                    config_.max_linear_x = limits["max_linear_x"].as<float>(0.5f);
                    config_.max_linear_y = limits["max_linear_y"].as<float>(0.4f);
                    config_.max_angular_z = limits["max_angular_z"].as<float>(0.8f);
                }

                // ---------- 加速度限制 ----------
                auto accel = twist_config["acceleration"];
                if (accel) {
                    config_.max_accel = accel["max_accel"].as<float>(1.0f);
                    config_.max_decel = accel["max_decel"].as<float>(1.0f);
                    config_.max_angular_accel = accel["max_angular_accel"].as<float>(2.0f);
                }

                // ---------- 超时配置 ----------
                config_.cmd_timeout = twist_config["cmd_timeout"].as<float>(0.5f);

                // ---------- 发布频率 ----------
                config_.publish_rate = twist_config["publish_rate"].as<int>(50);

                // ---------- 参数校验 ----------
                config_.max_linear_x = std::max(0.0f, config_.max_linear_x);
                config_.max_linear_y = std::max(0.0f, config_.max_linear_y);
                config_.max_angular_z = std::max(0.0f, config_.max_angular_z);
                config_.max_accel = std::max(0.0f, config_.max_accel);
                config_.max_decel = std::max(0.0f, config_.max_decel);
                config_.max_angular_accel = std::max(0.0f, config_.max_angular_accel);
                config_.cmd_timeout = std::max(0.1f, config_.cmd_timeout);
                config_.publish_rate = std::max(1, config_.publish_rate);

                RCLCPP_INFO(this->get_logger(), "Config loaded successfully");
            } else {
                RCLCPP_WARN(this->get_logger(), "No 'twist_handler' config found, using defaults");
            }
        } catch (const std::exception& e) {
            RCLCPP_WARN(this->get_logger(), "Failed to load config: %s", e.what());
            RCLCPP_WARN(this->get_logger(), "Using default values");
        }
    }

    /**
     * @brief /cmd_vel 消息订阅回调
     * 
     * 收到大脑发出的速度指令，存储最新值并更新时间戳。
     */
    void onCmdVel(const geometry_msgs::msg::Twist::SharedPtr msg)
    {
        std::lock_guard<std::mutex> lock(cmd_mutex_);
        latest_cmd_ = *msg;                      // 更新最新指令
        last_cmd_time_ = this->get_clock()->now();  // 更新时间戳
    }

    /**
     * @brief 定时器回调
     * 
     * 以固定频率处理速度指令：
     * 1. 检查是否超时，超时则自动归零（安全保护）
     * 2. 死区过滤（消除小信号抖动）
     * 3. 速度限幅（保护电机）
     * 4. 加速度平滑（防止突变）
     * 5. 发布处理后的指令
     * 
     * 超时机制说明：
     * - 正常运行时：指令连续，小车平滑运动
     * - 大脑短暂卡顿（丢一两帧）：保持上次指令，小车继续运行，不会抽搐
     * - 大脑长时间断连（> cmd_timeout）：自动归零，安全停车
     */
    void publishCmd()
    {
        float dt = 1.0f / config_.publish_rate;

        float linear_x, linear_y, angular_z;
        bool is_timeout = false;

        {
            std::lock_guard<std::mutex> lock(cmd_mutex_);
            auto elapsed = (this->get_clock()->now() - last_cmd_time_).seconds();

            if (elapsed > config_.cmd_timeout) {
                // 超时！自动归零（安全保护）
                linear_x = 0.0f;
                linear_y = 0.0f;
                angular_z = 0.0f;
                is_timeout = true;

                // 超时警告（每分钟最多打印一次）
                static rclcpp::Time last_warn_time = this->get_clock()->now();
                if ((this->get_clock()->now() - last_warn_time).seconds() > 1.0f) {
                    RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 1000 * 60,
                        "cmd_vel timeout (%.2fs > %.2fs), auto-zeroing",
                        elapsed, config_.cmd_timeout);
                    last_warn_time = this->get_clock()->now();
                }
            } else {
                linear_x = latest_cmd_.linear.x;
                linear_y = latest_cmd_.linear.y;
                angular_z = latest_cmd_.angular.z;
            }
        }

        // 处理速度指令（死区 → 限幅 → 平滑）
        auto result = twist_handler_.process(linear_x, linear_y, angular_z, dt);

        if (!result.valid) {
            return;
        }

        // 构建并发布消息
        auto pub_msg = geometry_msgs::msg::Twist();
        pub_msg.linear.x = result.linear_x;
        pub_msg.linear.y = result.linear_y;
        pub_msg.angular.z = result.angular_z;

        cmd_pub_->publish(pub_msg);

        // 调试日志（仅在限幅或平滑时打印）
        if (result.limited || result.smoothed) {
            RCLCPP_DEBUG(this->get_logger(),
                "Twist: lin=(%.2f, %.2f), ang=%.2f, limited=%d, smoothed=%d, timeout=%d",
                result.linear_x, result.linear_y, result.angular_z,
                result.limited, result.smoothed, is_timeout);
        }
    }
};

int main(int argc, char** argv)
{
    rclcpp::init(argc, argv);
    auto node = std::make_shared<TwistNode>();
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}