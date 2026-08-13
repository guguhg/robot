// NOLINT: This file starts with a BOM since it contain non-ASCII characters
// generated from rosidl_generator_c/resource/idl__struct.h.em
// with input from interfaces:msg/MotorStates.idl
// generated code does not contain a copyright notice

#ifndef INTERFACES__MSG__DETAIL__MOTOR_STATES__STRUCT_H_
#define INTERFACES__MSG__DETAIL__MOTOR_STATES__STRUCT_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


// Constants defined in the message

/// Struct defined in msg/MotorStates in the package interfaces.
/**
  * 四轮电机速度反馈 (r/s)
 */
typedef struct interfaces__msg__MotorStates
{
  float left_front;
  float right_front;
  float left_rear;
  float right_rear;
} interfaces__msg__MotorStates;

// Struct for a sequence of interfaces__msg__MotorStates.
typedef struct interfaces__msg__MotorStates__Sequence
{
  interfaces__msg__MotorStates * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} interfaces__msg__MotorStates__Sequence;

#ifdef __cplusplus
}
#endif

#endif  // INTERFACES__MSG__DETAIL__MOTOR_STATES__STRUCT_H_
