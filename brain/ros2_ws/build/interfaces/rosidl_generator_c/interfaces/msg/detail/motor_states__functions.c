// generated from rosidl_generator_c/resource/idl__functions.c.em
// with input from interfaces:msg/MotorStates.idl
// generated code does not contain a copyright notice
#include "interfaces/msg/detail/motor_states__functions.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "rcutils/allocator.h"


bool
interfaces__msg__MotorStates__init(interfaces__msg__MotorStates * msg)
{
  if (!msg) {
    return false;
  }
  // left_front
  // right_front
  // left_rear
  // right_rear
  return true;
}

void
interfaces__msg__MotorStates__fini(interfaces__msg__MotorStates * msg)
{
  if (!msg) {
    return;
  }
  // left_front
  // right_front
  // left_rear
  // right_rear
}

bool
interfaces__msg__MotorStates__are_equal(const interfaces__msg__MotorStates * lhs, const interfaces__msg__MotorStates * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  // left_front
  if (lhs->left_front != rhs->left_front) {
    return false;
  }
  // right_front
  if (lhs->right_front != rhs->right_front) {
    return false;
  }
  // left_rear
  if (lhs->left_rear != rhs->left_rear) {
    return false;
  }
  // right_rear
  if (lhs->right_rear != rhs->right_rear) {
    return false;
  }
  return true;
}

bool
interfaces__msg__MotorStates__copy(
  const interfaces__msg__MotorStates * input,
  interfaces__msg__MotorStates * output)
{
  if (!input || !output) {
    return false;
  }
  // left_front
  output->left_front = input->left_front;
  // right_front
  output->right_front = input->right_front;
  // left_rear
  output->left_rear = input->left_rear;
  // right_rear
  output->right_rear = input->right_rear;
  return true;
}

interfaces__msg__MotorStates *
interfaces__msg__MotorStates__create()
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  interfaces__msg__MotorStates * msg = (interfaces__msg__MotorStates *)allocator.allocate(sizeof(interfaces__msg__MotorStates), allocator.state);
  if (!msg) {
    return NULL;
  }
  memset(msg, 0, sizeof(interfaces__msg__MotorStates));
  bool success = interfaces__msg__MotorStates__init(msg);
  if (!success) {
    allocator.deallocate(msg, allocator.state);
    return NULL;
  }
  return msg;
}

void
interfaces__msg__MotorStates__destroy(interfaces__msg__MotorStates * msg)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (msg) {
    interfaces__msg__MotorStates__fini(msg);
  }
  allocator.deallocate(msg, allocator.state);
}


bool
interfaces__msg__MotorStates__Sequence__init(interfaces__msg__MotorStates__Sequence * array, size_t size)
{
  if (!array) {
    return false;
  }
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  interfaces__msg__MotorStates * data = NULL;

  if (size) {
    data = (interfaces__msg__MotorStates *)allocator.zero_allocate(size, sizeof(interfaces__msg__MotorStates), allocator.state);
    if (!data) {
      return false;
    }
    // initialize all array elements
    size_t i;
    for (i = 0; i < size; ++i) {
      bool success = interfaces__msg__MotorStates__init(&data[i]);
      if (!success) {
        break;
      }
    }
    if (i < size) {
      // if initialization failed finalize the already initialized array elements
      for (; i > 0; --i) {
        interfaces__msg__MotorStates__fini(&data[i - 1]);
      }
      allocator.deallocate(data, allocator.state);
      return false;
    }
  }
  array->data = data;
  array->size = size;
  array->capacity = size;
  return true;
}

void
interfaces__msg__MotorStates__Sequence__fini(interfaces__msg__MotorStates__Sequence * array)
{
  if (!array) {
    return;
  }
  rcutils_allocator_t allocator = rcutils_get_default_allocator();

  if (array->data) {
    // ensure that data and capacity values are consistent
    assert(array->capacity > 0);
    // finalize all array elements
    for (size_t i = 0; i < array->capacity; ++i) {
      interfaces__msg__MotorStates__fini(&array->data[i]);
    }
    allocator.deallocate(array->data, allocator.state);
    array->data = NULL;
    array->size = 0;
    array->capacity = 0;
  } else {
    // ensure that data, size, and capacity values are consistent
    assert(0 == array->size);
    assert(0 == array->capacity);
  }
}

interfaces__msg__MotorStates__Sequence *
interfaces__msg__MotorStates__Sequence__create(size_t size)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  interfaces__msg__MotorStates__Sequence * array = (interfaces__msg__MotorStates__Sequence *)allocator.allocate(sizeof(interfaces__msg__MotorStates__Sequence), allocator.state);
  if (!array) {
    return NULL;
  }
  bool success = interfaces__msg__MotorStates__Sequence__init(array, size);
  if (!success) {
    allocator.deallocate(array, allocator.state);
    return NULL;
  }
  return array;
}

void
interfaces__msg__MotorStates__Sequence__destroy(interfaces__msg__MotorStates__Sequence * array)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (array) {
    interfaces__msg__MotorStates__Sequence__fini(array);
  }
  allocator.deallocate(array, allocator.state);
}

bool
interfaces__msg__MotorStates__Sequence__are_equal(const interfaces__msg__MotorStates__Sequence * lhs, const interfaces__msg__MotorStates__Sequence * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  if (lhs->size != rhs->size) {
    return false;
  }
  for (size_t i = 0; i < lhs->size; ++i) {
    if (!interfaces__msg__MotorStates__are_equal(&(lhs->data[i]), &(rhs->data[i]))) {
      return false;
    }
  }
  return true;
}

bool
interfaces__msg__MotorStates__Sequence__copy(
  const interfaces__msg__MotorStates__Sequence * input,
  interfaces__msg__MotorStates__Sequence * output)
{
  if (!input || !output) {
    return false;
  }
  if (output->capacity < input->size) {
    const size_t allocation_size =
      input->size * sizeof(interfaces__msg__MotorStates);
    rcutils_allocator_t allocator = rcutils_get_default_allocator();
    interfaces__msg__MotorStates * data =
      (interfaces__msg__MotorStates *)allocator.reallocate(
      output->data, allocation_size, allocator.state);
    if (!data) {
      return false;
    }
    // If reallocation succeeded, memory may or may not have been moved
    // to fulfill the allocation request, invalidating output->data.
    output->data = data;
    for (size_t i = output->capacity; i < input->size; ++i) {
      if (!interfaces__msg__MotorStates__init(&output->data[i])) {
        // If initialization of any new item fails, roll back
        // all previously initialized items. Existing items
        // in output are to be left unmodified.
        for (; i-- > output->capacity; ) {
          interfaces__msg__MotorStates__fini(&output->data[i]);
        }
        return false;
      }
    }
    output->capacity = input->size;
  }
  output->size = input->size;
  for (size_t i = 0; i < input->size; ++i) {
    if (!interfaces__msg__MotorStates__copy(
        &(input->data[i]), &(output->data[i])))
    {
      return false;
    }
  }
  return true;
}
