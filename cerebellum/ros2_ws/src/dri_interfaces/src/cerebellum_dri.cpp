// cerebellum_dri.cpp
#include "dri_interfaces/cerebellum_dri.hpp"

namespace dri_interfaces {

/**
 * @brief 构造函数,启动子节点
 * 
 * @param options 
 */
CerebellumDriver::CerebellumDriver(const rclcpp::NodeOptions& options)
    : Node("cerebellum_driver", options)
{
    RCLCPP_INFO(this->get_logger(), "=== CerebellumDriver Starting ===");
    RCLCPP_INFO(this->get_logger(), "All nodes share the same ControllerBoard singleton");

    //让多个对象共享同一个资源，当最后一个持有者销毁时自动释放资源。这里的资源主要是硬件资源(如底层的控制板通信串口)
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
    
    rclcpp::executors::SingleThreadedExecutor executor;//单线执行器,多个节点在同一个ros2线程中运行,当回调少时用单线程,回调多就用多线程
    executor.add_node(manager);//添加父节点
    
    for (auto& child : manager->get_child_nodes()) {
        executor.add_node(child);//添加子节点
    }
    
    RCLCPP_INFO(manager->get_logger(), "All nodes added to executor, starting spin...");
    executor.spin();//事件循环
    
    rclcpp::shutdown();
    return 0;
}