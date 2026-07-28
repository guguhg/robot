#include "brain_controller/brain_controller.hpp"

#include <controller_interface/controller_interface.hpp>
#include <pluginlib/class_list_macros.hpp>
#include <hardware_interface/types/hardware_interface_type_values.hpp>

namespace brain_controller {

BrainController::BrainController() : ControllerInterface() {}

// ========== 生命周期 ==========
controller_interface::CallbackReturn BrainController::on_init() {
  try {
    auto node = get_node();
    node->declare_parameter("wheel_radius", 0.04);
    node->declare_parameter("wheel_separation_h", 0.18);
    node->declare_parameter("wheel_separation_w", 0.173);
    node->declare_parameter("max_speed", 1.33);
    node->declare_parameter("cmd_vel_topic", "/cmd_vel_safe");

    node->get_parameter("wheel_radius", wheel_radius_);
    node->get_parameter("wheel_separation_h", wheel_separation_h_);
    node->get_parameter("wheel_separation_w", wheel_separation_w_);
    node->get_parameter("max_speed", max_speed_);
    node->get_parameter("cmd_vel_topic", cmd_vel_topic_);

    RCLCPP_INFO(node->get_logger(), "========================================");
    RCLCPP_INFO(node->get_logger(), "BrainController Parameter Loading:");
    RCLCPP_INFO(node->get_logger(), "  wheel_radius         = %f", wheel_radius_);
    RCLCPP_INFO(node->get_logger(), "  wheel_separation_h   = %f", wheel_separation_h_);
    RCLCPP_INFO(node->get_logger(), "  wheel_separation_w   = %f", wheel_separation_w_);
    RCLCPP_INFO(node->get_logger(), "  max_speed            = %f", max_speed_);
    RCLCPP_INFO(node->get_logger(), "  cmd_vel_topic        = %s", cmd_vel_topic_.c_str());
    RCLCPP_INFO(node->get_logger(), "========================================");

    joint_names_ = {
      "wheel_front_left_joint",
      "wheel_front_right_joint",
      "wheel_back_left_joint",
      "wheel_back_right_joint"
    };

    odometry_.setWheelSeparation(wheel_separation_h_);

    RCLCPP_INFO(node->get_logger(), "BrainController initialized");
    return controller_interface::CallbackReturn::SUCCESS;
  } catch (const std::exception &e) {
    RCLCPP_ERROR(get_node()->get_logger(), "on_init failed: %s", e.what());
    return controller_interface::CallbackReturn::ERROR;
  }
}

controller_interface::CallbackReturn BrainController::on_configure(
    const rclcpp_lifecycle::State & /*previous_state*/) {
  auto node = get_node();
  RCLCPP_INFO(node->get_logger(), "Configuring...");

  cmd_vel_sub_ = node->create_subscription<geometry_msgs::msg::Twist>(
    cmd_vel_topic_, 1,
    std::bind(&BrainController::cmdVelCallback, this, std::placeholders::_1));
  RCLCPP_INFO(node->get_logger(), "Subscribed to %s", cmd_vel_topic_.c_str());

  odometry_publisher_ = node->create_publisher<nav_msgs::msg::Odometry>(
    "/odom", 1);
  //tf_broadcaster_ = std::make_shared<tf2_ros::TransformBroadcaster>(node);
  RCLCPP_INFO(node->get_logger(), "publishing /odom");
  return controller_interface::CallbackReturn::SUCCESS;
}

controller_interface::CallbackReturn BrainController::on_activate(
    const rclcpp_lifecycle::State & /*previous_state*/) {
  auto node = get_node();
  RCLCPP_INFO(node->get_logger(), "Activating...");

  odometry_.reset();

  if (!register_joint_handles()) {
    return controller_interface::CallbackReturn::ERROR;
  }

  RCLCPP_INFO(node->get_logger(), "Activated with %zu joints", wheel_handles_.size());
  return controller_interface::CallbackReturn::SUCCESS;
}

controller_interface::CallbackReturn BrainController::on_deactivate(
    const rclcpp_lifecycle::State & /*previous_state*/) {
  RCLCPP_INFO(get_node()->get_logger(), "Deactivating...");
  wheel_handles_.clear();
  return controller_interface::CallbackReturn::SUCCESS;
}

// ========== 接口配置 ==========
controller_interface::InterfaceConfiguration BrainController::command_interface_configuration() const {
  controller_interface::InterfaceConfiguration config;
  config.type = controller_interface::interface_configuration_type::INDIVIDUAL;

  for (const auto &joint : joint_names_) {
    config.names.push_back(joint + "/" + hardware_interface::HW_IF_VELOCITY);
  }

  return config;
}

controller_interface::InterfaceConfiguration BrainController::state_interface_configuration() const {
  controller_interface::InterfaceConfiguration config;
  config.type = controller_interface::interface_configuration_type::INDIVIDUAL;

  for (const auto &joint : joint_names_) {
    config.names.push_back(joint + "/" + hardware_interface::HW_IF_VELOCITY);
  }

  for (const auto &joint : joint_names_) {
    config.names.push_back(joint + "/" + hardware_interface::HW_IF_POSITION);
  }

  return config;
}

// ========== 注册句柄 ==========
bool BrainController::register_joint_handles() {
  wheel_handles_.clear();
  wheel_handles_.reserve(joint_names_.size());

  for (const auto &joint_name : joint_names_) {
    auto cmd_it = std::find_if(
      command_interfaces_.begin(),
      command_interfaces_.end(),
      [&joint_name](const hardware_interface::LoanedCommandInterface &interface) {
        return interface.get_prefix_name() == joint_name &&
               interface.get_interface_name() == hardware_interface::HW_IF_VELOCITY;
      });

    if (cmd_it == command_interfaces_.end()) {
      RCLCPP_ERROR(get_node()->get_logger(), "Cannot find velocity command for %s", joint_name.c_str());
      return false;
    }

    auto state_it = std::find_if(
      state_interfaces_.cbegin(),
      state_interfaces_.cend(),
      [&joint_name](const hardware_interface::LoanedStateInterface &interface) {
        return interface.get_prefix_name() == joint_name &&
               interface.get_interface_name() == hardware_interface::HW_IF_VELOCITY;
      });

    if (state_it == state_interfaces_.cend()) {
      RCLCPP_ERROR(get_node()->get_logger(), "Cannot find velocity state for %s", joint_name.c_str());
      return false;
    }

    auto pos_it = std::find_if(
      state_interfaces_.cbegin(),
      state_interfaces_.cend(),
      [&joint_name](const hardware_interface::LoanedStateInterface &interface) {
        return interface.get_prefix_name() == joint_name &&
               interface.get_interface_name() == hardware_interface::HW_IF_POSITION;
      });

    if (pos_it == state_interfaces_.cend()) {
      RCLCPP_ERROR(get_node()->get_logger(), "Cannot find position state for %s", joint_name.c_str());
      return false;
    }

    wheel_handles_.push_back({std::ref(*cmd_it), std::ref(*state_it),  std::ref(*pos_it)});
    RCLCPP_INFO(get_node()->get_logger(), "Registered handle for %s", joint_name.c_str());
  }

  return true;
}

// ========== 回调 ==========
void BrainController::cmdVelCallback(const geometry_msgs::msg::Twist::SharedPtr msg) {
  velocity_command_buffer_.writeFromNonRT(*msg);
}

// ========== 麦轮逆运动学 ==========
void BrainController::computeWheelVelocities(
    double vx, double vy, double omega,
    double &w_LF, double &w_RF, double &w_LR, double &w_RR) {
  const double r = wheel_radius_;
  const double L = wheel_separation_h_ / 2.0;
  const double W = wheel_separation_w_ / 2.0;
  const double MAX_SPEED = max_speed_;
  const double RAD_TO_RS = 1.0 / (2.0 * M_PI);

  double w_LF_rad = (vx - vy - (L + W) * omega) / r;
  double w_RF_rad = (vx + vy + (L + W) * omega) / r;
  double w_LR_rad = (vx + vy - (L + W) * omega) / r;
  double w_RR_rad = (vx - vy + (L + W) * omega) / r;

  // rad/s → r/s
  w_LF = w_LF_rad * RAD_TO_RS;
  w_RF = w_RF_rad * RAD_TO_RS;
  w_LR = w_LR_rad * RAD_TO_RS;
  w_RR = w_RR_rad * RAD_TO_RS;

  w_LF = std::clamp(w_LF, -MAX_SPEED, MAX_SPEED);
  w_RF = std::clamp(w_RF, -MAX_SPEED, MAX_SPEED);
  w_LR = std::clamp(w_LR, -MAX_SPEED, MAX_SPEED);
  w_RR = std::clamp(w_RR, -MAX_SPEED, MAX_SPEED);
}

// ========== 发布里程计 ==========
void BrainController::publishOdometry(const rclcpp::Time &time) {
  if (!odometry_publisher_) return;

  auto state = odometry_;

  auto odom_msg = nav_msgs::msg::Odometry();
  odom_msg.header.stamp = time;
  odom_msg.header.frame_id = "odom";
  odom_msg.child_frame_id = "base_link";

  odom_msg.pose.pose.position.x = state.getX();
  odom_msg.pose.pose.position.y = state.getY();
  odom_msg.pose.pose.position.z = 0.0;

  tf2::Quaternion q;
  q.setRPY(0.0, 0.0, state.getHeading());
  odom_msg.pose.pose.orientation.x = q.x();
  odom_msg.pose.pose.orientation.y = q.y();
  odom_msg.pose.pose.orientation.z = q.z();
  odom_msg.pose.pose.orientation.w = q.w();

  odom_msg.twist.twist.linear.x = state.getLinear();
  odom_msg.twist.twist.linear.y = 0.0;
  odom_msg.twist.twist.angular.z = state.getAngular();

  for (int i = 0; i < 36; ++i) {
    odom_msg.pose.covariance[i] = 0.0;
    odom_msg.twist.covariance[i] = 0.0;
  }
  odom_msg.pose.covariance[0] = 0.001;
  odom_msg.pose.covariance[7] = 0.001;
  odom_msg.pose.covariance[35] = 0.01;
  odom_msg.twist.covariance[0] = 0.001;
  odom_msg.twist.covariance[35] = 0.01;

  odometry_publisher_->publish(odom_msg);

  // geometry_msgs::msg::TransformStamped tf_msg;
  // tf_msg.header.stamp = time;
  // tf_msg.header.frame_id = "odom";
  // tf_msg.child_frame_id = "base_link";
  // tf_msg.transform.translation.x = state.getX();
  // tf_msg.transform.translation.y = state.getY();
  // tf_msg.transform.translation.z = 0.0;
  // tf_msg.transform.rotation.x = q.x();
  // tf_msg.transform.rotation.y = q.y();
  // tf_msg.transform.rotation.z = q.z();
  // tf_msg.transform.rotation.w = q.w();

  // tf_broadcaster_->sendTransform(tf_msg);
}

// ========== update ==========
controller_interface::return_type BrainController::update(
    const rclcpp::Time &time,
    const rclcpp::Duration &period) {
  double dt = period.seconds();
  if (dt <= 0.0) dt = 0.01;

  auto cmd_ptr = velocity_command_buffer_.readFromRT();
  if (cmd_ptr != nullptr) {
    const geometry_msgs::msg::Twist &cmd = *cmd_ptr;
    double vx = cmd.linear.x;
    double vy = cmd.linear.y;
    double omega = cmd.angular.z;

    double w_LF, w_RF, w_LR, w_RR;
    computeWheelVelocities(vx, vy, omega, w_LF, w_RF, w_LR, w_RR);

    if (wheel_handles_.size() >= 4) {
      wheel_handles_[0].velocity_command.get().set_value(w_LF);
      wheel_handles_[1].velocity_command.get().set_value(w_RF);
      wheel_handles_[2].velocity_command.get().set_value(w_LR);
      wheel_handles_[3].velocity_command.get().set_value(w_RR);
    }
  }

  //里程计更新
  if (wheel_handles_.size() >= 4) {
    // 读取反馈 (单位: r/s)
    double left_fb = (wheel_handles_[0].velocity_state.get().get_value() +
                      wheel_handles_[2].velocity_state.get().get_value()) / 2.0;
    double right_fb = (wheel_handles_[1].velocity_state.get().get_value() +
                       wheel_handles_[3].velocity_state.get().get_value()) / 2.0;

    // r/s → m/s
    double left_linear = left_fb * 2.0 * M_PI * wheel_radius_;
    double right_linear = right_fb * 2.0 * M_PI * wheel_radius_;
    
    odometry_.update(left_linear, right_linear, dt);
  }

  publishOdometry(time);

  return controller_interface::return_type::OK;
}

}  // namespace brain_controller

PLUGINLIB_EXPORT_CLASS(
  brain_controller::BrainController,
  controller_interface::ControllerInterface
)