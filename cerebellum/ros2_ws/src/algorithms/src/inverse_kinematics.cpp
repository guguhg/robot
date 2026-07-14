#include "algorithms/inverse_kinematics.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace algorithms {

/**
 * @brief 构造函数
 * 
 */
InverseKinematics::InverseKinematics()
{
    reset();
}

/**
 * @brief 设置底盘类型
 * 
 * @param type 
 */
void InverseKinematics::setChassisType(ChassisType type)
{
    std::lock_guard<std::mutex> lock(mutex_);
    params_.type = type;
}

/**
 * @brief 设置参数
 * 
 * @param wheel_base 轴距 
 * @param wheel_track 轮距
 * @param wheel_radius 轮半径
 * @param max_speed 最大轮速
 * @param min_speed 最小轮速
 */
void InverseKinematics::setParams(float wheel_base, float wheel_track,
                                   float wheel_radius, float max_speed, float min_speed)
{
    std::lock_guard<std::mutex> lock(mutex_);
    params_.wheel_base = std::abs(wheel_base);
    params_.wheel_track = std::abs(wheel_track);
    params_.wheel_radius = std::abs(wheel_radius);
    params_.max_speed = max_speed;
    params_.min_speed = min_speed;
}

/**
 * @brief 设置速度限幅
 * 
 * @param max_speed 
 * @param min_speed 
 */
void InverseKinematics::setSpeedLimits(float max_speed, float min_speed)
{
    std::lock_guard<std::mutex> lock(mutex_);
    params_.max_speed = max_speed;
    params_.min_speed = min_speed;
}

/**
 * @brief 复位
 * 
 */
void InverseKinematics::reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    // 无状态需要重置
}

/**
 * @brief 限幅
 * 
 * @param value 值 
 * @param min_val 最小值
 * @param max_val 最大值
 * @return float 值
 */
float InverseKinematics::clamp(float value, float min_val, float max_val)
{
    return std::max(min_val, std::min(max_val, value));
}

/**
 * @brief 限幅
 * 
 * @param speeds 
 */
void InverseKinematics::clampSpeeds(std::array<float, 4>& speeds)
{
    for (auto& s : speeds) {
        s = clamp(s, params_.min_speed, params_.max_speed);
    }
}

// ============================================================
// 麦轮逆运动学
// ============================================================
// 麦轮布局:
//      LF (左前)    RF (右前)
//          ╲    ╱
//           ╲  ╱
//           ╱  ╲
//          ╱    ╲
//      LR (左后)    RR (右后)
//
// 公式 (右轮与左轮反向):
//   v_LF = vx - vy - ω·L
//   v_RF = vx + vy + ω·L
//   v_LR = vx + vy + ω·L
//   v_RR = vx - vy - ω·L

/**
 * @brief 麦轮逆运动学
 * 
 * @param vx 前向速度 (m/s)
 * @param vy 横移速度 (m/s)
 * @param omega 角速度 (rad/s)
 * @return IKResult 轮速 (r/s)
 */
IKResult InverseKinematics::processMecanum(float vx, float vy, float omega)
{
    IKResult result;
    result.chassis_type_name = "mecanum";

    // 获取轮子半径，用于速度单位转换
    float radius = params_.wheel_radius;
    if (radius < 0.0001f) {
        radius = 0.05f;  // 默认 5cm
    }

    float Lx = params_.wheel_base / 2.0f;
    float Ly = params_.wheel_track / 2.0f;
    float L = Lx + Ly;  // 机器人半径 (m)

    // 1. 计算线速度 (m/s)
    float v_LF_mps = vx - vy - omega * L;
    float v_RF_mps = vx + vy + omega * L;
    float v_LR_mps = vx + vy - omega * L;   
    float v_RR_mps = vx - vy + omega * L;   

    // 2. 线速度 (m/s) → 轮速 (r/s)
    //    轮速 = 线速度 / (2π × 半径)
    float circumference = 2.0f * M_PI * radius;
    float v_LF_rps = v_LF_mps / circumference;
    float v_RF_rps = v_RF_mps / circumference;
    float v_LR_rps = v_LR_mps / circumference;
    float v_RR_rps = v_RR_mps / circumference;

    result.wheel_speeds = {v_LF_rps, v_RF_rps, v_LR_rps, v_RR_rps};
    clampSpeeds(result.wheel_speeds);

    result.valid = true;
    result.limited = false;

    return result;
}

// ============================================================
// 差速逆运动学
// ============================================================
// 公式:
//   left  = (vx - ω·track/2) / radius
//   right = (vx + ω·track/2) / radius
//   LF=left, RF=right, LR=left, RR=right
// 
// 注意：差速公式已经输出轮速 (r/s)，不需要额外转换
//      因为 (m/s) / (m) = r/s
/**
 * @brief 差速逆运动学
 * 
 * @param vx 前向速度 (m/s)
 * @param omega 角速度 (rad/s)
 * @return IKResult 轮速 (r/s)
 */
IKResult InverseKinematics::processDifferential(float vx, float omega)
{
    IKResult result;
    result.chassis_type_name = "differential";

    float radius = params_.wheel_radius;
    if (radius < 0.0001f) {
        radius = 0.05f;
    }

    // 差速公式输出的是轮速 (r/s)
    // 因为 vx (m/s) / radius (m) = (1/s) = r/s
    float left_speed = (vx - omega * params_.wheel_base / 2.0f) / radius;
    float right_speed = (vx + omega * params_.wheel_base / 2.0f) / radius;

    result.wheel_speeds = {left_speed, right_speed, left_speed, right_speed};
    clampSpeeds(result.wheel_speeds);

    result.valid = true;
    result.limited = false;

    return result;
}

// ============================================================
// 普通四轮独立驱动逆运动学
// ============================================================
// 与麦轮公式相同，但强制忽略横向移动 (vy=0)
/**
 * @brief 普通四轮逆运动学
 * 
 * @param vx 前向速度 (m/s)
 * @param omega 角速度 (rad/s)
 * @return IKResult 轮速 (r/s)
 */
IKResult InverseKinematics::processFourWDStandard(float vx, float omega)
{
    IKResult result;
    result.chassis_type_name = "4wd_standard";

    float radius = params_.wheel_radius;
    if (radius < 0.0001f) {
        radius = 0.05f;
    }

    float Lx = params_.wheel_base / 2.0f;
    float Ly = params_.wheel_track / 2.0f;
    float L = Lx + Ly;

    float vy = 0.0f;  // 强制忽略横向

    // 1. 计算线速度 (m/s)
    float v_LF_mps = vx - vy - omega * L;
    float v_RF_mps = vx + vy + omega * L;
    float v_LR_mps = vx + vy - omega * L;   
    float v_RR_mps = vx - vy + omega * L;   

    // 2. 线速度 (m/s) → 轮速 (r/s)
    float circumference = 2.0f * M_PI * radius;
    float v_LF_rps = v_LF_mps / circumference;
    float v_RF_rps = v_RF_mps / circumference;
    float v_LR_rps = v_LR_mps / circumference;
    float v_RR_rps = v_RR_mps / circumference;

    result.wheel_speeds = {v_LF_rps, v_RF_rps, v_LR_rps, v_RR_rps};
    clampSpeeds(result.wheel_speeds);

    result.valid = true;
    result.limited = false;

    return result;
}

/**
 * @brief 核心处理接口
 * 
 * @param linear_x 前向速度
 * @param linear_y 横移速度
 * @param angular_z 角速度
 * @return IKResult 
 */
IKResult InverseKinematics::process(float linear_x, float linear_y, float angular_z)
{
    std::lock_guard<std::mutex> lock(mutex_);

    // 死区处理
    const float DEADBAND = 0.001f;//上层有死区处理了，再加一层
    linear_x = (std::abs(linear_x) < DEADBAND) ? 0.0f : linear_x;
    linear_y = (std::abs(linear_y) < DEADBAND) ? 0.0f : linear_y;
    angular_z = (std::abs(angular_z) < DEADBAND) ? 0.0f : angular_z;

    switch (params_.type) {
        case ChassisType::MECANUM:
            return processMecanum(linear_x, linear_y, angular_z);
        case ChassisType::DIFFERENTIAL:
            return processDifferential(linear_x, angular_z);
        case ChassisType::FOUR_WD_STANDARD:
            return processFourWDStandard(linear_x, angular_z);
        default:
            return processMecanum(linear_x, linear_y, angular_z);
    }
}

/**
 * @brief 向量版本
 * 
 * @param cmd 
 * @return IKResult 
 */
IKResult InverseKinematics::process(const Eigen::Vector3f& cmd)
{
    return process(cmd.x(), cmd.y(), cmd.z());
}

/**
 * @brief 获取底盘类型
 * 
 * @return ChassisType 
 */
ChassisType InverseKinematics::getChassisType() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return params_.type;
}

/**
 * @brief 获取底盘类型编号的字符串
 * 
 * @param type 
 * @return std::string 
 */
std::string InverseKinematics::chassisTypeToString(ChassisType type)
{
    switch (type) {
        case ChassisType::MECANUM:         return "mecanum";
        case ChassisType::DIFFERENTIAL:    return "differential";
        case ChassisType::FOUR_WD_STANDARD: return "4wd_standard";
        default:                           return "unknown";
    }
}

}  // namespace algorithms