#include "dri_interfaces/chassis_dri.hpp"
#include <algorithm>
#include <cmath>
#include <string>

// ============ 速度数组索引 ============
#define IDX_LEFT_FRONT 0
#define IDX_RIGHT_FRONT 1
#define IDX_LEFT_REAR 2
#define IDX_RIGHT_REAR 3

namespace dri_interfaces
{

    ChassisDriver::ChassisDriver(const rclcpp::NodeOptions &options)
        : Node("chassis_driver", options)
    {
        RCLCPP_INFO(this->get_logger(), "=== ChassisDriver Starting (Hardware Proxy Mode) ===");

        loadConfig();
        loadMapping();
        initROS();

        RCLCPP_INFO(this->get_logger(), "ChassisDriver initialized");
        RCLCPP_INFO(this->get_logger(), "  LF:%d, RF:%d, LR:%d, RR:%d",
                    mapping_.left_front, mapping_.right_front,
                    mapping_.left_rear, mapping_.right_rear);
        RCLCPP_INFO(this->get_logger(), "  max_speed: %.2f, min_speed: %.2f, watchdog: %.1fs",
                    config_.max_speed, config_.min_speed, config_.watchdog_timeout);
        RCLCPP_INFO(this->get_logger(), "  Sub: %s → Pub: %s",
                    config_.motor_cmd_topic.c_str(),
                    config_.motor_states_topic.c_str());
    }

    // ============ 加载配置 ============
    void ChassisDriver::loadConfig()
    {
        try
        {
            YAML::Node config = common::ConfigLoader::loadDefault();
            auto motors = config["drivers"]["motors"];

            config_.max_speed = motors["max_speed"].as<float>(1.33f);
            config_.min_speed = motors["min_speed"].as<float>(-1.33f);
            config_.watchdog_timeout = motors["watchdog_timeout"].as<float>(1.0f);
            config_.publish_rate = motors["publish_rate"].as<int>(50);

            if (config_.publish_rate < 1) config_.publish_rate = 1;
            if (config_.publish_rate > 200) config_.publish_rate = 200;

            auto topics = motors["topics"];
            if (topics)
            {
                config_.motor_cmd_topic = topics["motor_cmd"].as<std::string>("/chassis/motor_cmd");
                config_.motor_states_topic = topics["motor_states"].as<std::string>("/chassis/motor_states");
            }

            RCLCPP_INFO(this->get_logger(), "Config loaded: max=%.2f, min=%.2f, watchdog=%.1fs, rate=%dHz",
                        config_.max_speed, config_.min_speed, config_.watchdog_timeout, config_.publish_rate);
        }
        catch (const std::exception &e)
        {
            RCLCPP_WARN(this->get_logger(), "Failed to load config: %s", e.what());
            RCLCPP_WARN(this->get_logger(), "Using default values");
        }
    }

    // ============ 加载电机映射 ============
    void ChassisDriver::loadMapping()
    {
        try
        {
            YAML::Node config = common::ConfigLoader::loadDefault();
            auto motors = config["drivers"]["motors"];

            auto map_node = motors["mapping"];
            if (map_node)
            {
                mapping_.left_front = map_node["left_front"].as<int>(0);
                mapping_.right_front = map_node["right_front"].as<int>(2);
                mapping_.left_rear = map_node["left_rear"].as<int>(1);
                mapping_.right_rear = map_node["right_rear"].as<int>(3);
            }

            RCLCPP_INFO(this->get_logger(), "Mapping loaded: LF=%d, RF=%d, LR=%d, RR=%d",
                        mapping_.left_front, mapping_.right_front,
                        mapping_.left_rear, mapping_.right_rear);
        }
        catch (const std::exception &e)
        {
            RCLCPP_WARN(this->get_logger(), "Failed to load mapping: %s", e.what());
            RCLCPP_WARN(this->get_logger(), "Using default mapping");
        }
    }

    // ============ 辅助方法 ============
    uint8_t ChassisDriver::getMotorId(int mapping) const
    {
        return static_cast<uint8_t>(mapping);
    }

    float ChassisDriver::clampSpeed(float speed)
    {
        return std::clamp(speed, config_.min_speed, config_.max_speed);
    }

    void ChassisDriver::clampSpeeds(float speeds[4])
    {
        for (int i = 0; i < 4; ++i)
        {
            speeds[i] = clampSpeed(speeds[i]);
        }
    }

    // ============ ROS2 初始化 ============
    void ChassisDriver::initROS()
    {
        // 订阅控制指令 (来自大脑 ros2_control)
        motor_cmd_sub_ = this->create_subscription<interfaces::msg::MotorCmd>(
            config_.motor_cmd_topic,
            1,
            std::bind(&ChassisDriver::onMotorCmd, this, std::placeholders::_1));

        // 发布状态反馈 (给大脑 ros2_control)
        motor_states_pub_ = this->create_publisher<interfaces::msg::MotorStates>(
            config_.motor_states_topic,
            1);

        // 状态发布定时器
        int interval_ms = 1000 / config_.publish_rate;
        publish_timer_ = this->create_wall_timer(
            std::chrono::milliseconds(interval_ms),
            std::bind(&ChassisDriver::publishMotorStates, this));

        // 看门狗定时器
        watchdog_timer_ = this->create_wall_timer(
            std::chrono::milliseconds(100),
            std::bind(&ChassisDriver::watchdogCheck, this));

        RCLCPP_INFO(this->get_logger(), "ROS2 initialized (publish: %dHz)", config_.publish_rate);
    }

    // ============ 发送电机指令 ============
    void ChassisDriver::sendMotorCommands(const float speeds[4])
    {
        std::map<uint8_t, float> cmd;
        cmd[getMotorId(mapping_.left_front)] = speeds[IDX_LEFT_FRONT];
        cmd[getMotorId(mapping_.right_front)] = speeds[IDX_RIGHT_FRONT];
        cmd[getMotorId(mapping_.left_rear)] = speeds[IDX_LEFT_REAR];
        cmd[getMotorId(mapping_.right_rear)] = speeds[IDX_RIGHT_REAR];

        drivers::Motors::ctrlMotorSpeed_rs(cmd);
    }

    // ============ 读取电机速度 ============
    void ChassisDriver::readMotorSpeeds(float speeds[4])
    {
        auto map = drivers::Motors::getMotorSpeed_rs();

        auto it = map.find(getMotorId(mapping_.left_front));
        speeds[IDX_LEFT_FRONT] = (it != map.end()) ? it->second : 0.0f;

        it = map.find(getMotorId(mapping_.right_front));
        speeds[IDX_RIGHT_FRONT] = (it != map.end()) ? it->second : 0.0f;

        it = map.find(getMotorId(mapping_.left_rear));
        speeds[IDX_LEFT_REAR] = (it != map.end()) ? it->second : 0.0f;

        it = map.find(getMotorId(mapping_.right_rear));
        speeds[IDX_RIGHT_REAR] = (it != map.end()) ? it->second : 0.0f;
    }

    // ============ 停止所有电机 ============
    void ChassisDriver::stopAllMotors()
    {
        drivers::Motors::ctrlMotorStop({
            getMotorId(mapping_.left_front),
            getMotorId(mapping_.right_front),
            getMotorId(mapping_.left_rear),
            getMotorId(mapping_.right_rear)
        });
    }

    // ============ 控制指令回调 ============
    void ChassisDriver::onMotorCmd(const interfaces::msg::MotorCmd::SharedPtr msg)
    {
        std::lock_guard<std::mutex> lock(state_.mutex);

        float speeds[4] = {
            msg->left_front,
            msg->right_front,
            msg->left_rear,
            msg->right_rear
        };

        // 速度限幅
        clampSpeeds(speeds);

        // 记录状态
        state_.last_cmd_time = this->now();
        state_.running = true;

        // 下发到驱动层 (透传，不做方向校准)
        sendMotorCommands(speeds);

        RCLCPP_DEBUG(this->get_logger(),
                     "motor_cmd: LF=%.2f, RF=%.2f, LR=%.2f, RR=%.2f",
                     speeds[IDX_LEFT_FRONT], speeds[IDX_RIGHT_FRONT],
                     speeds[IDX_LEFT_REAR], speeds[IDX_RIGHT_REAR]);
    }

    // ============ 发布电机状态 ============
    void ChassisDriver::publishMotorStates()
    {
        float speeds[4];
        readMotorSpeeds(speeds);

        {
            std::lock_guard<std::mutex> lock(state_.mutex);
            for (int i = 0; i < 4; ++i)
            {
                state_.current_speeds[i] = speeds[i];
            }
        }

        auto msg = interfaces::msg::MotorStates();
        msg.left_front = speeds[IDX_LEFT_FRONT];
        msg.right_front = speeds[IDX_RIGHT_FRONT];
        msg.left_rear = speeds[IDX_LEFT_REAR];
        msg.right_rear = speeds[IDX_RIGHT_REAR];

        motor_states_pub_->publish(msg);
    }

    // ============ 看门狗检查 ============
    void ChassisDriver::watchdogCheck()
    {
        if (config_.watchdog_timeout <= 0.0f)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(state_.mutex);

        if (!state_.running)
        {
            return;
        }

        auto now = this->now();
        auto dt = (now - state_.last_cmd_time).seconds();

        if (dt > config_.watchdog_timeout)
        {
            RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                                 "Watchdog timeout: %.1fs (limit: %.1fs), stopping",
                                 dt, config_.watchdog_timeout);

            float stop[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            sendMotorCommands(stop);
            state_.running = false;
        }
    }

} // namespace dri_interfaces