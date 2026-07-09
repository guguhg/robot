#pragma once

#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/imu.hpp"

#include "drivers/imu/imu.h"
#include "common/config_loader/config_loader.hpp"

#include <string>

namespace dri_interfaces {

/**
 * @brief IMU 驱动接口节点
 * 
 * 职责：
 * 1. 从 drivers::IMU 读取 IMU 数据
 * 2. 根据 axis_mapping 将数据转换到 ROS2 标准坐标系
 * 3. 发布 sensor_msgs/Imu 消息
 * 
 * 超时处理由驱动层通过 IMUData.valid 字段完成
 */
class IMUDriver : public rclcpp::Node
{
public:
    explicit IMUDriver(const rclcpp::NodeOptions& options = rclcpp::NodeOptions());
    ~IMUDriver() = default;

private:
    // ============ 配置结构 ============
    struct Config {
        std::string topic_name = "/imu/data_raw";
        int publish_rate = 100;
        std::string frame_id = "imu_link";
        std::string front = "x";
        std::string left = "y";
        std::string up = "z";
    } config_;

    // ============ 轴映射缓存 ============
    struct AxisMapping {
        int front_idx = 0;
        int left_idx = 1;
        int up_idx = 2;
        float front_sign = 1.0f;
        float left_sign = 1.0f;
        float up_sign = 1.0f;
    } axis_map_;

    // ============ ROS2 通信 ============
    rclcpp::Publisher<sensor_msgs::msg::Imu>::SharedPtr imu_pub_;
    rclcpp::TimerBase::SharedPtr publish_timer_;

    // ============ 方法 ============
    void loadConfig();
    void loadAxisMapping();
    void initROS();
    void publishIMU();

    sensor_msgs::msg::Imu convertToROSMsg(const drivers::IMUData& data);
    float mapAxis(const float values[3], int idx, float sign);
    int parseAxisName(const std::string& name, float& sign);
};

}  // namespace dri_interfaces
