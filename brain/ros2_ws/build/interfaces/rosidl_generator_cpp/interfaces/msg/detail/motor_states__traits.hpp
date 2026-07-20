// generated from rosidl_generator_cpp/resource/idl__traits.hpp.em
// with input from interfaces:msg/MotorStates.idl
// generated code does not contain a copyright notice

#ifndef INTERFACES__MSG__DETAIL__MOTOR_STATES__TRAITS_HPP_
#define INTERFACES__MSG__DETAIL__MOTOR_STATES__TRAITS_HPP_

#include <stdint.h>

#include <sstream>
#include <string>
#include <type_traits>

#include "interfaces/msg/detail/motor_states__struct.hpp"
#include "rosidl_runtime_cpp/traits.hpp"

namespace interfaces
{

namespace msg
{

inline void to_flow_style_yaml(
  const MotorStates & msg,
  std::ostream & out)
{
  out << "{";
  // member: left_front
  {
    out << "left_front: ";
    rosidl_generator_traits::value_to_yaml(msg.left_front, out);
    out << ", ";
  }

  // member: right_front
  {
    out << "right_front: ";
    rosidl_generator_traits::value_to_yaml(msg.right_front, out);
    out << ", ";
  }

  // member: left_rear
  {
    out << "left_rear: ";
    rosidl_generator_traits::value_to_yaml(msg.left_rear, out);
    out << ", ";
  }

  // member: right_rear
  {
    out << "right_rear: ";
    rosidl_generator_traits::value_to_yaml(msg.right_rear, out);
  }
  out << "}";
}  // NOLINT(readability/fn_size)

inline void to_block_style_yaml(
  const MotorStates & msg,
  std::ostream & out, size_t indentation = 0)
{
  // member: left_front
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "left_front: ";
    rosidl_generator_traits::value_to_yaml(msg.left_front, out);
    out << "\n";
  }

  // member: right_front
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "right_front: ";
    rosidl_generator_traits::value_to_yaml(msg.right_front, out);
    out << "\n";
  }

  // member: left_rear
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "left_rear: ";
    rosidl_generator_traits::value_to_yaml(msg.left_rear, out);
    out << "\n";
  }

  // member: right_rear
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "right_rear: ";
    rosidl_generator_traits::value_to_yaml(msg.right_rear, out);
    out << "\n";
  }
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const MotorStates & msg, bool use_flow_style = false)
{
  std::ostringstream out;
  if (use_flow_style) {
    to_flow_style_yaml(msg, out);
  } else {
    to_block_style_yaml(msg, out);
  }
  return out.str();
}

}  // namespace msg

}  // namespace interfaces

namespace rosidl_generator_traits
{

[[deprecated("use interfaces::msg::to_block_style_yaml() instead")]]
inline void to_yaml(
  const interfaces::msg::MotorStates & msg,
  std::ostream & out, size_t indentation = 0)
{
  interfaces::msg::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use interfaces::msg::to_yaml() instead")]]
inline std::string to_yaml(const interfaces::msg::MotorStates & msg)
{
  return interfaces::msg::to_yaml(msg);
}

template<>
inline const char * data_type<interfaces::msg::MotorStates>()
{
  return "interfaces::msg::MotorStates";
}

template<>
inline const char * name<interfaces::msg::MotorStates>()
{
  return "interfaces/msg/MotorStates";
}

template<>
struct has_fixed_size<interfaces::msg::MotorStates>
  : std::integral_constant<bool, true> {};

template<>
struct has_bounded_size<interfaces::msg::MotorStates>
  : std::integral_constant<bool, true> {};

template<>
struct is_message<interfaces::msg::MotorStates>
  : std::true_type {};

}  // namespace rosidl_generator_traits

#endif  // INTERFACES__MSG__DETAIL__MOTOR_STATES__TRAITS_HPP_
