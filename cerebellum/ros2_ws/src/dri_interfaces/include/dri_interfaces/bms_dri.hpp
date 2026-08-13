#pragma once

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/float32.hpp"

#include "drivers/bms/bms.h"
#include "common/config_loader/config_loader.hpp"

#include <string>

namespace dri_interfaces {

/**
 * @brief BMS 驱动接口节点
 * 
 * 职责：
 * 1. 从 drivers::BMS 读取电池数据
 * 2. 发布电压 (V) 和电量 (%) 到 ROS2 话题
 * 
 * 超时处理由驱动层通过 BMSData.valid 字段完成
 */
class BMSDriver : public rclcpp::Node
{
public:
    explicit BMSDriver(const rclcpp::NodeOptions& options = rclcpp::NodeOptions());
    ~BMSDriver() = default;

private:
    // ============ 配置结构 ============
    struct Config {
        std::string voltage_topic = "/bms/voltage";
        std::string soc_topic = "/bms/soc";
        int publish_rate = 10;
    } config_;

    // ============ ROS2 通信 ============
    rclcpp::Publisher<std_msgs::msg::Float32>::SharedPtr voltage_pub_;
    rclcpp::Publisher<std_msgs::msg::Float32>::SharedPtr soc_pub_;   // ✅ 改用 Float32
    rclcpp::TimerBase::SharedPtr publish_timer_;

    // ============ 方法 ============
    void loadConfig();
    void initROS();
    void publishBMS();
};

}  // namespace dri_interfaces