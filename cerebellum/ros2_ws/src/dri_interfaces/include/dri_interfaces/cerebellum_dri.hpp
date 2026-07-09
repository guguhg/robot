// cerebellum_dri.hpp
#pragma once

#include "rclcpp/rclcpp.hpp"
#include "chassis_dri.hpp"
#include "imu_dri.hpp"
#include "bms_dri.hpp"

#include <memory>

namespace dri_interfaces {

class CerebellumDriver : public rclcpp::Node
{
public:
    explicit CerebellumDriver(const rclcpp::NodeOptions& options = rclcpp::NodeOptions());
    ~CerebellumDriver() = default;

    // 获取子节点（用于执行器注册）
    std::vector<std::shared_ptr<rclcpp::Node>> get_child_nodes() const {
        return {chassis_node_, imu_node_, bms_node_};
    }

private:
    std::shared_ptr<ChassisDriver> chassis_node_;
    std::shared_ptr<IMUDriver> imu_node_;
    std::shared_ptr<BMSDriver> bms_node_;
};

}  // namespace dri_interfaces