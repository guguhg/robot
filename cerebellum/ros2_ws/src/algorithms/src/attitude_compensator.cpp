#include "algorithms/attitude_compensator.hpp"
#include <algorithm>
#include <cmath>
#include "common/logger/logger.hpp"

namespace algorithms {

/**
 * @brief 构造函数，重置状态
 */
AttitudeCompensator::AttitudeCompensator()
{
    reset();
}

/**
 * @brief 重置状态
 */
void AttitudeCompensator::reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    last_quat_ = Eigen::Quaternionf::Identity();
}

/**
 * @brief 死区处理：小于阈值的信号直接归零
 */
float AttitudeCompensator::deadband(float value, float threshold)
{
    return (std::abs(value) < threshold) ? 0.0f : value;
}

/**
 * @brief 四元数 → 旋转矩阵
 */
Eigen::Matrix3f AttitudeCompensator::quaternionToRotationMatrix(const Eigen::Quaternionf& q)
{
    return q.toRotationMatrix();
}

/**
 * @brief 将世界坐标系速度转换到机器人坐标系
 * 
 * 世界说：向世界正前方前进
 * IMU说：我现在向右偏了30°，需要先回正
 * 
 * R: IMU坐标系 → 世界坐标系
 * R.transpose(): 世界坐标系 → IMU坐标系 (R⁻¹)
 */
Eigen::Vector3f AttitudeCompensator::worldToRobot(const Eigen::Vector3f& world_vel,
                                                   const Eigen::Matrix3f& rot)
{
    return rot.transpose() * world_vel;
}

/**
* @brief 角度死区的平滑过渡，防止跳变
* 
* @param angle 角度
* @param deadband 死区
* @return float 平滑后的角度
*/
float AttitudeCompensator::applyAngleDeadband(float angle, float deadband)
{
    float abs_angle = std::abs(angle);
    
    if (abs_angle <= deadband) {
        return 0.0f;  // 死区内归零
    }
    
    if (abs_angle <= deadband * 2.0f) {
        // 过渡区：死区~2倍死区之间平滑切入
        float factor = (abs_angle - deadband) / deadband;
        return angle * factor;
    }
    
    return angle;  // 完全生效
}

/**
 * @brief 计算重力补偿量（速度相关补偿 + 角度死区）
 * 
 * 小车在坡道上时，重力在前后/左右方向产生分量：
 * - pitch > 0 (头朝下)：重力向前拉 → 补偿向后
 * - roll > 0 (右侧抬起)：重力向右拉 → 补偿向左
 * 
 * 补偿量 = -sin(角度) × 强度 × 速度因子
 * 
 * 速度因子 = clamp(速度 / 阈值, 0.0, 1.0)
 * - 静止时：速度因子 = 0 → 不补偿 
 * - 低速时：速度因子 < 1 → 弱补偿
 * - 高速时：速度因子 = 1 → 满补偿
 * 
 * 角度死区：小于 angle_deadband 的倾斜不补偿
 * - 避免小障碍物（< 2°）误触发补偿
 */
void AttitudeCompensator::computeGravityCompensation(float pitch, float roll,
                                                      float cmd_vx, float cmd_vy,
                                                      float& comp_x, float& comp_y)
{
    comp_x = 0.0f;
    comp_y = 0.0f;

    // 1. 角度死区：小于死区角度不补偿
    //LOG_INFO("orin pitch:%f    orin roll:%f\n", pitch, roll);

    pitch = applyAngleDeadband(pitch, angle_deadband_);
    roll = applyAngleDeadband(roll, angle_deadband_);

    //LOG_INFO("pitch:%f    roll:%f\n", pitch, roll);

    // 如果角度都为零，不补偿
    if (std::abs(pitch) < 0.0001f && std::abs(roll) < 0.0001f) {
        return;
    }

    // 2. 计算当前速度大小
    float speed = std::sqrt(cmd_vx * cmd_vx + cmd_vy * cmd_vy);

    // 3. 速度因子：速度低于阈值时不补偿
    float speed_factor = 1.0f;
    if (speed_threshold_ > 0.0f) {
        speed_factor = std::clamp(speed / speed_threshold_, 0.0f, 1.0f);
    }

    // 如果速度因子为 0，不补偿（静止状态）
    if (speed_factor < 0.001f) {
        return;
    }

    // 4. 计算重力补偿量（随速度变化）
    // 俯仰补偿：小车前后倾斜时，重力在X方向的分量
    // 俯仰补偿
    comp_x = -std::sin(pitch) * gravity_compensation_ * speed_factor;

    // 翻滚补偿：小车左右倾斜时，重力在Y方向的分量
    comp_y = std::sin(roll) * gravity_compensation_ * speed_factor;

    // 5. 如果角度很小，不补偿 (二次保护)
    if (std::abs(pitch) < 0.017f) {  // < 1°
        comp_x = 0.0f;
    }
    if (std::abs(roll) < 0.017f) {   // < 1°
        comp_y = 0.0f;
    }
}

/**
 * @brief 核心处理接口
 * 
 * 1. 四元数 → 旋转矩阵 → 提取欧拉角 (pitch, roll)
 * 2. 世界坐标系速度 → 机器人坐标系速度
 * 3. 重力补偿 (角度死区 + 速度相关补偿)
 * 4. 死区处理
 */
AttitudeCompensatedResult AttitudeCompensator::process(
    float cmd_linear_x,
    float cmd_linear_y,
    float cmd_angular_z,
    const Eigen::Quaternionf& quaternion)
{
    std::lock_guard<std::mutex> lock(mutex_);

    AttitudeCompensatedResult result;
    result.has_imu = false;

    last_quat_ = quaternion;

    // 如果补偿未启用，直接透传
    if (!compensation_enabled_) {
        result.valid = true;
        result.linear_x = cmd_linear_x;
        result.linear_y = cmd_linear_y;
        result.angular_z = cmd_angular_z;
        result.has_imu = false;
        result.pitch_compensation = 0.0f;
        result.roll_compensation = 0.0f;
        return result;
    }

    // 四元数 → 旋转矩阵
    Eigen::Matrix3f rot = quaternionToRotationMatrix(quaternion);

    // 提取欧拉角 (Z-Y-X顺序) - ENU 坐标系 (ROS2 标准)
    // pitch: 绕 Y 轴 (前后倾斜)  抬头为正
    // roll:  绕 X 轴 (左右倾斜)  左侧抬起为正
    float pitch = std::asin(std::clamp(
        2.0f * (quaternion.w() * quaternion.y() - quaternion.z() * quaternion.x()),
        -1.0f, 1.0f));
    float roll = std::atan2(
        2.0f * (quaternion.w() * quaternion.x() + quaternion.y() * quaternion.z()),
        1.0f - 2.0f * (quaternion.x() * quaternion.x() + quaternion.y() * quaternion.y()));
    
    // 世界坐标系速度 → 机器人坐标系速度
    Eigen::Vector3f world_vel(cmd_linear_x, cmd_linear_y, cmd_angular_z);
    Eigen::Vector3f robot_vel = worldToRobot(world_vel, rot);

    // 计算重力补偿量（角度死区 + 速度相关补偿）
    float comp_x = 0.0f, comp_y = 0.0f;
    computeGravityCompensation(pitch, roll, robot_vel.x(), robot_vel.y(), comp_x, comp_y);

    // 应用补偿
    float compensated_linear_x = robot_vel.x() + comp_x;
    float compensated_linear_y = robot_vel.y() + comp_y;
    float compensated_angular_z = robot_vel.z();

    // 死区处理（消除 IMU 噪声导致的微动）
    compensated_linear_x = deadband(compensated_linear_x);
    compensated_linear_y = deadband(compensated_linear_y);
    compensated_angular_z = deadband(compensated_angular_z);

    // 填充结果
    result.valid = true;
    result.linear_x = compensated_linear_x;
    result.linear_y = compensated_linear_y;
    result.angular_z = compensated_angular_z;
    result.has_imu = true;
    result.pitch_compensation = comp_x;
    result.roll_compensation = comp_y;

    return result;
}

/**
 * @brief 向量版本
 */
AttitudeCompensatedResult AttitudeCompensator::process(
    const Eigen::Vector3f& cmd,
    const Eigen::Quaternionf& quaternion)
{
    return process(cmd.x(), cmd.y(), cmd.z(), quaternion);
}

/**
 * @brief 获取最近一次使用的 IMU 四元数
 */
Eigen::Quaternionf AttitudeCompensator::getLastImuQuaternion() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return last_quat_;
}

}  // namespace algorithms