#pragma once

#include <hardware_interface/system_interface.hpp>
#include <hardware_interface/handle.hpp>
#include <hardware_interface/hardware_info.hpp>
#include <rclcpp/rclcpp.hpp>

#include "interfaces/msg/motor_cmd.hpp"
#include "interfaces/msg/motor_states.hpp"

#include <vector>
#include <string>
#include <map>
#include <memory>
#include <thread>

namespace brain_hardware {

class BrainSystemHardware : public hardware_interface::SystemInterface {
public:
  RCLCPP_SHARED_PTR_DEFINITIONS(BrainSystemHardware)

  hardware_interface::CallbackReturn on_init(
    const hardware_interface::HardwareInfo &info) override;

  hardware_interface::CallbackReturn on_configure(
    const rclcpp_lifecycle::State &previous_state) override;

  hardware_interface::CallbackReturn on_activate(
    const rclcpp_lifecycle::State &previous_state) override;

  hardware_interface::CallbackReturn on_deactivate(
    const rclcpp_lifecycle::State &previous_state) override;

  std::vector<hardware_interface::StateInterface> export_state_interfaces() override;
  std::vector<hardware_interface::CommandInterface> export_command_interfaces() override;

  hardware_interface::return_type read(
    const rclcpp::Time &time, const rclcpp::Duration &period) override;

  hardware_interface::return_type write(
    const rclcpp::Time &time, const rclcpp::Duration &period) override;

private:
  std::vector<std::string> joint_names_;
  std::vector<double> hw_commands_;
  std::vector<double> hw_states_;
  std::vector<double> hw_positions_;
  std::map<std::string, size_t> joint_index_;

  rclcpp::Subscription<interfaces::msg::MotorStates>::SharedPtr motor_states_sub_;
  rclcpp::Publisher<interfaces::msg::MotorCmd>::SharedPtr motor_cmd_pub_;

  std::shared_ptr<std::thread> spin_thread_;

  void motorStatesCallback(const interfaces::msg::MotorStates::SharedPtr msg);
};

}  // namespace brain_hardware