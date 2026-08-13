#pragma once
#include "drivers/controller_board/controller_comm.h"
#include <memory>
#include <map>
#include <mutex>
#include <vector>
#include <chrono>

namespace drivers
{

    class ControllerBoard
    {
    public:
        /**
         * @brief 禁止拷贝，确保硬件唯一性
         *
         */
        ControllerBoard(const ControllerBoard &) = delete;
        ControllerBoard &operator=(const ControllerBoard &) = delete;

        /**
         * @brief 获取IMU数据接口
         * 为什么不用结构体？因为该板载接口随时可能被弃用，但是上层会一直使用，应该是上层规划下层，而不是下层约束上层
         * @param accel_x 加速度x
         * @param accel_y 加速度y
         * @param accel_z 加速度z
         * @param gyro_x 陀螺仪x
         * @param gyro_y 陀螺仪y
         * @param gyro_z 陀螺仪z
         * @return true 读取成功
         * @return false 数据超过5s没更新，更新超时
         */
        static bool imuDataGet(float &accel_x, float &accel_y, float &accel_z,
                               float &gyro_x, float &gyro_y, float &gyro_z);

        /**
         * @brief 获取系统电压数据接口
         *
         * @param mv 毫伏
         * @return true 读取最新数据成功
         * @return false 数据超时
         */
        static bool voltageGet(uint16_t &mv);

        /**
         * @brief 控制单个电机
         *
         * @param motor_id 电机ID 0~3，取决于控制板
         * @param speed_rs 转速，单位r/s，+正转 -反转
         * @return true 发送帧成功
         * @return false 发送帧失败
         */
        static bool motorCtrl(const uint8_t motor_id, const float speed_rs);

        /**
         * @brief 控制多个电机
         *
         * @param mt_op 字典，id:rs
         * @return true 发送帧成功
         * @return false 发送帧失败
         */
        static bool motorCtrl(const std::map<uint8_t, float> &mt_op);

        /**
         * @brief 停止单个电机(急刹切断, Ctrl0速度是控制板PID制动)
         *
         * @param motor_id 电机id 0~3
         * @return true 发送帧成功
         * @return false 发送帧失败
         */
        static bool motorStop(const uint8_t motor_id);

        /**
         * @brief 停止多个电机(急刹切断, Ctrl0速度是控制板PID制动)
         *
         * @param mt_op vector，id
         * @return true 发送帧成功
         * @return false 发送帧失败
         */
        static bool motorStop(const std::vector<uint8_t> &mt_op);

    private:
        explicit ControllerBoard(); // 要求显式调用构造函数
        ~ControllerBoard();         // 析构函数

        static ControllerBoard &getInstance(); // 获取全局静态私有单例句柄

        std::unique_ptr<ControllerComm> comm_handle_; // 唯一通信句柄指针

        std::mutex data_mutex_;             // 数据更新，读取、写入互斥锁(有可能会被多处同时读写的共享资源必须加互斥锁)
        uint16_t mv_;                       // 电压数据
        float accel_x_, accel_y_, accel_z_; // 加速度,根据RRC与上位机通信协议分析.pdf,这里是已经换算成功的加速度物理值,而不是ADC值,与ros2 imu_tools输入一致
        float gyro_x_, gyro_y_, gyro_z_;    // 陀螺仪,根据RRC与上位机通信协议分析.pdf,这里是已经换算成功的陀螺仪物理值,而不是ADC值,与ros2 imu_tools输入一致
        uint64_t last_update_time_;         // 上次更新时间戳

        void parseFrame(const std::vector<uint8_t> &frame); // 解析帧
        template <typename T>
        T bytesToValue(const uint8_t *data, size_t offset); // 字节流转数值模板函数，小端模型

        bool sendFrame(uint8_t func, const std::vector<uint8_t> &params); // 发送帧
        void floatToBytes(float value, uint8_t *bytes);                   // 浮点数转字节流，小端模式

        // 私有实现（供静态方法调用）
        bool imuDataGet_private(float &accel_x, float &accel_y, float &accel_z,
                                float &gyro_x, float &gyro_y, float &gyro_z);
        bool voltageGet_private(uint16_t &mv);
        bool motorCtrl_private(const uint8_t motor_id, const float speed_rs);
        bool motorCtrl_private(const std::map<uint8_t, float> &mt_op);
        bool motorStop_private(const uint8_t motor_id);
        bool motorStop_private(const std::vector<uint8_t> &mt_op);

        // ============ 配置 ============
        struct Config
        {
            std::string port = "/dev/ttyACM0";
            int baudrate = 1000000;
            int timeout_ms = 100;
            int data_timeout_ms = 5000;
            int motor_count = 4;
            float max_speed = 1.33f;
            float min_speed = -1.33f;
        } config_;
        void loadConfig(); // 加载配置函数
    };

} // namespace drivers