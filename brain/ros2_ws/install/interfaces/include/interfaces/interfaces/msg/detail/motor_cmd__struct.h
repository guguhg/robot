// NOLINT: This file starts with a BOM since it contain non-ASCII characters
// generated from rosidl_generator_c/resource/idl__struct.h.em
// with input from interfaces:msg/MotorCmd.idl
// generated code does not contain a copyright notice

#ifndef INTERFACES__MSG__DETAIL__MOTOR_CMD__STRUCT_H_
#define INTERFACES__MSG__DETAIL__MOTOR_CMD__STRUCT_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


// Constants defined in the message

/// Struct defined in msg/MotorCmd in the package interfaces.
/**
  * 四轮电机控制指令 (r/s)
 */
typedef struct interfaces__msg__MotorCmd
{
  float left_front;
  float right_front;
  float left_rear;
  float right_rear;
} interfaces__msg__MotorCmd;

// Struct for a sequence of interfaces__msg__MotorCmd.
typedef struct interfaces__msg__MotorCmd__Sequence
{
  interfaces__msg__MotorCmd * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} interfaces__msg__MotorCmd__Sequence;

#ifdef __cplusplus
}
#endif

#endif  // INTERFACES__MSG__DETAIL__MOTOR_CMD__STRUCT_H_
