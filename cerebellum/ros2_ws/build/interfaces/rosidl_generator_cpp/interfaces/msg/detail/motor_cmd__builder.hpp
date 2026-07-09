// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from interfaces:msg/MotorCmd.idl
// generated code does not contain a copyright notice

#ifndef INTERFACES__MSG__DETAIL__MOTOR_CMD__BUILDER_HPP_
#define INTERFACES__MSG__DETAIL__MOTOR_CMD__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "interfaces/msg/detail/motor_cmd__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace interfaces
{

namespace msg
{

namespace builder
{

class Init_MotorCmd_right_rear
{
public:
  explicit Init_MotorCmd_right_rear(::interfaces::msg::MotorCmd & msg)
  : msg_(msg)
  {}
  ::interfaces::msg::MotorCmd right_rear(::interfaces::msg::MotorCmd::_right_rear_type arg)
  {
    msg_.right_rear = std::move(arg);
    return std::move(msg_);
  }

private:
  ::interfaces::msg::MotorCmd msg_;
};

class Init_MotorCmd_left_rear
{
public:
  explicit Init_MotorCmd_left_rear(::interfaces::msg::MotorCmd & msg)
  : msg_(msg)
  {}
  Init_MotorCmd_right_rear left_rear(::interfaces::msg::MotorCmd::_left_rear_type arg)
  {
    msg_.left_rear = std::move(arg);
    return Init_MotorCmd_right_rear(msg_);
  }

private:
  ::interfaces::msg::MotorCmd msg_;
};

class Init_MotorCmd_right_front
{
public:
  explicit Init_MotorCmd_right_front(::interfaces::msg::MotorCmd & msg)
  : msg_(msg)
  {}
  Init_MotorCmd_left_rear right_front(::interfaces::msg::MotorCmd::_right_front_type arg)
  {
    msg_.right_front = std::move(arg);
    return Init_MotorCmd_left_rear(msg_);
  }

private:
  ::interfaces::msg::MotorCmd msg_;
};

class Init_MotorCmd_left_front
{
public:
  Init_MotorCmd_left_front()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_MotorCmd_right_front left_front(::interfaces::msg::MotorCmd::_left_front_type arg)
  {
    msg_.left_front = std::move(arg);
    return Init_MotorCmd_right_front(msg_);
  }

private:
  ::interfaces::msg::MotorCmd msg_;
};

}  // namespace builder

}  // namespace msg

template<typename MessageType>
auto build();

template<>
inline
auto build<::interfaces::msg::MotorCmd>()
{
  return interfaces::msg::builder::Init_MotorCmd_left_front();
}

}  // namespace interfaces

#endif  // INTERFACES__MSG__DETAIL__MOTOR_CMD__BUILDER_HPP_
