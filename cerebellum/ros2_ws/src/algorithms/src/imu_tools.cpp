#include "algorithms/imu_tools.hpp"
#include "common/logger/logger.hpp"
#include <cmath>

/*
    四元数是用来表示三维旋转的数学工具，解决了欧拉角的万向锁问题。
    四元数 = 一个标量 + 三个虚部
    q = w + xi + yj + zk (w标量部分<实部>, x, y, z矢量部分<虚部>)
    q = (w, x, y, z)

    轴: axis = (axis.x, axis.y, axis.z)  ← 单位向量
    角: angle = θ                         ← 弧度
    四元数:
    q = (cos(θ/2), sin(θ/2) × axis.x, sin(θ/2) × axis.y, sin(θ/2) × axis.z)

    示例
    //没有任何旋转
    q = (1, 0, 0, 0)

    //绕 Z 轴旋转 90°
    θ = 90° = π/2, axis = (0, 0, 1)
    q = (cos(π/4), 0, 0, sin(π/4))
      = (0.707, 0, 0, 0.707)

    //绕 X 轴旋转 180°
    θ = 180° = π, axis = (1, 0, 0)
    q = (cos(π/2), sin(π/2), 0, 0)
      = (0, 1, 0, 0)

    四元数的性质
    单位四元数,所有表示旋转的四元数都是单位四元数:|q| = sqrt(w² + x² + y² + z²) = 1
    共轭(逆旋转):q_inv = (w, -x, -y, -z)   // 反向旋转
    乘法(叠加旋转):q3 = q1 * q2   // 先旋转 q2，再旋转 q1
    重要:四元数乘法不满足交换律！q1 × q2 ≠ q2 × q1
    q1 = (w1, x1, y1, z1)
    q2 = (w2, x2, y2, z2)

    q1 × q2 = (
    w1×w2 - x1×x2 - y1×y2 - z1×z2,
    w1×x2 + x1×w2 + y1×z2 - z1×y2,
    w1×y2 + y1×w2 + z1×x2 - x1×z2,
    w1×z2 + z1×w2 + x1×y2 - y1×x2
    )

    看一下例子你就懂了:
    (0.966, 0, 0.259, 0): zx平面绕y轴旋转了 30°
    可以用右手定则比划比划, z轴大拇指朝上、x轴食指朝前、y轴中指朝左

    世界坐标系:固定的、不动的参考坐标系。
    IMU坐标系:固定在 IMU 芯片上的坐标系，随着 IMU 转动而转动。
    IMU 原始数据 (在 IMU 坐标系下)
    │
    ├── 加速度 (ax, ay, az)  ← 在 IMU 坐标系下
    ├── 角速度 (gx, gy, gz)  ← 在 IMU 坐标系下
    │
    ↓ 姿态计算（四元数）
    │
    quat_ = IMU 到世界的旋转
    │
    ↓ 旋转
    │
    重力在世界坐标系 = (0, 0, -9.81)
    ↓ rot.transpose()
    │
    重力在 IMU 坐标系 = gravity_  ← 用于加速度计修正

    3×3 旋转矩阵
    旋转矩阵是描述三维空间中旋转的数学工具，一个 3×3 的正交矩阵。
    绕 X 轴旋转 θ
    Rx(θ) = [ 1    0      0   ]
            [ 0   cosθ  -sinθ ]
            [ 0   sinθ   cosθ ]

    绕 Y 轴旋转 θ
    Ry(θ) = [ cosθ   0    sinθ ]
            [  0     1     0   ]
            [ -sinθ  0    cosθ ]

    绕 Z 轴旋转 θ
    Rz(θ) = [ cosθ  -sinθ  0  ]
            [ sinθ   cosθ  0  ]
            [  0      0    1  ]

    // 世界坐标系中的向量（水平向前）
    v_world = (1, 0, 0)

    // 旋转到 IMU 坐标系（绕 Y 轴转 30°）
    v_imu = Ry(30°) × v_world
          = [ 0.866, 0, -0.5 ]
    // 结果：在 IMU 坐标系中，水平向量变成了向前+向下
    // 物理含义： IMU 前倾 30° 后，原来水平的向量在 IMU 坐标系中表现为向下倾斜。
*/
namespace algorithms
{

    /**
     * @brief 构造函数,初始化四元数(单位四元数<无旋转>)、重力方向为向下（-9.81 m/s²）
     */
    IMUTools::IMUTools()
        : quat_(Eigen::Quaternionf::Identity()),
          gravity_(0.0f, 0.0f, -9.81f)
    {
    }

    /**
     * @brief 更新 IMU 数据并计算姿态
     * @param ax, ay, az 加速度 (m/s²)
     * @param gx, gy, gz 角速度 (rad/s)
     * @param dt 时间增量 (秒)
     */
    void IMUTools::update(float ax, float ay, float az,
                          float gx, float gy, float gz,
                          float dt)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // 加速度归一化
        // 归一化前:accel = (0.5, 0.3, 9.78)  长度 ≈ 9.8
        // 归一化后:accel = (0.05, 0.03, 0.998)
        /*
            水平静止时:
            加速度 = (0, 0, 9.81)
            归一化后 = (0, 0, 1)  ← 重力方向（向上）

            倾斜时:
            加速度 = (2.5, 0, 9.5)
            归一化后 = (0.25, 0, 0.97)  ← 重力方向倾斜

            归一化后，只关心"方向"，不关心"大小"。

            原始加速度向量:
            Z
            ↑
            |  (0, 0, 9.81)
            |
            └──────────→ X

            归一化后 (单位向量):
            Z
            ↑
            |  (0, 0, 1)
            |
            └──────────→ X
            方向相同，长度变为 1
        */
        Eigen::Vector3f accel(ax, ay, az);
        float accel_norm = accel.norm();
        if (accel_norm > 0.001f) {
            accel.normalize();
        } else {
            return; // 加速度太小，无法使用
        }

        // ============ 角速度向量 - 减去零偏 ============
        Eigen::Vector3f gyro;
        if (bias_compensation_enabled_.load() && bias_calibrated_.load()) {
            gyro = Eigen::Vector3f(
                gx - gyro_bias_.x(),
                gy - gyro_bias_.y(),
                gz - gyro_bias_.z());
        } else {
            gyro = Eigen::Vector3f(gx, gy, gz);
        }

        // 保存补偿后的陀螺仪数据（供外部发布使用）
        compensated_gyro_ = gyro;

        // 1. 陀螺仪积分更新姿态
        Eigen::Quaternionf q_gyro = gyroUpdate(quat_, gyro, dt);

        // 2. 加速度计计算姿态（仅使用重力方向）
        Eigen::Quaternionf q_accel = accelUpdate(accel);

        // 3. 互补滤波融合
        if (initialized_) {
            quat_ = complementaryFilter(q_gyro, q_accel);
        } else {
            quat_ = q_accel;
            initialized_ = true;
        }

        // 归一化四元数
        quat_.normalize();

        // 4. 更新重力方向（世界坐标系下的重力向量）
        // 将重力向量从"世界坐标系"转换到"IMU 坐标系"，得到 IMU 当前感受到的重力方向。
        Eigen::Matrix3f rot = quat_.toRotationMatrix();                   // 获取3×3 旋转矩阵,表示从IMU坐标系到世界坐标系的旋转
        gravity_ = rot.transpose() * Eigen::Vector3f(0.0f, 0.0f, -9.81f); // 将重力转到 IMU 坐标系
    }

    /**
     * @brief 用陀螺仪角速度数据更新姿态四元数（积分）
     * @param q 当前姿态四元数
     * @param gyro 当前陀螺仪向量 (rad/s)
     * @param dt 时间步长 (秒)
     * @return 新姿态四元数
     */
    Eigen::Quaternionf IMUTools::gyroUpdate(const Eigen::Quaternionf &q,
                                            const Eigen::Vector3f &gyro,
                                            float dt)
    {
        // 角速度四元数 (忽略二阶小量)
        // 角度变化 = 角速度 × 时间 Δθ = ω × dt
        // q_gyro = [1, ωx·dt/2, ωy·dt/2, ωz·dt/2]
        // 为什么除以2? 四元数使用半角表示旋转。
        Eigen::Quaternionf q_gyro;
        q_gyro.x() = gyro.x() * dt / 2.0f;
        q_gyro.y() = gyro.y() * dt / 2.0f;
        q_gyro.z() = gyro.z() * dt / 2.0f;
        q_gyro.w() = 1.0f;

        // 更新姿态, 四元数乘法 = 两个旋转的叠加。
        // q (旧姿态) × q_gyro (角速度增量) = q_new (新姿态)
        // 顺序很重要： 先应用旧姿态，再叠加角速度增量。
        Eigen::Quaternionf result = q * q_gyro;
        result.normalize();
        return result;

        /*
        时间 t0: 姿态 = q
         │
         ├── 陀螺仪测量: ω = (ωx, ωy, ωz)
         │
         ├── 时间步长: dt
         │
         ├── 角度增量: Δθ = ω × dt
         │
         ├── 四元数增量: q_gyro = [1, Δθ/2]
         │
         └── 新姿态: q_new = q × q_gyro
               q        ×      q_gyro       =     q_new
            (旧姿态)        (角速度增量)          (新姿态)

            // 输入
            q = (1, 0, 0, 0)        // 初始姿态（无旋转）
            gyro = (0, 0, 0.1)      // 绕 Z 轴旋转 0.1 rad/s
            dt = 0.01               // 10ms

            // 计算
            q_gyro.x() = 0 * 0.01 / 2 = 0
            q_gyro.y() = 0 * 0.01 / 2 = 0
            q_gyro.z() = 0.1 * 0.01 / 2 = 0.0005
            q_gyro.w() = 1.0

            // 结果
            q_new = q × q_gyro = (1, 0, 0, 0.0005)
            // 绕 Z 轴旋转了 0.0005 弧度（≈ 0.028°）
        */
    }

    /**
     * @brief 用加速度计数据估算姿态（仅基于重力方向）
     * @param accel 重力方向向量
     * @return 计算出的姿态四元数
     */
    Eigen::Quaternionf IMUTools::accelUpdate(const Eigen::Vector3f &accel)
    {
        /*
            加速度计在静止时测量的是重力方向（向上方向）。
            状态	    加速度计读数	 含义
            水平放置	(0, 0, 9.81)	重力向上
            前倾 30°	(4.9, 0, 8.5)   重力偏前
            左倾 30°	(0, 4.9, 8.5)	重力偏左
            我们可以计算出"从重力方向到当前加速度方向"的旋转，这个旋转就是姿态。
        */

        // 定义参考重力方向, Z 轴负方向（向下）
        Eigen::Vector3f gravity(0.0f, 0.0f, -1.0f);

        // 叉积 → 旋转轴 (同时垂直于 gravity 和 accel 的向量)
        /*
            gravity = (0, 0, -1)  ← 重力方向（向下）
            accel   = (0.5, 0, -0.866)  ← 加速度计测量（前倾 30°）
            axis    = gravity × accel  ← 旋转轴（绕 Y 轴）

            Z (向上)
            ↑
            |   gravity (向下)
            |   ↓
            |   accel (前倾)
            |  /
            | /
            └────────────→ X (向前)
            旋转轴: Y 轴方向 (垂直于 X-Z 平面)
        */
        Eigen::Vector3f axis = gravity.cross(accel);

        // 点积 → 两个向量夹角的余弦值
        /*
            gravity · accel = |gravity| × |accel| × cos(θ) = cos(θ)
            angle = acos(cos(θ))
        */
        float angle = std::acos(std::clamp(gravity.dot(accel), -1.0f, 1.0f));

        // 当 gravity 和 accel 方向相同时，叉积为零向量，无法确定旋转轴
        if (axis.norm() < 0.001f) {
            return Eigen::Quaternionf::Identity();
        }

        // 用轴角表示法构建四元数
        axis.normalize();
        Eigen::Quaternionf q(Eigen::AngleAxisf(angle, axis));
        q.normalize();
        return q;

        /*
            // 输入
            accel = (0.5, 0, -0.866)  // 归一化后的加速度（前倾 30°）
            gravity = (0, 0, -1)      // 参考重力方向

            // 1. 叉积 → 旋转轴
            axis = (0, 0, -1) × (0.5, 0, -0.866)
                 = (0, -0.866, 0)     // 绕 Y 轴旋转

            // 2. 点积 → 角度
            dot = 0.866
            angle = acos(0.866) = 30°

            // 3. 构建四元数
            q = AngleAxis(30°, Y 轴)
              = (cos(15°), 0, sin(15°), 0)
              = (0.966, 0, 0.259, 0)
        */
    }

    /**
     * @brief 互补滤波融合
     * 陀螺仪 → 短期准确（但会累积漂移）
     * 加速度计 → 长期稳定（但有噪声和运动干扰）
     * 互补滤波 = 取两者的"优点"
     * 
     * @param q_gyro 陀螺仪积分得到的姿态
     * @param q_accel 加速度计估算的姿态
     * @return 融合后的姿态
     */
    Eigen::Quaternionf IMUTools::complementaryFilter(const Eigen::Quaternionf &q_gyro,
                                                     const Eigen::Quaternionf &q_accel)
    {
        /*
            slerp球面线性插值
            q_gyro (陀螺仪姿态) ←───────────→ q_accel (加速度计姿态)
            0%                          100%
            │                           │
            └── q_result = 插值结果 ────┘

            插值系数alpha_
            alpha_	1 - alpha_	信任	                   效果
            0.98	0.02	    98% 陀螺仪，2% 加速度计	    平滑，但响应慢
            0.90	0.10	    90% 陀螺仪，10% 加速度计    响应快，但噪声大
            0.50	0.50	    各 50%	                   均衡
            0.00	1.00	    100% 加速度计	            完全依赖加速度计

            时间轴:
                    t0    t1    t2    t3    t4
                    │     │     │     │     │
            陀螺仪:   ──────●──────●──────●──────●  (平滑但漂移)
                            ↑     ↑     ↑     ↑
                        积分   积分   积分   积分

            加速度计:   ●     ●     ●     ●     ●   (准确但有噪声)
                            ↑     ↑     ↑     ↑
                        测量   测量   测量   测量

            融合结果:   ──────●──────●──────●──────●  (平滑且准确)
        */
        Eigen::Quaternionf result = q_gyro.slerp(1.0f - alpha_, q_accel);
        result.normalize();
        return result;
    }

    // ======================== 公共接口 ========================

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

    void IMUTools::reset()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        quat_ = Eigen::Quaternionf::Identity();
        gravity_ = Eigen::Vector3f(0.0f, 0.0f, -9.81f);
        initialized_ = false;
    }

    // ======================== 零漂校准接口 ========================

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
    }

    void IMUTools::setPresetBias(const Eigen::Vector3f &bias)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        gyro_bias_ = bias;
        bias_calibrated_ = true;
    }

    // ======================== 工具函数 ========================

    Eigen::Vector3f IMUTools::quaternionToEuler(const Eigen::Quaternionf &q) const
    {
        // Z-Y-X 顺序 (yaw-pitch-roll)
        Eigen::Vector3f euler;
        euler.z() = std::atan2(2.0f * (q.w() * q.z() + q.x() * q.y()),
                               1.0f - 2.0f * (q.y() * q.y() + q.z() * q.z()));
        euler.y() = std::asin(std::clamp(2.0f * (q.w() * q.y() - q.z() * q.x()),
                                         -1.0f, 1.0f));
        euler.x() = std::atan2(2.0f * (q.w() * q.x() + q.y() * q.z()),
                               1.0f - 2.0f * (q.x() * q.x() + q.y() * q.y()));
        return euler;
    }

    Eigen::Matrix3f IMUTools::quaternionToRotationMatrix(const Eigen::Quaternionf &q) const
    {
        return q.toRotationMatrix();
    }

} // namespace algorithms