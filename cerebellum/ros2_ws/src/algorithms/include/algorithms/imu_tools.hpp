#pragma once

#include <Eigen/Dense>
#include <Eigen/Geometry>
#include <cmath>
#include <mutex>
#include <atomic>

namespace algorithms {

/**
 * @brief IMU 姿态数据
 */
struct IMUAttitude {
    Eigen::Quaternionf quaternion;   // 姿态四元数
    Eigen::Vector3f euler;           // 欧拉角 (roll, pitch, yaw) 弧度
    Eigen::Matrix3f rotation_matrix; // 旋转矩阵
    Eigen::Vector3f gravity;         // 重力方向向量 (世界坐标系)
    double timestamp;                // 时间戳
    bool valid = false;
};

/**
 * @brief IMU 工具类
 * 
 * 将 IMU 原始数据（加速度 + 角速度）转换为：
 * - 姿态四元数
 * - 欧拉角
 * - 旋转矩阵
 * 
 * 使用互补滤波融合加速度计和陀螺仪数据
 */
class IMUTools {
public:
    IMUTools();
    ~IMUTools() = default;

    /**
     * @brief 更新 IMU 数据并计算姿态
     * @param ax, ay, az 加速度 (m/s²)
     * @param gx, gy, gz 角速度 (rad/s)
     * @param dt 时间增量 (秒)
     */
    void update(float ax, float ay, float az,
                float gx, float gy, float gz,
                float dt);

    /**
     * @brief 获取当前姿态
     */
    IMUAttitude getAttitude() const;

    /**
     * @brief 获取四元数
     */
    Eigen::Quaternionf getQuaternion() const;

    /**
     * @brief 获取欧拉角 (roll, pitch, yaw) 弧度
     */
    Eigen::Vector3f getEuler() const;

    /**
     * @brief 获取旋转矩阵
     */
    Eigen::Matrix3f getRotationMatrix() const;

    /**
     * @brief 获取重力方向向量 (世界坐标系)
     */
    Eigen::Vector3f getGravity() const;

    /**
     * @brief 获取补偿后的陀螺仪数据（已减去零偏）
     */
    Eigen::Vector3f getCompensatedGyro() const;

    /**
     * @brief 重置姿态
     */
    void reset();

    /**
     * @brief 设置互补滤波系数 (0~1, 越大越信任陀螺仪)
     */
    void setFilterCoefficient(float alpha) { alpha_ = alpha; }

    // ============ 零漂校准接口 ============
    
    /**
     * @brief 获取陀螺仪零偏
     */
    Eigen::Vector3f getGyroBias() const;

    /**
     * @brief 获取零偏校准状态
     */
    bool isBiasCalibrated() const { return bias_calibrated_.load(); }

    /**
     * @brief 重置零偏校准（清除校准值）
     */
    void resetBiasCalibration();

    /**
     * @brief 设置预置零偏值（从配置文件加载）
     * @param bias 零偏值 (rad/s)
     */
    void setPresetBias(const Eigen::Vector3f& bias);

    /**
     * @brief 是否启用零偏补偿
     */
    void setBiasCompensationEnabled(bool enabled) { bias_compensation_enabled_ = enabled; }

private:
    // 姿态
    Eigen::Quaternionf quat_;        // 当前姿态四元数
    Eigen::Vector3f gravity_;        // 重力方向
    bool initialized_ = false;

    // 互补滤波参数
    float alpha_ = 0.98f;  // 陀螺仪权重（0.98 = 98% 信任陀螺仪）

    // 互斥锁（线程安全）
    mutable std::mutex mutex_;

    // ============ 零漂相关 ============
    Eigen::Vector3f gyro_bias_ = Eigen::Vector3f::Zero();          // 陀螺仪零偏
    Eigen::Vector3f compensated_gyro_ = Eigen::Vector3f::Zero();   // 补偿后的陀螺仪数据
    std::atomic<bool> bias_calibrated_{false};                     // 是否已校准
    std::atomic<bool> bias_compensation_enabled_{true};            // 是否启用零偏补偿

    // 内部方法
    Eigen::Quaternionf gyroUpdate(const Eigen::Quaternionf& q,
                                  const Eigen::Vector3f& gyro,
                                  float dt);
    Eigen::Quaternionf accelUpdate(const Eigen::Vector3f& accel);
    Eigen::Quaternionf complementaryFilter(const Eigen::Quaternionf& q_gyro,
                                           const Eigen::Quaternionf& q_accel);
    Eigen::Vector3f quaternionToEuler(const Eigen::Quaternionf& q) const;
    Eigen::Matrix3f quaternionToRotationMatrix(const Eigen::Quaternionf& q) const;
};

}  // namespace algorithms