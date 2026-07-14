#pragma once

#include <Eigen/Dense>
#include <Eigen/Geometry>
#include <cmath>
#include <mutex>
#include <atomic>

namespace algorithms {

/**
 * @brief IMU 姿态数据结构体
 * 
 * 包含一次姿态解算的所有输出信息
 */
struct IMUAttitude {
    Eigen::Quaternionf quaternion;   // 姿态四元数 (w, x, y, z)
    Eigen::Vector3f euler;           // 欧拉角 (roll, pitch, yaw) 弧度
    Eigen::Matrix3f rotation_matrix; // 3x3 旋转矩阵
    Eigen::Vector3f gravity;         // 重力方向向量 (世界坐标系)
    double timestamp;                // 数据时间戳
    bool valid = false;              // 是否有效（IMU 已初始化）
};

/**
 * @brief IMU 数据处理工具类
 * 
 * 核心功能：
 *   1. 融合加速度计和陀螺仪数据，计算姿态四元数
 *   2. 零漂校准（消除陀螺仪静态偏置）
 *   3. 零点校准（消除 IMU 安装倾斜影响）
 * 
 * 算法原理：
 *   - 陀螺仪积分：短期准确，但会累积漂移
 *   - 加速度计：长期稳定，但有噪声和运动干扰
 *   - 互补滤波：取两者优点，alpha 控制信任比例
 * 
 * 校准说明：
 *   零漂校准：消除陀螺仪静态偏置，开机自动执行，也可手动触发
 *   零点校准：消除 IMU 安装倾斜，手动触发，也可配置为开机自动
 */
class IMUTools {
public:
    IMUTools();
    ~IMUTools() = default;

    /**
     * @brief 更新 IMU 数据并计算姿态
     * @param ax, ay, az 加速度 (m/s²)
     * @param gx, gy, gz 角速度 (rad/s)
     * @param dt         时间增量 (秒)
     * 
     * 处理流程：
     *   1. 归一化加速度（只取方向）
     *   2. 减去陀螺仪零偏（如已校准）
     *   3. 陀螺仪积分 → 短期姿态
     *   4. 加速度计估算 → 长期姿态
     *   5. 互补滤波融合 → 最终姿态
     *   6. 更新重力方向
     */
    void update(float ax, float ay, float az,
                float gx, float gy, float gz,
                float dt);

    // ======================== 姿态获取接口 ========================
    
    /**
     * @brief 获取完整的姿态数据（绝对姿态）
     */
    IMUAttitude getAttitude() const;

    /**
     * @brief 获取姿态四元数（绝对姿态）
     * @note 包含 IMU 的安装倾斜
     */
    Eigen::Quaternionf getQuaternion() const;

    /**
     * @brief 获取欧拉角（绝对姿态）
     * @note 包含 IMU 的安装倾斜
     */
    Eigen::Vector3f getEuler() const;

    /**
     * @brief 获取旋转矩阵（绝对姿态）
     */
    Eigen::Matrix3f getRotationMatrix() const;

    /**
     * @brief 获取当前估计的重力方向（世界坐标系）
     */
    Eigen::Vector3f getGravity() const;

    /**
     * @brief 获取补偿后的陀螺仪数据（已减去零偏）
     */
    Eigen::Vector3f getCompensatedGyro() const;

    /**
     * @brief 重置姿态为单位四元数，清空校准状态
     */
    void reset();

    /**
     * @brief 设置互补滤波系数
     * @param alpha 陀螺仪信任权重 (0~1)
     *       0.98 = 98% 陀螺仪 + 2% 加速度计（平滑，响应慢）
     */
    void setFilterCoefficient(float alpha) { alpha_ = alpha; }

    // ======================== 零漂校准接口 ========================
    // 零漂校准：消除陀螺仪静态偏置
    // 原理：静止时陀螺仪输出理论上应为 0，实际有固定偏移
    // 执行方式：开机自动（auto_calibration = true）或手动服务调用
    
    /**
     * @brief 获取当前陀螺仪零偏值
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
     */
    void setPresetBias(const Eigen::Vector3f& bias);

    /**
     * @brief 启用/禁用零偏补偿
     */
    void setBiasCompensationEnabled(bool enabled) { bias_compensation_enabled_ = enabled; }

    // ======================== 零点校准接口 ========================
    // 零点校准：消除 IMU 安装倾斜的影响
    // 原理：记录水平静止时的姿态作为参考零点
    //       后续姿态 = 当前姿态 - 参考零点（相对变化）
    // 执行方式：手动触发（推荐），或配置为开机自动（auto_calibration = true）
    
    /**
     * @brief 设置零点校准相关参数
     * @param enabled   是否启用零点校准功能
     * @param threshold 静止检测阈值 (rad/s)
     * @param samples   连续静止采样次数（预留，暂未使用）
     */
    void setZeroPointConfig(bool enabled, float threshold, int samples);

    /**
     * @brief 获取零点校准是否启用
     */
    bool isZeroPointEnabled() const { return zero_point_enabled_; }

    /**
     * @brief 设置预置零点四元数（从配置文件加载）
     * @param quat 零点四元数
     * @note 作为启动时的初始零点值
     */
    void setPresetZeroPoint(const Eigen::Quaternionf& quat);

    /**
     * @brief 手动执行零点校准
     * @return true=成功，false=失败
     * 
     * 执行条件：
     *   1. 零点校准功能已启用
     *   2. IMU 已初始化
     *   3. 车辆静止（角速度 < threshold）
     * 
     * @note 会覆盖之前的零点值（包括预设值）
     */
    bool calibrateZeroPoint();

    // ======================== 零点校准输出接口 ========================
    
    /**
     * @brief 获取相对初始姿态的四元数（已减去零点偏置）
     * @return 相对四元数，如果未校准返回单位四元数
     * 
     * 计算公式：relative = initial_quat_.inverse() * quat_
     * 这是姿态补偿需要的数据：只关心"相对变化"，不关心"初始偏移"
     */
    Eigen::Quaternionf getRelativeQuaternion() const;

    /**
     * @brief 获取相对初始姿态的欧拉角
     * @return 欧拉角向量，如果未校准返回 (0, 0, 0)
     */
    Eigen::Vector3f getRelativeEuler() const;

    /**
     * @brief 检查零点是否已校准
     */
    bool isZeroPointCalibrated() const { return initial_quat_set_; }

    /**
     * @brief 获取当前零点四元数
     */
    Eigen::Quaternionf getZeroPointQuat() const { return initial_quat_; }

private:
    // ======================== 姿态状态 ========================
    Eigen::Quaternionf quat_;        // 当前姿态四元数（绝对姿态）
    Eigen::Vector3f gravity_;        // 重力方向向量（世界坐标系）
    bool initialized_ = false;       // 是否已完成首次初始化

    float alpha_ = 0.98f;            // 互补滤波系数 (0~1)

    mutable std::mutex mutex_;       // 线程安全锁

    // ======================== 零漂校准 ========================
    Eigen::Vector3f gyro_bias_ = Eigen::Vector3f::Zero();          // 陀螺仪零偏 (rad/s)
    Eigen::Vector3f compensated_gyro_ = Eigen::Vector3f::Zero();   // 补偿后的角速度
    std::atomic<bool> bias_calibrated_{false};                     // 是否已校准
    std::atomic<bool> bias_compensation_enabled_{true};            // 是否启用补偿

    // ======================== 零点校准 ========================
    // 零点校准：记录水平静止时的姿态作为参考点
    // 预设值从配置文件加载，手动校准会覆盖
    bool zero_point_enabled_ = true;                     // 是否启用零点校准
    float stationary_threshold_ = 0.02f;                 // 静止检测阈值 (rad/s)
    
    Eigen::Quaternionf initial_quat_ = Eigen::Quaternionf::Identity();   // 零点参考四元数
    bool initial_quat_set_ = false;                      // 是否已设置零点

    // ======================== 内部算法函数 ========================
    
    /**
     * @brief 陀螺仪积分更新姿态
     * 原理：q_new = q * q_delta, q_delta = [1, ω·dt/2]
     */
    Eigen::Quaternionf gyroUpdate(const Eigen::Quaternionf& q,
                                  const Eigen::Vector3f& gyro,
                                  float dt);

    /**
     * @brief 加速度计估算姿态
     * 原理：计算从参考重力方向 (0,0,-1) 到当前加速度的旋转
     */
    Eigen::Quaternionf accelUpdate(const Eigen::Vector3f& accel);

    /**
     * @brief 互补滤波融合
     * 原理：q_result = slerp(q_gyro, q_accel, 1-alpha)
     */
    Eigen::Quaternionf complementaryFilter(const Eigen::Quaternionf& q_gyro,
                                           const Eigen::Quaternionf& q_accel);

    /**
     * @brief 四元数转欧拉角 (Z-Y-X 顺序)
     * 符合 ROS2 REP 103 标准 (ENU 坐标系)
     */
    Eigen::Vector3f quaternionToEuler(const Eigen::Quaternionf& q) const;

    /**
     * @brief 四元数转旋转矩阵
     */
    Eigen::Matrix3f quaternionToRotationMatrix(const Eigen::Quaternionf& q) const;
};

}  // namespace algorithms