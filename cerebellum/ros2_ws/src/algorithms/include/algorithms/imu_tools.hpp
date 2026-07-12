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
 * 
 * 零点校准说明：
 *   小车静止时，四元数表示的是 IMU 相对于世界坐标系的绝对姿态。
 *   如果 IMU 安装倾斜，四元数会有一个固定的初始偏移。
 *   零点校准就是把静止时的姿态作为参考零点，后续只输出相对于这个零点的变化量。
 *   这样即使 IMU 安装倾斜，姿态补偿也能正常工作。
 * 
 *   类比：你从北京出发，往南走100公里到了保定。
 *   增量（往南走100公里）是对的，但绝对位置在北京还是上海取决于起点。
 *   零点校准就是把起点设为北京，这样增量才能正确反映运动。
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
     * @brief 获取当前姿态（绝对姿态，未经过零点校准）
     */
    IMUAttitude getAttitude() const;

    /**
     * @brief 获取四元数（绝对姿态）
     */
    Eigen::Quaternionf getQuaternion() const;

    /**
     * @brief 获取欧拉角（绝对姿态）
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

    // ============ 零点校准配置接口 ============
    
    /**
     * @brief 设置零点校准参数
     * @param enabled 是否启用
     * @param threshold 静止检测阈值 (rad/s)
     * @param samples 连续静止采样次数
     */
    void setZeroPointConfig(bool enabled, float threshold, int samples);

    /**
     * @brief 获取零点校准是否启用
     */
    bool isZeroPointEnabled() const { return zero_point_enabled_; }

    // ============ 零点校准输出接口 ============
    
    /**
     * @brief 获取相对初始姿态的四元数（已减去初始偏置）
     *        如果未校准，返回单位四元数
     */
    Eigen::Quaternionf getRelativeQuaternion() const;

    /**
     * @brief 获取相对初始姿态的欧拉角（已减去初始偏置）
     *        如果未校准，返回 (0, 0, 0)
     */
    Eigen::Vector3f getRelativeEuler() const;

    /**
     * @brief 获取零点校准是否完成
     */
    bool isZeroPointCalibrated() const { return initial_quat_set_; }

private:
    // ======================== 姿态 ========================
    Eigen::Quaternionf quat_;        // 当前姿态四元数（绝对姿态）
    Eigen::Vector3f gravity_;        // 重力方向
    bool initialized_ = false;

    // 互补滤波参数
    float alpha_ = 0.98f;  // 陀螺仪权重（0.98 = 98% 信任陀螺仪）

    // ======================== 互斥锁 ========================
    mutable std::mutex mutex_;

    // ======================== 零漂相关 ========================
    Eigen::Vector3f gyro_bias_ = Eigen::Vector3f::Zero();          // 陀螺仪零偏
    Eigen::Vector3f compensated_gyro_ = Eigen::Vector3f::Zero();   // 补偿后的陀螺仪数据
    std::atomic<bool> bias_calibrated_{false};                     // 是否已校准
    std::atomic<bool> bias_compensation_enabled_{true};            // 是否启用零偏补偿

    // ======================== 零点校准相关 ========================
    // 零点校准的原理：
    //   小车第一次静止时，记录当前四元数作为参考零点 (initial_quat_)
    //   后续输出的四元数 = initial_quat_.inverse() * quat_
    //   这样静止时输出 (1,0,0,0)，pitch=0, roll=0
    //   只有小车真正运动时，四元数才会变化，姿态补偿才能正确工作
    
    // 零点校准配置
    bool zero_point_enabled_ = true;                    // 是否启用零点校准
    float stationary_threshold_ = 0.02f;                // 静止检测阈值 (rad/s)
    int required_stationary_samples_ = 50;              // 需要连续静止的采样次数
    
    // 零点校准状态
    Eigen::Quaternionf initial_quat_ = Eigen::Quaternionf::Identity();  // 初始姿态（静止时记录）
    bool initial_quat_set_ = false;                                     // 是否已记录初始姿态
    int stationary_counter_ = 0;                                        // 静止计数器（连续静止采样次数）
    bool is_stationary_ = false;                                        // 当前是否处于静止状态

    // ======================== 内部方法 ========================
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