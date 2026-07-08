#include "drivers/motor/motor.h"
#include "drivers/controller_board/controller_board.h"
#include "common/config_loader/config_loader.hpp"
#include "common/logger/logger.hpp"
#include <algorithm>
#include <cmath>

namespace drivers
{

// ============ 单例获取 ============
Motors& Motors::getInstance()
{
    static Motors instance;
    return instance;
}

// ============ 构造函数 ============
Motors::Motors()
    : motor_count_(4),
      max_speed_rs_(1.33f),
      min_speed_rs_(-1.33f)
{
    try
    {
        YAML::Node config = common::ConfigLoader::loadDefault()["drivers"]["motors"];
        
        motor_count_ = config["count"] ? config["count"].as<int>() : 4;
        max_speed_rs_ = config["max_speed"] ? config["max_speed"].as<float>() : 1.33f;
        min_speed_rs_ = config["min_speed"] ? config["min_speed"].as<float>() : -1.33f;

        LOG_INFO("[Motors] Config loaded successfully.");
        LOG_INFO("[Motors] count:%d, max_speed:%.2f, min_speed:%.2f",
                 motor_count_, max_speed_rs_, min_speed_rs_);
    }
    catch (const std::exception& e)
    {
        LOG_WARN("[Motors] Failed to load config: %s", e.what());
        LOG_WARN("[Motors] Using default values.");
    }

    // 初始化电机速度字典
    for (uint8_t i = 0; i < motor_count_; ++i)
    {
        motor_od_[i] = 0.0f;
    }
}

// ============ 静态公有接口 ============

float Motors::getMotorSpeed_rs(const uint8_t id)
{
    return getInstance().getMotorSpeed_rs_private(id);
}

std::map<uint8_t, float> Motors::getMotorSpeed_rs()
{
    return getInstance().getMotorSpeed_rs_private();
}

bool Motors::ctrlMotorSpeed_rs(const uint8_t id, const float speed_rs)
{
    return getInstance().ctrlMotorSpeed_rs_private(id, speed_rs);
}

bool Motors::ctrlMotorSpeed_rs(const std::map<uint8_t, float>& mt_op)
{
    return getInstance().ctrlMotorSpeed_rs_private(mt_op);
}

bool Motors::ctrlMotorStop(const uint8_t motor_id)
{
    return getInstance().ctrlMotorStop_private(motor_id);
}

bool Motors::ctrlMotorStop(const std::vector<uint8_t>& mt_op)
{
    return getInstance().ctrlMotorStop_private(mt_op);
}

// ============ 私有实现 ============

float Motors::getMotorSpeed_rs_private(const uint8_t id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (id >= motor_count_)
    {
        LOG_WARN("[Motors] Invalid motor id: %d", id);
        return 0.0f;
    }
    
    auto it = motor_od_.find(id);
    if (it != motor_od_.end())
    {
        return it->second;
    }
    
    return 0.0f;
}

std::map<uint8_t, float> Motors::getMotorSpeed_rs_private()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return motor_od_;  // 返回副本
}

bool Motors::ctrlMotorSpeed_rs_private(const uint8_t id, const float speed_rs)
{
    if (id >= motor_count_)
    {
        LOG_WARN("[Motors] Invalid motor id: %d", id);
        return false;
    }

    // 速度限幅
    float clamped_speed = std::clamp(speed_rs, min_speed_rs_, max_speed_rs_);
    
    if (clamped_speed != speed_rs)
    {
        LOG_WARN("[Motors] Speed clamped: %.2f -> %.2f", speed_rs, clamped_speed);
    }

    // 调用 ControllerBoard 发送指令
    bool success = ControllerBoard::motorCtrl(id, clamped_speed);
    
    if (success)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        motor_od_[id] = clamped_speed;
        LOG_DEBUG("[Motors] Motor %d speed set to %.2f", id, clamped_speed);
    }
    else
    {
        LOG_ERROR("[Motors] Failed to control motor %d", id);
    }
    
    return success;
}

bool Motors::ctrlMotorSpeed_rs_private(const std::map<uint8_t, float>& mt_op)
{
    if (mt_op.empty())
    {
        LOG_WARN("[Motors] Motor operation map is empty!");
        return false;
    }

    std::map<uint8_t, float> clamped_ops;
    
    // 验证并限幅
    for (const auto& [id, speed] : mt_op)
    {
        if (id >= motor_count_)
        {
            LOG_WARN("[Motors] Invalid motor id: %d", id);
            return false;
        }
        
        float clamped = std::clamp(speed, min_speed_rs_, max_speed_rs_);
        if (clamped != speed)
        {
            LOG_WARN("[Motors] Speed clamped: id=%d, %.2f -> %.2f", id, speed, clamped);
        }
        clamped_ops[id] = clamped;
    }

    // 调用 ControllerBoard 发送指令
    bool success = ControllerBoard::motorCtrl(clamped_ops);
    
    if (success)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [id, speed] : clamped_ops)
        {
            motor_od_[id] = speed;
            LOG_DEBUG("[Motors] Motor %d speed set to %.2f", id, speed);
        }
    }
    else
    {
        LOG_ERROR("[Motors] Failed to control multiple motors");
    }
    
    return success;
}

bool Motors::ctrlMotorStop_private(const uint8_t motor_id)
{
    if (motor_id >= motor_count_)
    {
        LOG_WARN("[Motors] Invalid motor id: %d", motor_id);
        return false;
    }

    bool success = ControllerBoard::motorStop(motor_id);
    
    if (success)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        motor_od_[motor_id] = 0.0f;
        LOG_DEBUG("[Motors] Motor %d stopped", motor_id);
    }
    else
    {
        LOG_ERROR("[Motors] Failed to stop motor %d", motor_id);
    }
    
    return success;
}

bool Motors::ctrlMotorStop_private(const std::vector<uint8_t>& mt_op)
{
    if (mt_op.empty())
    {
        LOG_WARN("[Motors] Motor stop list is empty!");
        return false;
    }

    // 验证 ID
    for (uint8_t id : mt_op)
    {
        if (id >= motor_count_)
        {
            LOG_WARN("[Motors] Invalid motor id: %d", id);
            return false;
        }
    }

    bool success = ControllerBoard::motorStop(mt_op);
    
    if (success)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (uint8_t id : mt_op)
        {
            motor_od_[id] = 0.0f;
            LOG_DEBUG("[Motors] Motor %d stopped", id);
        }
    }
    else
    {
        LOG_ERROR("[Motors] Failed to stop multiple motors");
    }
    
    return success;
}

}  // namespace drivers