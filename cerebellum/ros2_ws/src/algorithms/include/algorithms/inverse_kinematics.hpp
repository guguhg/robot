#pragma once

#include <Eigen/Dense>
#include <array>
#include <mutex>
#include <cmath>

namespace algorithms {

/**
 * @brief 底盘类型
 * 
 * 增加底盘类型步骤：
 * 1. 在此枚举中添加类型
 * 2. inverse_kinematics.cpp 中 chassisTypeToString 添加字符串
 * 3. 添加 processXXX 求解函数
 * 4. process 函数中调用新增的求解函数
 * 5. ik_node.cpp 中 loadConfig 添加类型赋值
 */
enum class ChassisType : uint8_t {
    MECANUM = 0,           // 麦轮 (4WD) → 支持全向移动 (vx, vy, omega)
    DIFFERENTIAL = 1,      // 差速 (2WD) → 只有 vx + omega
    FOUR_WD_STANDARD = 2,  // 普通四轮独立驱动 → 忽略横向移动 (vx + omega, vy=0)
    // ACKERMANN = 3,      // 阿克曼 (待扩展)
};

/**
 * @brief 逆运动学结果
 */
struct IKResult {
    bool valid = false;
    std::array<float, 4> wheel_speeds = {0.0f, 0.0f, 0.0f, 0.0f};  // LF, RF, LR, RR (r/s)
    bool limited = false;
    std::string chassis_type_name;
};

/**
 * @brief 逆运动学处理器
 * 
 * 将 Twist 速度指令转换为四轮转速：
 * 
 * - 麦轮 (MECANUM):
 *   支持横移 (vy)，右轮与左轮方向相反
 *   v_LF = vx - vy - ω·L
 *   v_RF = vx + vy + ω·L
 *   v_LR = vx + vy + ω·L
 *   v_RR = vx - vy - ω·L
 * 
 * - 差速 (DIFFERENTIAL):
 *   左侧同速，右侧同速
 *   left = (vx - ω·track/2) / radius
 *   right = (vx + ω·track/2) / radius
 *   LF=left, RF=right, LR=left, RR=right
 * 
 * - 普通四轮独立驱动 (FOUR_WD_STANDARD):
 *   与麦轮公式相同，但强制忽略横向移动 (vy=0)
 */
class InverseKinematics {
public:
    InverseKinematics();
    ~InverseKinematics() = default;

    // ============ 配置接口 ============
    void setChassisType(ChassisType type);
    void setParams(float wheel_base, float wheel_track, 
                   float wheel_radius = 0.05f,
                   float max_speed = 1.33f, 
                   float min_speed = -1.33f);
    void setSpeedLimits(float max_speed, float min_speed);
    void reset();

    // ============ 核心处理接口 ============
    IKResult process(float linear_x, float linear_y, float angular_z);
    IKResult process(const Eigen::Vector3f& cmd);

    // ============ 状态查询 ============
    ChassisType getChassisType() const;
    static std::string chassisTypeToString(ChassisType type);

private:
    struct Params {
        ChassisType type = ChassisType::MECANUM;
        float wheel_base = 0.3f;
        float wheel_track = 0.25f;
        float wheel_radius = 0.05f;
        float max_speed = 1.33f;
        float min_speed = -1.33f;
    } params_;

    // ============ 各底盘逆运动学求解 ============
    IKResult processMecanum(float vx, float vy, float omega);
    IKResult processDifferential(float vx, float omega);
    IKResult processFourWDStandard(float vx, float omega);
    
    // ============ 工具函数 ============
    float clamp(float value, float min_val, float max_val);
    void clampSpeeds(std::array<float, 4>& speeds);

    mutable std::mutex mutex_;
};

}  // namespace algorithms