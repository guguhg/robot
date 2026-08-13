// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from interfaces:msg/MotorStates.idl
// generated code does not contain a copyright notice

#ifndef INTERFACES__MSG__DETAIL__MOTOR_STATES__BUILDER_HPP_
#define INTERFACES__MSG__DETAIL__MOTOR_STATES__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "interfaces/msg/detail/motor_states__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace interfaces
{

namespace msg
{

namespace builder
{

class Init_MotorStates_right_rear
{
public:
  explicit Init_MotorStates_right_rear(::interfaces::msg::MotorStates & msg)
  : msg_(msg)
  {}
  ::interfaces::msg::MotorStates right_rear(::interfaces::msg::MotorStates::_right_rear_type arg)
  {
    msg_.right_rear = std::move(arg);
    return std::move(msg_);
  }

private:
  ::interfaces::msg::MotorStates msg_;
};

class Init_MotorStates_left_rear
{
public:
  explicit Init_MotorStates_left_rear(::interfaces::msg::MotorStates & msg)
  : msg_(msg)
  {}
  Init_MotorStates_right_rear left_rear(::interfaces::msg::MotorStates::_left_rear_type arg)
  {
    msg_.left_rear = std::move(arg);
    return Init_MotorStates_right_rear(msg_);
  }

private:
  ::interfaces::msg::MotorStates msg_;
};

class Init_MotorStates_right_front
{
public:
  explicit Init_MotorStates_right_front(::interfaces::msg::MotorStates & msg)
  : msg_(msg)
  {}
  Init_MotorStates_left_rear right_front(::interfaces::msg::MotorStates::_right_front_type arg)
  {
    msg_.right_front = std::move(arg);
    return Init_MotorStates_left_rear(msg_);
  }

private:
  ::interfaces::msg::MotorStates msg_;
};

class Init_MotorStates_left_front
{
public:
  Init_MotorStates_left_front()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_MotorStates_right_front left_front(::interfaces::msg::MotorStates::_left_front_type arg)
  {
    msg_.left_front = std::move(arg);
    return Init_MotorStates_right_front(msg_);
  }

private:
  ::interfaces::msg::MotorStates msg_;
};

}  // namespace builder

}  // namespace msg

template<typename MessageType>
auto build();

template<>
inline
auto build<::interfaces::msg::MotorStates>()
{
  return interfaces::msg::builder::Init_MotorStates_left_front();
}

}  // namespace interfaces

#endif  // INTERFACES__MSG__DETAIL__MOTOR_STATES__BUILDER_HPP_
