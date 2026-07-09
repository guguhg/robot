// generated from rosidl_generator_cpp/resource/idl__struct.hpp.em
// with input from interfaces:msg/MotorCmd.idl
// generated code does not contain a copyright notice

#ifndef INTERFACES__MSG__DETAIL__MOTOR_CMD__STRUCT_HPP_
#define INTERFACES__MSG__DETAIL__MOTOR_CMD__STRUCT_HPP_

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "rosidl_runtime_cpp/bounded_vector.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


#ifndef _WIN32
# define DEPRECATED__interfaces__msg__MotorCmd __attribute__((deprecated))
#else
# define DEPRECATED__interfaces__msg__MotorCmd __declspec(deprecated)
#endif

namespace interfaces
{

namespace msg
{

// message struct
template<class ContainerAllocator>
struct MotorCmd_
{
  using Type = MotorCmd_<ContainerAllocator>;

  explicit MotorCmd_(rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  {
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->left_front = 0.0f;
      this->right_front = 0.0f;
      this->left_rear = 0.0f;
      this->right_rear = 0.0f;
    }
  }

  explicit MotorCmd_(const ContainerAllocator & _alloc, rosidl_runtime_cpp::MessageInitialization _init = rosidl_runtime_cpp::MessageInitialization::ALL)
  {
    (void)_alloc;
    if (rosidl_runtime_cpp::MessageInitialization::ALL == _init ||
      rosidl_runtime_cpp::MessageInitialization::ZERO == _init)
    {
      this->left_front = 0.0f;
      this->right_front = 0.0f;
      this->left_rear = 0.0f;
      this->right_rear = 0.0f;
    }
  }

  // field types and members
  using _left_front_type =
    float;
  _left_front_type left_front;
  using _right_front_type =
    float;
  _right_front_type right_front;
  using _left_rear_type =
    float;
  _left_rear_type left_rear;
  using _right_rear_type =
    float;
  _right_rear_type right_rear;

  // setters for named parameter idiom
  Type & set__left_front(
    const float & _arg)
  {
    this->left_front = _arg;
    return *this;
  }
  Type & set__right_front(
    const float & _arg)
  {
    this->right_front = _arg;
    return *this;
  }
  Type & set__left_rear(
    const float & _arg)
  {
    this->left_rear = _arg;
    return *this;
  }
  Type & set__right_rear(
    const float & _arg)
  {
    this->right_rear = _arg;
    return *this;
  }

  // constant declarations

  // pointer types
  using RawPtr =
    interfaces::msg::MotorCmd_<ContainerAllocator> *;
  using ConstRawPtr =
    const interfaces::msg::MotorCmd_<ContainerAllocator> *;
  using SharedPtr =
    std::shared_ptr<interfaces::msg::MotorCmd_<ContainerAllocator>>;
  using ConstSharedPtr =
    std::shared_ptr<interfaces::msg::MotorCmd_<ContainerAllocator> const>;

  template<typename Deleter = std::default_delete<
      interfaces::msg::MotorCmd_<ContainerAllocator>>>
  using UniquePtrWithDeleter =
    std::unique_ptr<interfaces::msg::MotorCmd_<ContainerAllocator>, Deleter>;

  using UniquePtr = UniquePtrWithDeleter<>;

  template<typename Deleter = std::default_delete<
      interfaces::msg::MotorCmd_<ContainerAllocator>>>
  using ConstUniquePtrWithDeleter =
    std::unique_ptr<interfaces::msg::MotorCmd_<ContainerAllocator> const, Deleter>;
  using ConstUniquePtr = ConstUniquePtrWithDeleter<>;

  using WeakPtr =
    std::weak_ptr<interfaces::msg::MotorCmd_<ContainerAllocator>>;
  using ConstWeakPtr =
    std::weak_ptr<interfaces::msg::MotorCmd_<ContainerAllocator> const>;

  // pointer types similar to ROS 1, use SharedPtr / ConstSharedPtr instead
  // NOTE: Can't use 'using' here because GNU C++ can't parse attributes properly
  typedef DEPRECATED__interfaces__msg__MotorCmd
    std::shared_ptr<interfaces::msg::MotorCmd_<ContainerAllocator>>
    Ptr;
  typedef DEPRECATED__interfaces__msg__MotorCmd
    std::shared_ptr<interfaces::msg::MotorCmd_<ContainerAllocator> const>
    ConstPtr;

  // comparison operators
  bool operator==(const MotorCmd_ & other) const
  {
    if (this->left_front != other.left_front) {
      return false;
    }
    if (this->right_front != other.right_front) {
      return false;
    }
    if (this->left_rear != other.left_rear) {
      return false;
    }
    if (this->right_rear != other.right_rear) {
      return false;
    }
    return true;
  }
  bool operator!=(const MotorCmd_ & other) const
  {
    return !this->operator==(other);
  }
};  // struct MotorCmd_

// alias to use template instance with default allocator
using MotorCmd =
  interfaces::msg::MotorCmd_<std::allocator<void>>;

// constant definitions

}  // namespace msg

}  // namespace interfaces

#endif  // INTERFACES__MSG__DETAIL__MOTOR_CMD__STRUCT_HPP_
