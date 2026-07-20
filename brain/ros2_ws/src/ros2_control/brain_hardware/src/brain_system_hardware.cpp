#include "brain_hardware/brain_system_hardware.hpp"

#include <hardware_interface/types/hardware_interface_type_values.hpp>
#include <pluginlib/class_list_macros.hpp>

namespace brain_hardware {

// ========== 获取独立节点 ==========
static std::shared_ptr<rclcpp::Node> get_hardware_node() {
  static auto node = rclcpp::Node::make_shared("brain_hardware_node");
  return node;
}

// ========== 生命周期 ==========
hardware_interface::CallbackReturn BrainSystemHardware::on_init(
    const hardware_interface::HardwareInfo &info) {
  if (hardware_interface::SystemInterface::on_init(info) !=
      hardware_interface::CallbackReturn::SUCCESS) {
    return hardware_interface::CallbackReturn::ERROR;
  }

  joint_names_.clear();
  for (size_t i = 0; i < info_.joints.size(); ++i) {
    joint_names_.push_back(info_.joints[i].name);
    joint_index_[info_.joints[i].name] = i;
  }

  size_t num_joints = joint_names_.size();
  hw_commands_.resize(num_joints, 0.0);
  hw_states_.resize(num_joints, 0.0);
  hw_positions_.resize(num_joints, 0.0);

  RCLCPP_INFO(rclcpp::get_logger("BrainSystemHardware"),
              "Initialized with %zu joints", num_joints);
  return hardware_interface::CallbackReturn::SUCCESS;
}

//on_configure：创建订阅和发布器
hardware_interface::CallbackReturn BrainSystemHardware::on_configure(
    const rclcpp_lifecycle::State & /*previous_state*/) {
  RCLCPP_INFO(rclcpp::get_logger("BrainSystemHardware"), "Configuring...");

  auto node = get_hardware_node();

  motor_states_sub_ = node->create_subscription<interfaces::msg::MotorStates>(
      "/chassis/motor_states", 10,
      std::bind(&BrainSystemHardware::motorStatesCallback, this, std::placeholders::_1));

  motor_cmd_pub_ = node->create_publisher<interfaces::msg::MotorCmd>(
      "/chassis/motor_cmd", 10);

  RCLCPP_INFO(rclcpp::get_logger("BrainSystemHardware"),
              "Subscribed to /chassis/motor_states, publishing to /chassis/motor_cmd");

  return hardware_interface::CallbackReturn::SUCCESS;
}

//on_activate：启动 spin 线程 
hardware_interface::CallbackReturn BrainSystemHardware::on_activate(
    const rclcpp_lifecycle::State & /*previous_state*/) {
  RCLCPP_INFO(rclcpp::get_logger("BrainSystemHardware"), "Activating...");

  for (size_t i = 0; i < hw_states_.size(); ++i) {
    hw_states_[i] = 0.0;
  }

  for (size_t i = 0; i < hw_positions_.size(); ++i) {
    hw_positions_[i] = 0.0;
  }
  
  // 启动 spin 线程处理订阅回调
  if (!spin_thread_) {
    auto node = get_hardware_node();
    spin_thread_ = std::make_shared<std::thread>(
        [node]() {
          rclcpp::spin(node);
        });
  }
  
  return hardware_interface::CallbackReturn::SUCCESS;
}

hardware_interface::CallbackReturn BrainSystemHardware::on_deactivate(
    const rclcpp_lifecycle::State & /*previous_state*/) {
  RCLCPP_INFO(rclcpp::get_logger("BrainSystemHardware"), "Deactivating...");
  
  motor_states_sub_.reset();
  motor_cmd_pub_.reset();
  
  if (spin_thread_ && spin_thread_->joinable()) {
    spin_thread_->detach();
  }
  spin_thread_.reset();
  
  return hardware_interface::CallbackReturn::SUCCESS;
}

// ========== 接口导出 ==========
std::vector<hardware_interface::StateInterface>
BrainSystemHardware::export_state_interfaces() {
  std::vector<hardware_interface::StateInterface> state_interfaces;
  for (size_t i = 0; i < joint_names_.size(); ++i) {
    state_interfaces.emplace_back(
        joint_names_[i], hardware_interface::HW_IF_VELOCITY, &hw_states_[i]);
    state_interfaces.emplace_back(
        joint_names_[i], hardware_interface::HW_IF_POSITION, &hw_positions_[i]);
  }
  return state_interfaces;
}

std::vector<hardware_interface::CommandInterface>
BrainSystemHardware::export_command_interfaces() {
  std::vector<hardware_interface::CommandInterface> command_interfaces;
  for (size_t i = 0; i < joint_names_.size(); ++i) {
    command_interfaces.emplace_back(
        joint_names_[i], hardware_interface::HW_IF_VELOCITY, &hw_commands_[i]);
  }
  return command_interfaces;
}

// ========== 回调 ==========
void BrainSystemHardware::motorStatesCallback(
    const interfaces::msg::MotorStates::SharedPtr msg) {
  auto it = joint_index_.find("wheel_front_left_joint");
  if (it != joint_index_.end()) hw_states_[it->second] = msg->left_front;

  it = joint_index_.find("wheel_front_right_joint");
  if (it != joint_index_.end()) hw_states_[it->second] = -msg->right_front; //右轮-rs是前进，*-1=+rs

  it = joint_index_.find("wheel_back_left_joint");
  if (it != joint_index_.end()) hw_states_[it->second] = msg->left_rear;

  it = joint_index_.find("wheel_back_right_joint");
  if (it != joint_index_.end()) hw_states_[it->second] = -msg->right_rear;
}

// ========== 读写 ==========
hardware_interface::return_type BrainSystemHardware::read(
    const rclcpp::Time & /*time*/,
    const rclcpp::Duration & /*period*/) {
  return hardware_interface::return_type::OK;
}

hardware_interface::return_type BrainSystemHardware::write(
    const rclcpp::Time & /*time*/,
    const rclcpp::Duration & /*period*/) {
  if (!motor_cmd_pub_) return hardware_interface::return_type::OK;

  auto msg = interfaces::msg::MotorCmd();
  msg.left_front = 0.0;
  msg.right_front = 0.0;
  msg.left_rear = 0.0;
  msg.right_rear = 0.0;

  auto it = joint_index_.find("wheel_front_left_joint");
  if (it != joint_index_.end()) msg.left_front = hw_commands_[it->second];

  it = joint_index_.find("wheel_front_right_joint");
  if (it != joint_index_.end()) msg.right_front = -hw_commands_[it->second];

  it = joint_index_.find("wheel_back_left_joint");
  if (it != joint_index_.end()) msg.left_rear = hw_commands_[it->second];

  it = joint_index_.find("wheel_back_right_joint");
  if (it != joint_index_.end()) msg.right_rear = -hw_commands_[it->second];

  motor_cmd_pub_->publish(msg);
  return hardware_interface::return_type::OK;
}

}  // namespace brain_hardware

PLUGINLIB_EXPORT_CLASS(
    brain_hardware::BrainSystemHardware,
    hardware_interface::SystemInterface)