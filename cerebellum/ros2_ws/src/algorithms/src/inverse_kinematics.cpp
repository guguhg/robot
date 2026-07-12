#include "algorithms/inverse_kinematics.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace algorithms {

// ============ 构造函数 ============
InverseKinematics::InverseKinematics()
{
    reset();
}

// ============ 配置接口 ============

void InverseKinematics::setChassisType(ChassisType type)
{
    std::lock_guard<std::mutex> lock(mutex_);
    params_.type = type;
}

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

void InverseKinematics::setSpeedLimits(float max_speed, float min_speed)
{
    std::lock_guard<std::mutex> lock(mutex_);
    params_.max_speed = max_speed;
    params_.min_speed = min_speed;
}

void InverseKinematics::reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    // 无状态需要重置
}

// ============ 工具函数 ============

float InverseKinematics::clamp(float value, float min_val, float max_val)
{
    return std::max(min_val, std::min(max_val, value));
}

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
IKResult InverseKinematics::processMecanum(float vx, float vy, float omega)
{
    IKResult result;
    result.chassis_type_name = "mecanum";

    float Lx = params_.wheel_base / 2.0f;
    float Ly = params_.wheel_track / 2.0f;
    float L = Lx + Ly;  // 机器人半径

    float v_LF = vx - vy - omega * L;
    float v_RF = vx + vy + omega * L;
    float v_LR = vx + vy - omega * L;   
    float v_RR = vx - vy + omega * L;   

    result.wheel_speeds = {v_LF, v_RF, v_LR, v_RR};
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
IKResult InverseKinematics::processDifferential(float vx, float omega)
{
    IKResult result;
    result.chassis_type_name = "differential";

    float radius = params_.wheel_radius;
    if (radius < 0.0001f) {
        radius = 0.05f;
    }

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
IKResult InverseKinematics::processFourWDStandard(float vx, float omega)
{
    IKResult result;
    result.chassis_type_name = "4wd_standard";

    float Lx = params_.wheel_base / 2.0f;
    float Ly = params_.wheel_track / 2.0f;
    float L = Lx + Ly;

    float vy = 0.0f;  // 强制忽略横向

    float v_LF = vx - vy - omega * L;
    float v_RF = vx + vy + omega * L;
    float v_LR = vx + vy - omega * L;   
    float v_RR = vx - vy + omega * L;   

    result.wheel_speeds = {v_LF, v_RF, v_LR, v_RR};
    clampSpeeds(result.wheel_speeds);

    result.valid = true;
    result.limited = false;

    return result;
}

// ============================================================
// 核心处理接口
// ============================================================
IKResult InverseKinematics::process(float linear_x, float linear_y, float angular_z)
{
    std::lock_guard<std::mutex> lock(mutex_);

    // 死区处理
    const float DEADBAND = 0.001f;
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

IKResult InverseKinematics::process(const Eigen::Vector3f& cmd)
{
    return process(cmd.x(), cmd.y(), cmd.z());
}

// ============ 状态查询 ============

ChassisType InverseKinematics::getChassisType() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return params_.type;
}

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