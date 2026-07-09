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
     * @brief 底盘驱动节点
     *
     * 职责：
     * 1. 订阅 /chassis/motor_cmd (MotorCmd) → 下发到驱动层
     * 2. 发布 /chassis/motor_states (MotorStates) → 当前速度反馈
     * 3. 看门狗保护（超时自动停止）
     *
     * 所有话题名从 YAML 配置文件读取，支持灵活配置
     */
    class ChassisDriver : public rclcpp::Node
    {
    public:
        explicit ChassisDriver(const rclcpp::NodeOptions &options = rclcpp::NodeOptions());
        ~ChassisDriver() = default;

    private:
        /**
         * @brief 配置结构体
         *
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
         * @brief 底盘电机ID结构体
         *
         */
        struct Mapping
        {
            int left_front = 0;  // 左前轮电机 ID
            int right_front = 2; // 右前轮电机 ID
            int left_rear = 1;   // 左后轮电机 ID
            int right_rear = 3;  // 右后轮电机 ID
        } mapping_;

        /**
         * @brief 方向系数
         *
         */
        struct Direction
        {
            float left_front = 1.0f;   // 左前：正转向前
            float right_front = -1.0f; // 右前：正转向后（反转向前）
            float left_rear = 1.0f;    // 左后：正转向前
            float right_rear = -1.0f;  // 右后：正转向后（反转向前）
        } direction_;

        /**
         * @brief 运行时的状态
         *
         */
        struct State
        {
            float current_speeds[4] = {0.0f, 0.0f, 0.0f, 0.0f}; // 当前四轮速度 (r/s)
            rclcpp::Time last_cmd_time;                         // 最后一次收到指令的时间
            bool running = false;                               // 是否正在运行
            std::mutex mutex;                                   // 互斥锁，保护共享数据
        } state_;

        // ============ ROS2 通信 ============
        rclcpp::Subscription<interfaces::msg::MotorCmd>::SharedPtr motor_cmd_sub_;    // 控制指令订阅器
        rclcpp::Publisher<interfaces::msg::MotorStates>::SharedPtr motor_states_pub_; // 状态反馈发布器

        rclcpp::TimerBase::SharedPtr publish_timer_;  // 状态发布定时器
        rclcpp::TimerBase::SharedPtr watchdog_timer_; // 看门狗定时器

        // ============ 方法 ============
        void loadConfig();  // 加载配置文件
        void loadMapping(); // 加载电机映射和方向系数
        void initROS();     // 初始化 ROS2 通信

        void onMotorCmd(const interfaces::msg::MotorCmd::SharedPtr msg); // 控制指令回调
        void publishMotorStates();                                       // 发布电机状态
        void watchdogCheck();                                            // 看门狗检查

        void sendMotorCommands(const float speeds[4]); // 发送电机指令到驱动层
        void readMotorSpeeds(float speeds[4]);         // 从驱动层读取当前速度
        void stopAllMotors();                          // 截止所有电机

        float clampSpeed(float speed);         // 速度限幅
        void clampSpeeds(float speeds[4]);     // 四轮速度批量限幅
        void normalizeSpeeds(float speeds[4]); // 方向系数归一化

        uint8_t getMotorId(int mapping) const; // 获取电机 ID
    };

} // namespace dri_interfaces