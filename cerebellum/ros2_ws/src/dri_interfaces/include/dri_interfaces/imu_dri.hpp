/**
 * @file imu_dri.hpp
 * @brief IMU 驱动接口节点
 * 
 * 职责:
 *   1. 读取 IMU 原始数据 (加速度 g, 角速度 deg/s)
 *   2. 单位转换: g → m/s², deg/s → rad/s
 *   3. 零漂修正 (减去 bias)
 *   4. 互补滤波 → 四元数
 *   5. 发布 sensor_msgs/Imu (含四元数)
 * 
 * 轴映射由 URDF 的 imu_joint 完成
 */

#pragma once

#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/imu.hpp"

#include "drivers/imu/imu.h"
#include "common/config_loader/config_loader.hpp"

#include <string>
#include <Eigen/Dense>

namespace dri_interfaces {

/**
 * @brief IMU 驱动接口节点 (含互补滤波)
 */
class IMUDriver : public rclcpp::Node
{
public:
    explicit IMUDriver(const rclcpp::NodeOptions &options = rclcpp::NodeOptions());
    ~IMUDriver() = default;

private:
    // =========================================================================
    // 配置结构体
    // =========================================================================
    struct Config
    {
        std::string topic_name = "/imu/data_raw";
        int publish_rate = 100;
        std::string frame_id = "imu_link";
        int accel_unit = 0;              // 0: g → m/s², 1: m/s² 透传
        int gyro_unit = 0;               // 0: deg/s → rad/s, 1: rad/s 透传
        Eigen::Vector3f bias = Eigen::Vector3f::Zero();  // 陀螺仪零漂 (rad/s)
        float alpha = 0.98f;             // 互补滤波系数
    } config_;

    // =========================================================================
    // 状态
    // =========================================================================
    Eigen::Quaternionf quat_ = Eigen::Quaternionf::Identity();
    bool initialized_ = false;
    std::mutex mutex_;

    // =========================================================================
    // ROS2 通信
    // =========================================================================
    rclcpp::Publisher<sensor_msgs::msg::Imu>::SharedPtr imu_pub_;
    rclcpp::TimerBase::SharedPtr publish_timer_;

    // =========================================================================
    // 私有方法
    // =========================================================================
    void loadConfig();
    void initROS();
    void publishIMU();

    bool readIMUData(drivers::IMUData &data);
    sensor_msgs::msg::Imu convertToROSMsg(const drivers::IMUData &data);

    // =========================================================================
    // 互补滤波
    // =========================================================================
    void updateFilter(float ax, float ay, float az,
                      float gx, float gy, float gz,
                      float dt);

    Eigen::Quaternionf gyroUpdate(const Eigen::Quaternionf& q,
                                   const Eigen::Vector3f& gyro,
                                   float dt);

    Eigen::Quaternionf accelUpdate(const Eigen::Vector3f& accel);

    Eigen::Quaternionf complementaryFilter(const Eigen::Quaternionf& q_gyro,
                                            const Eigen::Quaternionf& q_accel);
};

} // namespace dri_interfaces