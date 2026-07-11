#pragma once

#include <Eigen/Dense>
#include <Eigen/Geometry>
#include <mutex>
#include <cmath>

namespace algorithms {

/**
 * @brief 姿态补偿结果
 */
struct AttitudeCompensatedResult {
    bool valid = false;
    float linear_x = 0.0f;      // 补偿后的前向速度 (m/s)
    float linear_y = 0.0f;      // 补偿后的横向速度 (m/s)
    float angular_z = 0.0f;     // 补偿后的角速度 (rad/s)
    bool has_imu = false;       // 是否使用了 IMU 数据
    float pitch_compensation = 0.0f;  // 俯仰补偿量
    float roll_compensation = 0.0f;   // 翻滚补偿量
};

/**
 * @brief 姿态补偿器
 * 
 * 利用 IMU 姿态（四元数）对速度指令进行补偿：
 * 1. 将世界坐标系速度指令转换到机器人坐标系
 * 2. 考虑坡道/倾斜地面的重力补偿
 * 3. 防止小车在坡道上"溜坡"
 * 
 * 补偿原理：
 * - 俯仰补偿（前后方向）：上下坡自动修正速度，防止溜坡
 * - 翻滚补偿（左右方向）：车身倾斜时自动修正横向偏移
 * 
 * 使用方法:
 *   AttitudeCompensator compensator;
 *   compensator.setCompensationEnabled(true);
 *   compensator.setGravityCompensation(0.5f);
 *   
 *   auto result = compensator.process(cmd_linear_x, cmd_linear_y, cmd_angular_z, quaternion);
 */
class AttitudeCompensator {
public:
    AttitudeCompensator();
    ~AttitudeCompensator() = default;

    // ============ 配置接口 ============
    
    /**
     * @brief 启用/禁用姿态补偿
     */
    void setCompensationEnabled(bool enabled) { compensation_enabled_ = enabled; }

    /**
     * @brief 设置重力补偿强度 (0.0 ~ 1.0)
     * 0.0: 不补偿，1.0: 完全补偿
     */
    void setGravityCompensation(float strength) {
        gravity_compensation_ = std::clamp(strength, 0.0f, 1.0f);
    }

    /**
     * @brief 设置最大俯仰角度补偿范围 (弧度)
     * 超过此角度认为小车在坡道上，进行全补偿
     */
    void setMaxPitchAngle(float angle) { max_pitch_angle_ = std::abs(angle); }

    /**
     * @brief 设置最大翻滚角度补偿范围 (弧度)
     */
    void setMaxRollAngle(float angle) { max_roll_angle_ = std::abs(angle); }

    /**
     * @brief 重置状态
     */
    void reset();

    // ============ 核心处理接口 ============
    
    /**
     * @brief 处理速度指令，结合 IMU 姿态进行补偿
     * @param cmd_linear_x  目标前向速度 (m/s)
     * @param cmd_linear_y  目标横向速度 (m/s)
     * @param cmd_angular_z 目标角速度 (rad/s)
     * @param quaternion    IMU 姿态四元数 (从 /imu/data 获取)
     * @return 补偿后的速度指令
     */
    AttitudeCompensatedResult process(float cmd_linear_x,
                                       float cmd_linear_y,
                                       float cmd_angular_z,
                                       const Eigen::Quaternionf& quaternion);

    /**
     * @brief 处理速度指令 (Eigen 版本)
     */
    AttitudeCompensatedResult process(const Eigen::Vector3f& cmd,
                                       const Eigen::Quaternionf& quaternion);

    // ============ 状态查询 ============
    
    /**
     * @brief 获取当前姿态补偿是否启用
     */
    bool isCompensationEnabled() const { return compensation_enabled_; }

    /**
     * @brief 获取当前的 IMU 姿态 (最近一次处理时使用)
     */
    Eigen::Quaternionf getLastImuQuaternion() const;

    /**
     * @brief 获取当前补偿强度
     */
    float getGravityCompensation() const { return gravity_compensation_; }

private:
    // ============ 配置参数 ============
    bool compensation_enabled_ = true;
    float gravity_compensation_ = 0.5f;
    float max_pitch_angle_ = 0.523f;
    float max_roll_angle_ = 0.523f;

    // ============ 状态 ============
    Eigen::Quaternionf last_quat_ = Eigen::Quaternionf::Identity();
    mutable std::mutex mutex_;

    // ============ 内部方法 ============
    float deadband(float value, float threshold = 0.001f);
    Eigen::Matrix3f quaternionToRotationMatrix(const Eigen::Quaternionf& q);

    /**
     * @brief 将世界坐标系速度转换到机器人坐标系
     * 使用 IMU 旋转矩阵的逆 (R.transpose())
     */
    Eigen::Vector3f worldToRobot(const Eigen::Vector3f& world_vel,
                                  const Eigen::Matrix3f& rot);

    /**
     * @brief 计算重力补偿量
     * 根据俯仰/翻滚角度，补偿前进/横向速度
     */
    void computeGravityCompensation(float pitch, float roll,
                                     float& comp_x, float& comp_y);
};

}  // namespace algorithms