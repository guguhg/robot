// cerebellum_dri.cpp
#include "dri_interfaces/cerebellum_dri.hpp"

namespace dri_interfaces {

CerebellumDriver::CerebellumDriver(const rclcpp::NodeOptions& options)
    : Node("cerebellum_driver", options)
{
    RCLCPP_INFO(this->get_logger(), "=== CerebellumDriver Starting ===");
    RCLCPP_INFO(this->get_logger(), "All nodes share the same ControllerBoard singleton");

    chassis_node_ = std::make_shared<ChassisDriver>();
    imu_node_ = std::make_shared<IMUDriver>();
    bms_node_ = std::make_shared<BMSDriver>();

    RCLCPP_INFO(this->get_logger(), "  - ChassisDriver created");
    RCLCPP_INFO(this->get_logger(), "  - IMUDriver created");
    RCLCPP_INFO(this->get_logger(), "  - BMSDriver created");
    RCLCPP_INFO(this->get_logger(), "CerebellumDriver initialized successfully");
}

}  // namespace dri_interfaces

int main(int argc, char** argv)
{
    rclcpp::init(argc, argv);
    
    auto manager = std::make_shared<dri_interfaces::CerebellumDriver>();
    
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(manager);
    
    for (auto& child : manager->get_child_nodes()) {
        executor.add_node(child);
    }
    
    RCLCPP_INFO(manager->get_logger(), "All nodes added to executor, starting spin...");
    executor.spin();
    
    rclcpp::shutdown();
    return 0;
}