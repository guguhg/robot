#include "drivers/controller_board/controller_board.h"
#include "drivers/imu/imu.h"
#include "common/config_loader/config_loader.hpp"
#include "common/logger/logger.hpp"

namespace drivers
{
    /**
     * @brief 全局唯一静态IMU实例句柄
     * 
     * @return IMU& 
     */
    IMU &IMU::getInstance()
    {
        static IMU instance;
        return instance;
    }

    /**
     * @brief 构造函数
     * 
     */
    IMU::IMU()
    {
        LOG_INFO("[IMU] Initialized.\n");
    }

    /**
     * @brief 获取IMU数据具体实现
     * 
     * @return IMUData 
     */
    IMUData IMU::getImuData_private()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        data_.valid = drivers::ControllerBoard::imuDataGet(data_.accel_x, data_.accel_y, data_.accel_z,
                                                            data_.gyro_x, data_.gyro_y, data_.gyro_z);         
        return data_;
    }

    /**
     * @brief 外部静态接口
     * 
     * @return IMUData 
     */
    IMUData IMU::getImuData()
    {
        return getInstance().getImuData_private();
    }
}