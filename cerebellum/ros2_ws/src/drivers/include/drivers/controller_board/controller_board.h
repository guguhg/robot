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
         * @brief 单例获取接口
         *
         * @return ControllerBoard*
         */
        static ControllerBoard *getInstance();

        /**
         * @brief 单例销毁
         *
         */
        static void destroyInstance();

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
        bool imuDataGet(float &accel_x, float &accel_y, float &accel_z,
                        float &gyro_x, float &gyro_y, float &gyro_z);

        /**
         * @brief 获取系统电压数据接口
         *
         * @param mv 毫伏
         * @return true 读取最新数据成功
         * @return false 数据超时
         */
        bool voltageGet(uint16_t &mv);

        /**
         * @brief 控制单个电机
         *
         * @param motor_id 电机ID 0~3，取决于控制板
         * @param speed_rs 转速，单位r/s，+正转 -反转
         * @return true 发送帧成功
         * @return false 发送帧失败
         */
        bool motorCtrl(const uint8_t motor_id, const float speed_rs);

        /**
         * @brief 控制多个电机
         *
         * @param mt_op 字典，id:rs
         * @return true 发送帧成功
         * @return false 发送帧失败
         */
        bool motorCtrl(const std::map<uint8_t, float> &mt_op);

        /**
         * @brief 停止单个电机
         *
         * @param motor_id 电机id 0~3
         * @return true 发送帧成功
         * @return false 发送帧失败
         */
        bool motorStop(const uint8_t motor_id);

        /**
         * @brief 停止多个电机
         *
         * @param mt_op vector，id
         * @return true 发送帧成功
         * @return false 发送帧失败
         */
        bool motorStop(const std::vector<uint8_t> &mt_op);

    private:
        static ControllerBoard *instance_; // 单例句柄
        static std::mutex instance_mutex_; // 单例互斥锁

        explicit ControllerBoard(); // 要求显式调用构造函数
        ~ControllerBoard();         // 析构函数

        std::unique_ptr<ControllerComm> comm_handle_; // 唯一通信句柄指针

        std::mutex data_mutex_;             // 数据更新，读取、写入互斥锁
        uint16_t mv_;                       // 电压数据
        float accel_x_, accel_y_, accel_z_; // 加速度
        float gyro_x_, gyro_y_, gyro_z_;    // 陀螺仪
        uint64_t last_update_time_;         // 上次更新时间戳

        void parseFrame(const std::vector<uint8_t> &frame); // 解析帧
        template <typename T>
        T bytesToValue(const uint8_t *data, size_t offset); // 字节流转数值模板函数，小端模型

        bool sendFrame(uint8_t func, const std::vector<uint8_t> &params); // 发送帧
        void floatToBytes(float value, uint8_t *bytes);                   // 浮点数转字节流，小端模式

        // ============ 配置 ============
        struct Config
        {
            std::string port = "/dev/ttyACM0";
            int baudrate = 1000000;
            int timeout_ms = 100;
            int data_timeout_ms = 5000;
            int motor_count = 4;
            float max_speed = 10.0f;
            float min_speed = -10.0f;
        } config_;
        void loadConfig();//加载配置函数
    };

} // namespace drivers