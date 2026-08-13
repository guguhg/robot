// cerebellum_dri.hpp
#pragma once

#include "rclcpp/rclcpp.hpp"
#include "chassis_dri.hpp"
#include "imu_dri.hpp"
#include "bms_dri.hpp"

#include <memory>

namespace dri_interfaces {
/**
 * @brief 小脑驱动接口层父节点,共享进程,使用同一个硬件实例
 * 
 */
class CerebellumDriver : public rclcpp::Node
{
public:
    explicit CerebellumDriver(const rclcpp::NodeOptions& options = rclcpp::NodeOptions());//构造函数
    ~CerebellumDriver() = default;//析构函数

    /**
     * @brief 获取子节点
     * 
     * @return 子节点
     */
    std::vector<std::shared_ptr<rclcpp::Node>> get_child_nodes() const {
        return {chassis_node_, imu_node_, bms_node_};
    }

private:
    std::shared_ptr<ChassisDriver> chassis_node_;//共享指针,底盘
    std::shared_ptr<IMUDriver> imu_node_;//共享指针,imu
    std::shared_ptr<BMSDriver> bms_node_;//共享指针,bms
};

}  // namespace dri_interfaces