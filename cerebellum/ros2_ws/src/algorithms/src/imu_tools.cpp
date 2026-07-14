#include "algorithms/imu_tools.hpp"
#include "common/logger/logger.hpp"
#include <cmath>

/*
 * ============================================================================
 * 四元数基础
 * ============================================================================
 * 
 * 四元数 q = w + xi + yj + zk
 *   - w: 实部（标量）
 *   - (x, y, z): 虚部（矢量）
 * 
 * 轴角表示法：
 *   轴: axis = (ax, ay, az)  ← 单位向量
 *   角: angle = θ             ← 弧度
 *   四元数: q = (cos(θ/2), sin(θ/2)·ax, sin(θ/2)·ay, sin(θ/2)·az)
 * 
 * 示例：
 *   无旋转:        q = (1, 0, 0, 0)
 *   绕Z轴转90°:    q = (0.707, 0, 0, 0.707)
 *   绕X轴转180°:   q = (0, 1, 0, 0)
 * 
 * 重要性质：
 *   单位四元数: |q| = 1
 *   共轭（逆旋转）: q_inv = (w, -x, -y, -z)
 *   乘法（旋转叠加）: q3 = q1 * q2 (先旋转q2，再旋转q1)
 *   不满足交换律: q1*q2 ≠ q2*q1
 * 
 * ============================================================================
 * 坐标系说明
 * ============================================================================
 * 
 * 世界坐标系: ENU (东北天)
 * IMU坐标系: 固定在IMU芯片上，随IMU转动
 * 
 * 数据流：
 *   IMU原始数据 (IMU坐标系)
 *   ├── 加速度 (ax, ay, az)
 *   ├── 角速度 (gx, gy, gz)
 *   ↓
 *   姿态计算 (四元数)
 *   quat_ = 从IMU坐标系到世界坐标系的旋转
 *   ↓
 *   重力方向 (世界坐标系)
 *   gravity_world = (0, 0, -9.81)
 *   ↓ 旋转到IMU坐标系
 *   gravity_imu = rot.transpose() * gravity_world
 */

namespace algorithms
{

// ============================================================================
// 构造与配置
// ============================================================================

IMUTools::IMUTools()
    : quat_(Eigen::Quaternionf::Identity()),
      gravity_(0.0f, 0.0f, -9.81f)
{
    // 初始化为单位四元数（无旋转）
    // 重力默认向下（世界坐标系 ENU 标准）
}

void IMUTools::setZeroPointConfig(bool enabled, float threshold, int samples)
{
    std::lock_guard<std::mutex> lock(mutex_);
    zero_point_enabled_ = enabled;
    stationary_threshold_ = std::max(0.001f, threshold);
    // samples 参数预留，暂未使用
    (void)samples;
}

// ============================================================================
// 零点校准
// ============================================================================

void IMUTools::setPresetZeroPoint(const Eigen::Quaternionf& quat)
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (quat.norm() > 0.001f) {
        initial_quat_ = quat.normalized();
        initial_quat_set_ = true;
        
        Eigen::Vector3f init_euler = quaternionToEuler(initial_quat_);
        LOG_INFO("[IMUTools] Preset zero point loaded:");
        LOG_INFO("[IMUTools]   Quat: w=%.4f, x=%.4f, y=%.4f, z=%.4f",
                 initial_quat_.w(), initial_quat_.x(), 
                 initial_quat_.y(), initial_quat_.z());
        LOG_INFO("[IMUTools]   Euler: roll=%.2f°, pitch=%.2f°, yaw=%.2f°",
                 init_euler.x() * 180.0 / M_PI,
                 init_euler.y() * 180.0 / M_PI,
                 init_euler.z() * 180.0 / M_PI);
    }
}

bool IMUTools::calibrateZeroPoint()
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    // ---- 前置条件检查 ----
    
    // 1. 检查功能是否启用
    if (!zero_point_enabled_) {
        LOG_WARN("[IMUTools] Zero point calibration is disabled");
        return false;
    }
    
    // 2. 检查 IMU 是否已初始化
    if (!initialized_) {
        LOG_WARN("[IMUTools] Cannot calibrate: IMU not initialized yet");
        return false;
    }

    // 3. 检查车辆是否静止（角速度足够小）
    float gyro_magnitude = compensated_gyro_.norm();
    if (gyro_magnitude > stationary_threshold_) {
        LOG_WARN("[IMUTools] Cannot calibrate: IMU is moving");
        LOG_WARN("[IMUTools]   gyro=%.4f rad/s > threshold=%.4f rad/s",
                 gyro_magnitude, stationary_threshold_);
        LOG_WARN("[IMUTools]   Place vehicle on level ground and keep still");
        return false;
    }

    // ---- 执行校准（覆盖之前的零点值） ----
    
    initial_quat_ = quat_;
    initial_quat_set_ = true;

    // 打印校准结果
    Eigen::Vector3f init_euler = quaternionToEuler(initial_quat_);
    LOG_INFO("[IMUTools] ========================================");
    LOG_INFO("[IMUTools] Zero point calibrated successfully!");
    LOG_INFO("[IMUTools]   Quat: w=%.4f, x=%.4f, y=%.4f, z=%.4f",
             initial_quat_.w(), initial_quat_.x(), 
             initial_quat_.y(), initial_quat_.z());
    LOG_INFO("[IMUTools]   Euler: roll=%.2f°, pitch=%.2f°, yaw=%.2f°",
             init_euler.x() * 180.0 / M_PI,
             init_euler.y() * 180.0 / M_PI,
             init_euler.z() * 180.0 / M_PI);
    LOG_WARN("[IMUTools]   Copy to config.yaml:");
    LOG_WARN("[IMUTools]     preset_zero_point:");
    LOG_WARN("[IMUTools]       w: %.4f", initial_quat_.w());
    LOG_WARN("[IMUTools]       x: %.4f", initial_quat_.x());
    LOG_WARN("[IMUTools]       y: %.4f", initial_quat_.y());
    LOG_WARN("[IMUTools]       z: %.4f", initial_quat_.z());
    LOG_INFO("[IMUTools] ========================================");

    return true;
}

// ============================================================================
// 核心更新函数
// ============================================================================

void IMUTools::update(float ax, float ay, float az,
                      float gx, float gy, float gz,
                      float dt)
{
    std::lock_guard<std::mutex> lock(mutex_);

    // ---- Step 1: 加速度归一化 ----
    // 只关心加速度的方向（重力方向），不关心大小
    // 示例：accel = (0.5, 0.3, 9.78) → (0.05, 0.03, 0.998)
    Eigen::Vector3f accel(ax, ay, az);
    float accel_norm = accel.norm();
    if (accel_norm > 0.001f) {
        accel.normalize();
    } else {
        return;  // 加速度太小，无法使用
    }

    // ---- Step 2: 角速度零偏补偿 ----
    // 如果已校准，减去零偏值
    Eigen::Vector3f gyro;
    if (bias_compensation_enabled_.load() && bias_calibrated_.load()) {
        gyro = Eigen::Vector3f(
            gx - gyro_bias_.x(),
            gy - gyro_bias_.y(),
            gz - gyro_bias_.z());
    } else {
        gyro = Eigen::Vector3f(gx, gy, gz);
    }
    compensated_gyro_ = gyro;

    // ---- Step 3: 陀螺仪积分 → 短期姿态 ----
    // 陀螺仪测量角速度，积分得到角度变化
    // 短期准确，但会累积漂移
    Eigen::Quaternionf q_gyro = gyroUpdate(quat_, gyro, dt);

    // ---- Step 4: 加速度计估算 → 长期姿态 ----
    // 加速度计测量重力方向，可用于估算姿态
    // 长期稳定，但有噪声，受运动加速度干扰
    Eigen::Quaternionf q_accel = accelUpdate(accel);

    // ---- Step 5: 互补滤波融合 ----
    // 结合陀螺仪和加速度计的优点
    // alpha = 0.98: 98% 信任陀螺仪，2% 信任加速度计
    if (initialized_) {
        quat_ = complementaryFilter(q_gyro, q_accel);
    } else {
        // 首次启动，直接使用加速度计姿态
        quat_ = q_accel;
        initialized_ = true;
        LOG_INFO("[IMUTools] IMU initialized");
    }

    // 归一化确保单位四元数
    quat_.normalize();

    // ---- Step 6: 更新重力方向 ----
    // 将重力向量从"世界坐标系"转换到"IMU坐标系"
    Eigen::Matrix3f rot = quat_.toRotationMatrix();
    gravity_ = rot.transpose() * Eigen::Vector3f(0.0f, 0.0f, -9.81f);
    
    // 注意：零点校准不在此处自动执行，由用户手动触发或开机自动触发
}

// ============================================================================
// 陀螺仪积分
// ============================================================================

Eigen::Quaternionf IMUTools::gyroUpdate(const Eigen::Quaternionf &q,
                                        const Eigen::Vector3f &gyro,
                                        float dt)
{
    /*
     * 原理：q_new = q * q_delta
     * 
     * q_delta 是角速度增量对应的四元数：
     *   Δθ = ω × dt
     *   q_delta ≈ [1, ω·dt/2]  (小角度近似)
     * 
     * 示例：
     *   q = (1, 0, 0, 0), gyro = (0, 0, 0.1), dt = 0.01
     *   q_delta = (1, 0, 0, 0.0005)
     *   q_new = (1, 0, 0, 0.0005)  ← 绕Z轴转了0.0005 rad
     */
    Eigen::Quaternionf q_delta;
    q_delta.x() = gyro.x() * dt / 2.0f;
    q_delta.y() = gyro.y() * dt / 2.0f;
    q_delta.z() = gyro.z() * dt / 2.0f;
    q_delta.w() = 1.0f;

    Eigen::Quaternionf result = q * q_delta;
    result.normalize();
    return result;
}

// ============================================================================
// 加速度计姿态估算
// ============================================================================

Eigen::Quaternionf IMUTools::accelUpdate(const Eigen::Vector3f &accel)
{
    /*
     * 原理：计算从"参考重力方向"到"当前加速度方向"的旋转
     * 
     * 参考重力: gravity = (0, 0, -1)  (向下)
     * 当前加速度: accel (归一化后的重力方向)
     * 
     * 轴角表示：
     *   旋转轴: axis = gravity × accel
     *   旋转角: angle = acos(gravity · accel)
     * 
     * 示例：
     *   gravity = (0, 0, -1), accel = (0.5, 0, -0.866)  (前倾30°)
     *   axis = (0, -0.866, 0), angle = 30°
     *   q = (0.966, 0, 0.259, 0)
     */
    Eigen::Vector3f gravity(0.0f, 0.0f, -1.0f);
    Eigen::Vector3f axis = gravity.cross(accel);
    float angle = std::acos(std::clamp(gravity.dot(accel), -1.0f, 1.0f));

    // 当重力方向和加速度方向相同时，无法确定旋转轴
    if (axis.norm() < 0.001f) {
        return Eigen::Quaternionf::Identity();
    }

    axis.normalize();
    Eigen::Quaternionf q(Eigen::AngleAxisf(angle, axis));
    q.normalize();
    return q;
}

// ============================================================================
// 互补滤波
// ============================================================================

Eigen::Quaternionf IMUTools::complementaryFilter(const Eigen::Quaternionf &q_gyro,
                                                 const Eigen::Quaternionf &q_accel)
{
    /*
     * 互补滤波 = 球面线性插值 (slerp)
     * 
     * q_result = slerp(q_gyro, q_accel, 1 - alpha)
     * 
     * alpha = 0.98: 98% 信任陀螺仪（平滑），2% 信任加速度计（修正漂移）
     * 
     * 为什么叫"互补"？
     *   陀螺仪: 高频准确，低频漂移
     *   加速度计: 低频准确，高频噪声
     *   两者互补
     */
    Eigen::Quaternionf result = q_gyro.slerp(1.0f - alpha_, q_accel);
    result.normalize();
    return result;
}

// ============================================================================
// 姿态获取接口
// ============================================================================

IMUAttitude IMUTools::getAttitude() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    IMUAttitude att;
    att.quaternion = quat_;
    att.euler = quaternionToEuler(quat_);
    att.rotation_matrix = quaternionToRotationMatrix(quat_);
    att.gravity = gravity_;
    att.valid = initialized_;
    return att;
}

Eigen::Quaternionf IMUTools::getQuaternion() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return quat_;
}

Eigen::Vector3f IMUTools::getEuler() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return quaternionToEuler(quat_);
}

Eigen::Matrix3f IMUTools::getRotationMatrix() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return quaternionToRotationMatrix(quat_);
}

Eigen::Vector3f IMUTools::getGravity() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return gravity_;
}

Eigen::Vector3f IMUTools::getCompensatedGyro() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return compensated_gyro_;
}

// ============================================================================
// 重置
// ============================================================================

void IMUTools::reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    quat_ = Eigen::Quaternionf::Identity();
    gravity_ = Eigen::Vector3f(0.0f, 0.0f, -9.81f);
    initialized_ = false;
    initial_quat_set_ = false;
    initial_quat_ = Eigen::Quaternionf::Identity();
}

// ============================================================================
// 零漂校准接口
// ============================================================================

Eigen::Vector3f IMUTools::getGyroBias() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return gyro_bias_;
}

void IMUTools::resetBiasCalibration()
{
    std::lock_guard<std::mutex> lock(mutex_);
    gyro_bias_ = Eigen::Vector3f::Zero();
    bias_calibrated_ = false;
    LOG_INFO("[IMUTools] Gyro bias reset");
}

void IMUTools::setPresetBias(const Eigen::Vector3f &bias)
{
    std::lock_guard<std::mutex> lock(mutex_);
    gyro_bias_ = bias;
    bias_calibrated_ = true;
    LOG_INFO("[IMUTools] Preset gyro bias loaded: x=%.4f, y=%.4f, z=%.4f rad/s",
             bias.x(), bias.y(), bias.z());
}

// ============================================================================
// 零点校准输出接口
// ============================================================================

Eigen::Quaternionf IMUTools::getRelativeQuaternion() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initial_quat_set_) {
        return Eigen::Quaternionf::Identity();
    }
    
    /*
     * 相对四元数 = 初始零点的逆 × 当前姿态
     * 
     * 原理：
     *   当前位置 = 初始位置 + 相对位移
     *   相对位移 = 当前位置 - 初始位置
     * 
     * 示例：
     *   initial_quat_ = (0.966, 0, 0.259, 0)  (前倾30°)
     *   quat_ = (0.966, 0, 0.259, 0)  (静止)
     *   relative = initial_quat_.inverse() * quat_ = (1, 0, 0, 0)
     *   结果：静止时输出单位四元数 ✓
     */
    return initial_quat_.inverse() * quat_;
}

Eigen::Vector3f IMUTools::getRelativeEuler() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initial_quat_set_) {
        return Eigen::Vector3f::Zero();
    }
    
    Eigen::Quaternionf relative_quat = initial_quat_.inverse() * quat_;
    return quaternionToEuler(relative_quat);
}

// ============================================================================
// 工具函数
// ============================================================================

Eigen::Vector3f IMUTools::quaternionToEuler(const Eigen::Quaternionf &q) const
{
    Eigen::Vector3f euler;
    euler.z() = std::atan2(2.0f * (q.w() * q.z() + q.x() * q.y()),
                           1.0f - 2.0f * (q.y() * q.y() + q.z() * q.z()));//yaw
    euler.y() = std::asin(std::clamp(2.0f * (q.w() * q.y() - q.z() * q.x()),
                                     -1.0f, 1.0f));//pitch
    euler.x() = std::atan2(2.0f * (q.w() * q.x() + q.y() * q.z()),
                           1.0f - 2.0f * (q.x() * q.x() + q.y() * q.y()));//roll
    return euler;
}

Eigen::Matrix3f IMUTools::quaternionToRotationMatrix(const Eigen::Quaternionf &q) const
{
    return q.toRotationMatrix();
}

}  // namespace algorithms