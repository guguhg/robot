#pragma once
#include <mutex>
#include <cstdint>

namespace drivers
{
    /**
     * @brief IMU数据结构体，数据都是物理量，不是ADC值！
     * 
     */
    struct IMUData
    {
        float accel_x = 0.0f;//加速度物理量，不是adc
        float accel_y = 0.0f;
        float accel_z = 0.0f;
        float gyro_x = 0.0f;//陀螺仪物理量，不是adc
        float gyro_y = 0.0f;
        float gyro_z = 0.0f;
        bool valid = false;//本次数据是否最新(config.yaml配置超时时间)
    };

    class IMU
    {
    public:
        /**
         * @brief 全局静态获取IMU数据
         * 
         * @return IMUData IMU数据
         */
        static IMUData getImuData(); 

        //禁止拷贝
        IMU(const IMU &) = delete;
        IMU &operator=(const IMU &) = delete;

    private:
        explicit IMU(); //构造函数
        ~IMU() = default;         
        static IMU& getInstance();// 获取全局静态私有单例句柄

        IMUData data_;//IMU数据

        IMUData getImuData_private();//getImuData具体实现
        std::mutex mutex_;//共享资源互斥锁
    };
}