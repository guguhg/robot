#pragma once

#include <Eigen/Dense>
#include <Eigen/Geometry>
#include <cmath>
#include <mutex>
#include <chrono>

namespace algorithms {

/**
 * @brief Twist 处理结果
 */
struct TwistResult {
    bool valid = false;
    float linear_x = 0.0f;      // 前向速度 (m/s)
    float linear_y = 0.0f;      // 横向速度 (m/s)
    float angular_z = 0.0f;     // 角速度 (rad/s)
    bool limited = false;       // 是否被限幅
    bool smoothed = false;      // 是否被平滑
};

/**
 * @brief Twist 处理器
 * 
 * 负责：
 * 1. 速度限幅（最大线速度、最大角速度）
 * 2. 加速度/减速度平滑（防止突变）
 * 3. 输出处理后的速度指令
 * 
 * 使用方法:
 *   TwistHandler handler;
 *   handler.setLimits(max_linear, max_angular);
 *   handler.setAccelLimits(max_accel, max_decel);
 *   
 *   auto result = handler.process(cmd_linear_x, cmd_linear_y, cmd_angular_z, dt);
 */
class TwistHandler {
public:
    TwistHandler();
    ~TwistHandler() = default;

    // ============ 配置接口 ============
    
    /**
     * @brief 设置速度限制
     * @param max_linear  最大线速度 (m/s)
     * @param max_angular 最大角速度 (rad/s)
     * @param max_linear_y 最大横向速度 (m/s)，默认为 max_linear * 0.8
     */
    void setLimits(float max_linear, float max_angular, 
                   float max_linear_y = -1.0f);
    
    /**
     * @brief 设置加速度/减速度限制
     * @param max_accel  最大加速度 (m/s²)
     * @param max_decel  最大减速度 (m/s²)，默认为 max_accel
     */
    void setAccelLimits(float max_accel, float max_decel = -1.0f);
    
    /**
     * @brief 设置角加速度限制
     * @param max_angular_accel 最大角加速度 (rad/s²)
     */
    void setAngularAccelLimit(float max_angular_accel);

    /**
     * @brief 重置状态（清除平滑滤波器状态）
     */
    void reset();

    // ============ 核心处理接口 ============
    
    /**
     * @brief 处理速度指令
     * @param cmd_linear_x  目标前向速度 (m/s)
     * @param cmd_linear_y  目标横向速度 (m/s)
     * @param cmd_angular_z 目标角速度 (rad/s)
     * @param dt            时间步长 (秒)
     * @return 处理后的速度指令
     */
    TwistResult process(float cmd_linear_x, 
                        float cmd_linear_y,
                        float cmd_angular_z,
                        float dt);

    /**
     * @brief 处理速度指令 (Eigen 版本)
     * @param cmd  目标速度 (x: 前向, y: 横向, z: 角速度)
     * @param dt   时间步长 (秒)
     * @return 处理后的速度指令
     */
    TwistResult process(const Eigen::Vector3f& cmd, float dt);

    // ============ 状态查询 ============
    
    /**
     * @brief 获取当前输出速度（平滑后的值）
     */
    Eigen::Vector3f getCurrentOutput() const;

    /**
     * @brief 检查是否在运动
     */
    bool isMoving() const;

    /**
     * @brief 检查是否达到最大速度
     */
    bool isAtMaxSpeed() const;

private:
    // ============ 配置参数 ============
    struct Limits {
        float max_linear_x = 0.5f;      // 最大前向速度 (m/s)
        float max_linear_y = 0.4f;      // 最大横向速度 (m/s)
        float max_angular_z = 0.8f;     // 最大角速度 (rad/s)
        float max_accel = 1.0f;         // 最大加速度 (m/s²)
        float max_decel = 1.0f;         // 最大减速度 (m/s²)
        float max_angular_accel = 2.0f; // 最大角加速度 (rad/s²)
    } limits_;

    // ============ 状态 ============
    struct State {
        float linear_x = 0.0f;
        float linear_y = 0.0f;
        float angular_z = 0.0f;
        std::chrono::steady_clock::time_point last_update;
    } current_;

    // ============ 内部方法 ============
    float clamp(float value, float min_val, float max_val);
    float deadband(float value, float threshold = 0.001f);
    float smoothLinear(float target, float current, float dt, 
                       float max_accel, float max_decel);
    float smoothAngular(float target, float current, float dt, 
                        float max_angular_accel);

    mutable std::mutex mutex_;
    bool initialized_ = false;
};

}  // namespace algorithms