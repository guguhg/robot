#pragma once

#include "rclcpp/rclcpp.hpp"
#include "interfaces/msg/motor_cmd.hpp"
#include "interfaces/msg/motor_states.hpp"

#include "drivers/motor/motor.h"
#include "common/config_loader/config_loader.hpp"

#include <map>
#include <mutex>
#include <memory>
#include <string>

namespace dri_interfaces
{

    /**
     * @brief 底盘驱动节点 (小脑硬件代理)
     * 
     * 职责：
     * 1. 订阅 /chassis/motor_cmd (MotorCmd) → 下发到驱动层
     * 2. 发布 /chassis/motor_states (MotorStates) → 当前速度反馈
     * 3. 看门狗保护（超时自动停止）
     * 
     * 方向校准由大脑 ros2_control 负责，小脑只做透传
     */
    class ChassisDriver : public rclcpp::Node
    {
    public:
        explicit ChassisDriver(const rclcpp::NodeOptions &options = rclcpp::NodeOptions());
        ~ChassisDriver() = default;

    private:
        /**
         * @brief 配置结构体
         */
        struct Config
        {
            float max_speed = 1.33f;                                  // 最大正转速度 (r/s)
            float min_speed = -1.33f;                                 // 最大反转速度 (r/s)
            float watchdog_timeout = 1.0f;                            // 看门狗超时时间 (秒)，0 表示禁用
            int publish_rate = 50;                                    // 状态发布频率 (Hz)
            std::string motor_cmd_topic = "/chassis/motor_cmd";       // 控制指令订阅话题
            std::string motor_states_topic = "/chassis/motor_states"; // 状态反馈发布话题
        } config_;

        /**
         * @brief 底盘电机ID映射 (硬件相关)
         */
        struct Mapping
        {
            int left_front = 0;  // 左前轮电机 ID
            int right_front = 2; // 右前轮电机 ID
            int left_rear = 1;   // 左后轮电机 ID
            int right_rear = 3;  // 右后轮电机 ID
        } mapping_;

        /**
         * @brief 运行时的状态
         */
        struct State
        {
            float current_speeds[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            rclcpp::Time last_cmd_time;
            bool running = false;
            std::mutex mutex;
        } state_;

        // ============ ROS2 通信 ============
        rclcpp::Subscription<interfaces::msg::MotorCmd>::SharedPtr motor_cmd_sub_;
        rclcpp::Publisher<interfaces::msg::MotorStates>::SharedPtr motor_states_pub_;

        rclcpp::TimerBase::SharedPtr publish_timer_;
        rclcpp::TimerBase::SharedPtr watchdog_timer_;

        // ============ 方法 ============
        void loadConfig();
        void loadMapping();
        void initROS();

        void onMotorCmd(const interfaces::msg::MotorCmd::SharedPtr msg);
        void publishMotorStates();
        void watchdogCheck();

        void sendMotorCommands(const float speeds[4]);
        void readMotorSpeeds(float speeds[4]);
        void stopAllMotors();

        float clampSpeed(float speed);
        void clampSpeeds(float speeds[4]);

        uint8_t getMotorId(int mapping) const;
    };

} // namespace dri_interfaces