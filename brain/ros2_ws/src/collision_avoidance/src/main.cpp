#include "collision_avoidance/simple_collision_monitor.hpp"
#include <rclcpp/rclcpp.hpp>

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  auto node = std::make_shared<collision_avoidance::SimpleCollisionMonitor>();
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}