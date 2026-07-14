#include "algorithms/twist_handler.hpp"
#include <algorithm>
#include <cmath>
#include <cfloat>

namespace algorithms {

/**
 * @brief 构造函数，重置状态
 */
TwistHandler::TwistHandler()
{
    reset();
}

/**
 * @brief 设置速度限制
 */
void TwistHandler::setLimits(float max_linear, float max_angular, float max_linear_y)
{
    std::lock_guard<std::mutex> lock(mutex_);
    limits_.max_linear_x = std::abs(max_linear);
    limits_.max_angular_z = std::abs(max_angular);
    limits_.max_linear_y = (max_linear_y > 0.0f) ? std::abs(max_linear_y) 
                                                   : limits_.max_linear_x * 0.8f;
}

/**
 * @brief 设置加速度/减速度限制
 */
void TwistHandler::setAccelLimits(float max_accel, float max_decel)
{
    std::lock_guard<std::mutex> lock(mutex_);
    limits_.max_accel = std::abs(max_accel);
    limits_.max_decel = (max_decel > 0.0f) ? std::abs(max_decel) : limits_.max_accel;
}

/**
 * @brief 设置角加速度限制
 */
void TwistHandler::setAngularAccelLimit(float max_angular_accel)
{
    std::lock_guard<std::mutex> lock(mutex_);
    limits_.max_angular_accel = std::abs(max_angular_accel);
}

/**
 * @brief 重置状态（清除平滑滤波器状态）
 */
void TwistHandler::reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    current_.linear_x = 0.0f;
    current_.linear_y = 0.0f;
    current_.angular_z = 0.0f;
    current_.last_update = std::chrono::steady_clock::now();
    initialized_ = false;
}

// ============ 工具函数 ============

/**
 * @brief 限幅
 */
float TwistHandler::clamp(float value, float min_val, float max_val)
{
    return std::max(min_val, std::min(max_val, value));
}

/**
 * @brief 死区处理：小于阈值的信号直接归零
 * 消除小信号抖动，防止小车在"静止"时因为微小的噪声而微微抖动。
 */
float TwistHandler::deadband(float value, float threshold)
{
    return (std::abs(value) < threshold) ? 0.0f : value;
}

/**
 * @brief 线性平滑（加速度/减速度限制）
 * 
 * 核心思想：限制速度每一步的变化量，让速度变化更平滑。
 * 加速用 max_accel，减速用 max_decel，可以分别控制。
 */
float TwistHandler::smoothLinear(float target, float current, float dt,
                                  float max_accel, float max_decel)
{
    float diff = target - current;      // 速度差（还需要改变多少）
    float max_change;                    // 这一步最多能改变多少

    if (std::abs(diff) < 1e-6f || dt < 1e-6f) {
        return target;
    }

    // 根据方向选择加速度或减速度限制
    if (diff > 0.0f) {
        max_change = max_accel * dt;     // 加速
    } else {
        max_change = max_decel * dt;     // 减速
    }

    if (std::abs(diff) <= max_change) {
        return target;                   // 一步就能到目标
    } else {
        return current + (diff > 0.0f ? max_change : -max_change);
    }
}

/**
 * @brief 角速度平滑
 */
float TwistHandler::smoothAngular(float target, float current, float dt,
                                   float max_angular_accel)
{
    float diff = target - current;
    float max_change = max_angular_accel * dt;

    if (std::abs(diff) < 1e-6f || dt < 1e-6f) {
        return target;
    }

    if (std::abs(diff) <= max_change) {
        return target;
    } else {
        return current + (diff > 0.0f ? max_change : -max_change);
    }
}

/**
 * @brief 核心处理接口
 * 
 * 1. 死区处理（消除微小抖动）
 * 2. 速度限幅（保护电机）
 * 3. 加速度/减速度平滑（防止突变）
 */
TwistResult TwistHandler::process(float cmd_linear_x,
                                   float cmd_linear_y,
                                   float cmd_angular_z,
                                   float dt)
{
    std::lock_guard<std::mutex> lock(mutex_);

    TwistResult result;

    // 1. 参数检查
    if (dt < 0.001f) {
        dt = 0.001f;
    }
    if (dt > 0.1f) {
        dt = 0.1f;
    }

    // 2. 死区处理（消除微小抖动）
    cmd_linear_x = deadband(cmd_linear_x);
    cmd_linear_y = deadband(cmd_linear_y);
    cmd_angular_z = deadband(cmd_angular_z);

    // 3. 速度限幅
    float limited_linear_x = clamp(cmd_linear_x, -limits_.max_linear_x, limits_.max_linear_x);
    float limited_linear_y = clamp(cmd_linear_y, -limits_.max_linear_y, limits_.max_linear_y);
    float limited_angular_z = clamp(cmd_angular_z, -limits_.max_angular_z, limits_.max_angular_z);

    bool was_limited = (limited_linear_x != cmd_linear_x) ||
                       (limited_linear_y != cmd_linear_y) ||
                       (limited_angular_z != cmd_angular_z);

    // 4. 初始化：第一次直接使用限幅后的值
    if (!initialized_) {
        current_.linear_x = limited_linear_x;
        current_.linear_y = limited_linear_y;
        current_.angular_z = limited_angular_z;
        current_.last_update = std::chrono::steady_clock::now();
        initialized_ = true;
        
        result.linear_x = limited_linear_x;
        result.linear_y = limited_linear_y;
        result.angular_z = limited_angular_z;
        result.limited = was_limited;
        result.smoothed = false;
        result.valid = true;
        return result;
    }

    // 5. 计算时间增量（用于平滑）
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::duration<float>>(
        now - current_.last_update).count();// 秒
    
    float actual_dt = std::max(dt, elapsed);
    actual_dt = std::min(actual_dt, 0.05f);  // 限制最大步长

    // 6. 加速度/减速度平滑
    float smoothed_linear_x = smoothLinear(
        limited_linear_x, 
        current_.linear_x, 
        actual_dt,
        limits_.max_accel, 
        limits_.max_decel);

    float smoothed_linear_y = smoothLinear(
        limited_linear_y, 
        current_.linear_y, 
        actual_dt,
        limits_.max_accel, 
        limits_.max_decel);

    float smoothed_angular_z = smoothAngular(
        limited_angular_z,
        current_.angular_z,
        actual_dt,
        limits_.max_angular_accel);

    bool was_smoothed = (std::abs(smoothed_linear_x - limited_linear_x) > 0.0001f) ||
                        (std::abs(smoothed_linear_y - limited_linear_y) > 0.0001f) ||
                        (std::abs(smoothed_angular_z - limited_angular_z) > 0.0001f);

    // 7. 更新状态
    current_.linear_x = smoothed_linear_x;
    current_.linear_y = smoothed_linear_y;
    current_.angular_z = smoothed_angular_z;
    current_.last_update = now;

    // 8. 填充结果
    result.linear_x = smoothed_linear_x;
    result.linear_y = smoothed_linear_y;
    result.angular_z = smoothed_angular_z;
    result.limited = was_limited;
    result.smoothed = was_smoothed;
    result.valid = true;

    return result;
}

/**
 * @brief 向量版本
 */
TwistResult TwistHandler::process(const Eigen::Vector3f& cmd, float dt)
{
    return process(cmd.x(), cmd.y(), cmd.z(), dt);
}

// ============ 状态查询 ============

Eigen::Vector3f TwistHandler::getCurrentOutput() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return Eigen::Vector3f(current_.linear_x, current_.linear_y, current_.angular_z);
}

bool TwistHandler::isMoving() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    static const float MOVING_THRESHOLD = 0.01f;
    return (std::abs(current_.linear_x) > MOVING_THRESHOLD) ||
           (std::abs(current_.linear_y) > MOVING_THRESHOLD) ||
           (std::abs(current_.angular_z) > MOVING_THRESHOLD * 0.5f);
}

bool TwistHandler::isAtMaxSpeed() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return (std::abs(current_.linear_x) >= limits_.max_linear_x * 0.95f) ||
           (std::abs(current_.linear_y) >= limits_.max_linear_y * 0.95f) ||
           (std::abs(current_.angular_z) >= limits_.max_angular_z * 0.95f);
}

}  // namespace algorithms