#pragma once
#include "drivers/controller_board/controller_board.h"

// ============================================
// 驱动层移植接口
// 所有硬件相关操作通过这里进行宏封装
// 更换硬件时只需修改这个文件
// ============================================

/**
 * @brief return bool:操作是否成功  id:电机id  speed:目标速度rs
 * 
 */
#define MOTOR_CTRL(id, speed)       drivers::ControllerBoard::motorCtrl(id, speed)

/**
 * @brief return bool:操作是否成功  map:字典  id:rs
 * 
 */
#define MOTOR_CTRL_MAP(map)         drivers::ControllerBoard::motorCtrl(map)

/**
 * @brief return bool:操作是否成功  id:电机id
 * 
 */
#define MOTOR_STOP(id)              drivers::ControllerBoard::motorStop(id)

/**
 * @brief return bool:操作是否成功  list:vector<uint8_t>电机id列表
 * 
 */
#define MOTOR_STOP_LIST(list)       drivers::ControllerBoard::motorStop(list)

/**
 * @brief return bool:操作是否成功  id:电机id &speed:速度rs
 * 
 */
//#define MOTOR_GET_SPEED(id, speed)  drivers::ControllerBoard::motorSpeedGet(id, speed)

/**
 * @brief return bool:操作是否成功  &map:字典 id:rs
 * 
 */
//#define MOTOR_GET_SPEED_MAP(map)    drivers::ControllerBoard::motorSpeedGet(map)  

/**
 * @brief return bool:操作是否成功  ax, ay, az, gx, gy, gz:物理量,不是ADC
 * 
 */
#define IMU_DATA_GET(ax, ay, az, gx, gy, gz) drivers::ControllerBoard::imuDataGet(ax, ay, az, gx, gy, gz)

/**
 * @brief return bool:操作是否成功  mv:毫伏
 * 
 */
#define VOLTAGE_MV_GET(mv)       drivers::ControllerBoard::voltageGet(mv)

