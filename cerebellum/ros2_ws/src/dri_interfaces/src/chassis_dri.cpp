#include "dri_interfaces/chassis_dri.hpp"
#include <algorithm>
#include <cmath>
#include <string>

// ============ 速度数组索引宏 ============
#define IDX_LEFT_FRONT 0  // 左前轮在数组中的索引
#define IDX_RIGHT_FRONT 1 // 右前轮在数组中的索引
#define IDX_LEFT_REAR 2   // 左后轮在数组中的索引
#define IDX_RIGHT_REAR 3  // 右后轮在数组中的索引

namespace dri_interfaces
{

    /**
     * @brief 构造函数
     *
     * @param options ros2选项
     */
    ChassisDriver::ChassisDriver(const rclcpp::NodeOptions &options)
        : Node("chassis_driver", options)
    {
        RCLCPP_INFO(this->get_logger(), "=== ChassisDriver Starting ===");

        loadConfig();  // 加载配置文件
        loadMapping(); // 加载电机映射
        initROS();     // 初始化 ROS2 通信

        RCLCPP_INFO(this->get_logger(), "ChassisDriver initialized");
        RCLCPP_INFO(this->get_logger(), "LF:%d, RF:%d, LR:%d, RR:%d",
                    mapping_.left_front, mapping_.right_front,
                    mapping_.left_rear, mapping_.right_rear);
        RCLCPP_INFO(this->get_logger(), "max_speed: %.2f, min_speed: %.2f, watchdog: %.1fs",
                    config_.max_speed, config_.min_speed, config_.watchdog_timeout);
        RCLCPP_INFO(this->get_logger(), "Topics: cmd='%s', states='%s'",
                    config_.motor_cmd_topic.c_str(), config_.motor_states_topic.c_str());
    }

    /**
     * @brief 加载配置
     *
     */
    void ChassisDriver::loadConfig()
    {
        try
        {
            YAML::Node config = common::ConfigLoader::loadDefault();

            // ---------- 电机配置 ----------
            auto motors = config["drivers"]["motors"];

            config_.max_speed = motors["max_speed"].as<float>(1.33f);
            config_.min_speed = motors["min_speed"].as<float>(-1.33f);
            config_.watchdog_timeout = motors["watchdog_timeout"].as<float>(1.0f);
            config_.publish_rate = motors["publish_rate"].as<int>(50);

            // 限幅，防止无效值
            if (config_.publish_rate < 1)
                config_.publish_rate = 1;
            if (config_.publish_rate > 200)
                config_.publish_rate = 200;

            // ---------- 话题配置 ----------
            auto topics = motors["topics"];
            if (topics)
            {
                config_.motor_cmd_topic = topics["motor_cmd"].as<std::string>("/chassis/motor_cmd");
                config_.motor_states_topic = topics["motor_states"].as<std::string>("/chassis/motor_states");
            }

            RCLCPP_INFO(this->get_logger(), "Config loaded: max=%.2f, min=%.2f, watchdog=%.1fs, publish_rate=%dhz",
                        config_.max_speed, config_.min_speed, config_.watchdog_timeout, config_.publish_rate);
            RCLCPP_INFO(this->get_logger(), "Topics: cmd='%s', states='%s'",
                        config_.motor_cmd_topic.c_str(), config_.motor_states_topic.c_str());
        }
        catch (const std::exception &e)
        {
            RCLCPP_WARN(this->get_logger(), "Failed to load config: %s", e.what());
            RCLCPP_WARN(this->get_logger(), "Using default values");
        }
    }

    /**
     * @brief 加载底盘电机ID映射
     *
     */
    void ChassisDriver::loadMapping()
    {
        try
        {
            YAML::Node config = common::ConfigLoader::loadDefault();
            auto motors = config["drivers"]["motors"];

            // ---------- 读取电机 ID 映射 ----------
            auto map_node = motors["mapping"];
            if (map_node)
            {
                mapping_.left_front = map_node["left_front"].as<int>(0);
                mapping_.right_front = map_node["right_front"].as<int>(2);
                mapping_.left_rear = map_node["left_rear"].as<int>(1);
                mapping_.right_rear = map_node["right_rear"].as<int>(3);
            }

            // ---------- 读取方向系数 ----------
            auto dir_node = motors["direction"];
            if (dir_node)
            {
                direction_.left_front = dir_node["left_front"].as<float>(1.0f);
                direction_.right_front = dir_node["right_front"].as<float>(-1.0f);
                direction_.left_rear = dir_node["left_rear"].as<float>(1.0f);
                direction_.right_rear = dir_node["right_rear"].as<float>(-1.0f);
            }

            RCLCPP_INFO(this->get_logger(), "Mapping: LF=%d, RF=%d, LR=%d, RR=%d",
                        mapping_.left_front, mapping_.right_front,
                        mapping_.left_rear, mapping_.right_rear);
            RCLCPP_INFO(this->get_logger(), "Direction: LF=%.0f, RF=%.0f, LR=%.0f, RR=%.0f",
                        direction_.left_front, direction_.right_front,
                        direction_.left_rear, direction_.right_rear);
        }
        catch (const std::exception &e)
        {
            RCLCPP_WARN(this->get_logger(), "Failed to load mapping: %s", e.what());
            RCLCPP_WARN(this->get_logger(), "Using default mapping");
        }
    }

    /**
     * @brief 获取电机ID, int转uint8_t
     *
     * @param mapping ID
     * @return uint8_t ID
     */
    uint8_t ChassisDriver::getMotorId(int mapping) const
    {
        return static_cast<uint8_t>(mapping);
    }

    /**
     * @brief ros2相关功能初始化
     *
     */
    void ChassisDriver::initROS()
    {
        // ---------- 订阅控制指令 ----------
        motor_cmd_sub_ = this->create_subscription<interfaces::msg::MotorCmd>(
            config_.motor_cmd_topic, // 话题名（从配置读取）
            1,                       // 队列大小：只保留最新指令
            std::bind(&ChassisDriver::onMotorCmd, this, std::placeholders::_1));

        // ---------- 发布状态反馈 ----------
        motor_states_pub_ = this->create_publisher<interfaces::msg::MotorStates>(
            config_.motor_states_topic, // 话题名（从配置读取）
            1);                         // 队列大小：只保留最新状态

        // ---------- 状态发布定时器 ----------
        int interval_ms = 1000 / config_.publish_rate;
        publish_timer_ = this->create_wall_timer(
            std::chrono::milliseconds(interval_ms),
            std::bind(&ChassisDriver::publishMotorStates, this));

        // ---------- 看门狗定时器 ----------
        watchdog_timer_ = this->create_wall_timer(
            std::chrono::milliseconds(100), // 每 100ms 检查一次
            std::bind(&ChassisDriver::watchdogCheck, this));

        RCLCPP_INFO(this->get_logger(), "ROS2 initialized (publish: %dHz)", config_.publish_rate);
    }

    /**
     * @brief 速度限幅
     *
     * @param speed 速度
     * @return float 限幅后的速度
     */
    float ChassisDriver::clampSpeed(float speed)
    {
        return std::clamp(speed, config_.min_speed, config_.max_speed);
    }

    /**
     * @brief 批量限幅速度
     *
     * @param speeds 速度组
     */
    void ChassisDriver::clampSpeeds(float speeds[4])
    {
        for (int i = 0; i < 4; ++i)
        {
            speeds[i] = clampSpeed(speeds[i]);
        }
    }

    /**
     * @brief 方向系数归一化，让每个轮子发送正转是向车头，反转是向车尾
     *
     * @param speeds 速度组
     */
    void ChassisDriver::normalizeSpeeds(float speeds[4])
    {
        speeds[IDX_LEFT_FRONT] *= direction_.left_front;
        speeds[IDX_RIGHT_FRONT] *= direction_.right_front;
        speeds[IDX_LEFT_REAR] *= direction_.left_rear;
        speeds[IDX_RIGHT_REAR] *= direction_.right_rear;
    }

    /**
     * @brief 发送电机控制命令
     *
     * @param speeds 速度组
     */
    // ============ 发送电机指令 ============
    // 注意：此函数由 onMotorCmd 调用，调用时已持有 state_.mutex 锁
    // 因此这里不再加锁，避免死锁
    void ChassisDriver::sendMotorCommands(const float speeds[4])
    {
        std::map<uint8_t, float> cmd;

        cmd[getMotorId(mapping_.left_front)] = speeds[IDX_LEFT_FRONT];
        cmd[getMotorId(mapping_.right_front)] = speeds[IDX_RIGHT_FRONT];
        cmd[getMotorId(mapping_.left_rear)] = speeds[IDX_LEFT_REAR];
        cmd[getMotorId(mapping_.right_rear)] = speeds[IDX_RIGHT_REAR];

        // 通过 Motors 单例下发指令到驱动层
        drivers::Motors::ctrlMotorSpeed_rs(cmd);
    }

    /**
     * @brief 读取速度组
     *
     * @param speeds 速度组
     */
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

    /**
     * @brief 截断刹车，非控制板缓速PID制动
     *
     */
    void ChassisDriver::stopAllMotors()
    {
        drivers::Motors::ctrlMotorStop({getMotorId(mapping_.left_front),
                                        getMotorId(mapping_.right_front),
                                        getMotorId(mapping_.left_rear),
                                        getMotorId(mapping_.right_rear)});
    }

    /**
     * @brief 订阅-控制指令的回调函数
     *
     * @param msg
     */
    void ChassisDriver::onMotorCmd(const interfaces::msg::MotorCmd::SharedPtr msg)
    {
        // 加锁保护共享数据
        std::lock_guard<std::mutex> lock(state_.mutex);

        // 提取四轮速度指令
        float speeds[4] = {
            msg->left_front,
            msg->right_front,
            msg->left_rear,
            msg->right_rear};

        // 1. 速度限幅（防止超出电机最大转速）
        clampSpeeds(speeds);

        // 2. 方向系数（左轮正转向前，右轮正转向后）
        normalizeSpeeds(speeds);

        // 3. 记录状态
        state_.last_cmd_time = this->now();
        state_.running = true;

        // 4. 下发到驱动层
        sendMotorCommands(speeds);

        RCLCPP_DEBUG(this->get_logger(),
                     "motor_cmd: LF=%.2f, RF=%.2f, LR=%.2f, RR=%.2f",
                     speeds[IDX_LEFT_FRONT], speeds[IDX_RIGHT_FRONT],
                     speeds[IDX_LEFT_REAR], speeds[IDX_RIGHT_REAR]);
    }

    /**
     * @brief 发布-定时器回调函数-底盘状态
     *
     */
    void ChassisDriver::publishMotorStates()
    {
        float speeds[4];
        readMotorSpeeds(speeds);

        // 更新当前速度缓存
        {
            std::lock_guard<std::mutex> lock(state_.mutex);
            for (int i = 0; i < 4; ++i)
            {
                state_.current_speeds[i] = speeds[i];
            }
        }

        // 组装并发布消息
        auto msg = interfaces::msg::MotorStates();
        msg.left_front = speeds[IDX_LEFT_FRONT];
        msg.right_front = speeds[IDX_RIGHT_FRONT];
        msg.left_rear = speeds[IDX_LEFT_REAR];
        msg.right_rear = speeds[IDX_RIGHT_REAR];

        motor_states_pub_->publish(msg);
    }

    /**
     * @brief 定时器回调函数-看门狗检查，超时未接收到控制指令消息进行PID缓速停止
     *
     */
    void ChassisDriver::watchdogCheck()
    {
        // 如果看门狗被禁用 (watchdog_timeout <= 0)，直接返回
        if (config_.watchdog_timeout <= 0.0f)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(state_.mutex);

        // 如果从未收到过指令，不触发看门狗
        if (!state_.running)
        {
            return;
        }

        // 计算距离最后一次收到指令的时间差
        auto now = this->now();
        auto dt = (now - state_.last_cmd_time).seconds();

        // 如果超时，自动停止电机
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

/**
 * @brief 进程主函数
 *
 * @param argc 无
 * @param argv 无
 * @return int 无
 */
// int main(int argc, char **argv)
// {
//     rclcpp::init(argc, argv);
//     auto node = std::make_shared<dri_interfaces::ChassisDriver>();
//     rclcpp::spin(node); // 消息循环，处理所有回调
//     rclcpp::shutdown();
//     return 0;
// }