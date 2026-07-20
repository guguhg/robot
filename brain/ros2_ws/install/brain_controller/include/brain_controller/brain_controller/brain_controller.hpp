#pragma once

#include <controller_interface/controller_interface.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/node_interfaces/lifecycle_node_interface.hpp>
#include <geometry_msgs/msg/twist.hpp>
#include <geometry_msgs/msg/transform_stamped.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <tf2_ros/transform_broadcaster.h>
#include <tf2/LinearMath/Quaternion.h>
#include <realtime_tools/realtime_buffer.h>
#include <hardware_interface/loaned_command_interface.hpp>
#include <hardware_interface/loaned_state_interface.hpp>

#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <cmath>
#include <algorithm>

namespace brain_controller {

struct WheelHandle {
  std::reference_wrapper<hardware_interface::LoanedCommandInterface> velocity_command;
  std::reference_wrapper<const hardware_interface::LoanedStateInterface> velocity_state;
  std::reference_wrapper<const hardware_interface::LoanedStateInterface> position_state;
};

class Odometry {
public:
  Odometry() : x_(0.0), y_(0.0), heading_(0.0), linear_(0.0), angular_(0.0) {}

  void reset(double x = 0.0, double y = 0.0, double heading = 0.0) {
    x_ = x;
    y_ = y;
    heading_ = heading;
    linear_ = 0.0;
    angular_ = 0.0;
  }

  void update(double left_velocity, double right_velocity, double dt) {
    double linear = (left_velocity + right_velocity) / 2.0;
    double angular = (right_velocity - left_velocity) / wheel_separation_;

    if (std::fabs(angular) < 1e-6) {
      double direction = heading_ + angular * 0.5;
      x_ += linear * std::cos(direction) * dt;
      y_ += linear * std::sin(direction) * dt;
      heading_ += angular * dt;
    } else {
      double heading_old = heading_;
      double r = linear / angular;
      heading_ += angular * dt;
      x_ += r * (std::sin(heading_) - std::sin(heading_old));
      y_ += -r * (std::cos(heading_) - std::cos(heading_old));
    }

    while (heading_ > M_PI) heading_ -= 2.0 * M_PI;
    while (heading_ < -M_PI) heading_ += 2.0 * M_PI;

    linear_ = linear;
    angular_ = angular;
  }

  void setWheelSeparation(double separation) { wheel_separation_ = separation; }

  double getX() const { return x_; }
  double getY() const { return y_; }
  double getHeading() const { return heading_; }
  double getLinear() const { return linear_; }
  double getAngular() const { return angular_; }

private:
  double x_, y_, heading_;
  double linear_, angular_;
  double wheel_separation_ = 0.18;
};

class BrainController : public controller_interface::ControllerInterface {
public:
  BrainController();
  ~BrainController() = default;

  controller_interface::CallbackReturn on_init() override;
  controller_interface::CallbackReturn on_configure(
    const rclcpp_lifecycle::State &previous_state) override;
  controller_interface::CallbackReturn on_activate(
    const rclcpp_lifecycle::State &previous_state) override;
  controller_interface::CallbackReturn on_deactivate(
    const rclcpp_lifecycle::State &previous_state) override;

  controller_interface::InterfaceConfiguration command_interface_configuration() const override;
  controller_interface::InterfaceConfiguration state_interface_configuration() const override;

  controller_interface::return_type update(
    const rclcpp::Time &time, const rclcpp::Duration &period) override;

private:
  double wheel_radius_;
  double wheel_separation_h_;
  double wheel_separation_w_;
  double max_speed_;

  realtime_tools::RealtimeBuffer<geometry_msgs::msg::Twist> velocity_command_buffer_;

  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr cmd_vel_sub_;
  std::shared_ptr<rclcpp::Publisher<nav_msgs::msg::Odometry>> odometry_publisher_;
  std::shared_ptr<tf2_ros::TransformBroadcaster> tf_broadcaster_;

  std::vector<WheelHandle> wheel_handles_;
  std::vector<std::string> joint_names_;

  Odometry odometry_;

  void cmdVelCallback(const geometry_msgs::msg::Twist::SharedPtr msg);
  void computeWheelVelocities(
    double vx, double vy, double omega,
    double &w_LF, double &w_RF, double &w_LR, double &w_RR);
  bool register_joint_handles();
  void publishOdometry(const rclcpp::Time &time);
};

}  // namespace brain_controller