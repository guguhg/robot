#include "drivers/controller_board/controller_board.h"
#include "drivers/bms/bms.h"
#include "drivers/imu/imu.h"
#include "drivers/motor/motor.h"
#include "common/config_loader/config_loader.hpp"
#include "common/logger/logger.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <map>
#include <vector>
#include <iomanip>
#include <string>

/**
 * @brief 电机映射结构（从 YAML 读取）
 */
struct MotorMapping
{
    int left_front = 0;
    int right_front = 1;
    int left_rear = 2;
    int right_rear = 3;
    int count = 4;
    float max_speed = 1.33f;
    float min_speed = -1.33f;
};

/**
 * @brief 电机方向系数结构（从 YAML 读取）
 *
 * 左轮正转=向前，右轮正转=向后
 */
struct MotorDirection
{
    float left_front = 1.0f;   // 左前：正转向前
    float right_front = -1.0f; // 右前：正转向后
    float left_rear = 1.0f;    // 左后：正转向前
    float right_rear = -1.0f;  // 右后：正转向后
};

/**
 * @brief 从 YAML 加载电机映射
 */
MotorMapping loadMotorMapping()
{
    MotorMapping mapping;

    try
    {
        YAML::Node config = common::ConfigLoader::loadDefault();
        auto motors = config["drivers"]["motors"];

        mapping.count = motors["count"].as<int>(4);
        mapping.max_speed = motors["max_speed"].as<float>(1.33f);
        mapping.min_speed = motors["min_speed"].as<float>(-1.33f);

        auto map_node = motors["mapping"];
        if (map_node)
        {
            mapping.left_front = map_node["left_front"].as<int>(0);
            mapping.right_front = map_node["right_front"].as<int>(1);
            mapping.left_rear = map_node["left_rear"].as<int>(2);
            mapping.right_rear = map_node["right_rear"].as<int>(3);
        }

        LOG_INFO("[Test] Motor mapping loaded from config");
        LOG_INFO("[Test] LF:%d, RF:%d, LR:%d, RR:%d",
                 mapping.left_front, mapping.right_front,
                 mapping.left_rear, mapping.right_rear);
    }
    catch (const std::exception &e)
    {
        LOG_WARN("[Test] Failed to load motor mapping: %s", e.what());
        LOG_WARN("[Test] Using default mapping");
    }

    return mapping;
}

/**
 * @brief 从 YAML 加载电机方向系数
 */
MotorDirection loadMotorDirection()
{
    MotorDirection dir;

    try
    {
        YAML::Node config = common::ConfigLoader::loadDefault();
        auto dir_node = config["drivers"]["motors"]["direction"];

        if (dir_node)
        {
            dir.left_front = dir_node["left_front"].as<float>(1.0f);
            dir.right_front = dir_node["right_front"].as<float>(-1.0f);
            dir.left_rear = dir_node["left_rear"].as<float>(1.0f);
            dir.right_rear = dir_node["right_rear"].as<float>(-1.0f);
        }

        LOG_INFO("[Test] Motor direction loaded from config");
        LOG_INFO("[Test] LF:%.1f, RF:%.1f, LR:%.1f, RR:%.1f",
                 dir.left_front, dir.right_front,
                 dir.left_rear, dir.right_rear);
    }
    catch (const std::exception &e)
    {
        LOG_WARN("[Test] Failed to load motor direction: %s", e.what());
        LOG_WARN("[Test] Using default direction (LF:+, RF:-, LR:+, RR:-)");
    }

    return dir;
}

/**
 * @brief 获取所有电机 ID 列表
 */
std::vector<uint8_t> getAllMotorIds(const MotorMapping &mapping)
{
    return {
        static_cast<uint8_t>(mapping.left_front),
        static_cast<uint8_t>(mapping.right_front),
        static_cast<uint8_t>(mapping.left_rear),
        static_cast<uint8_t>(mapping.right_rear)};
}

/**
 * @brief 打印分隔线
 */
void printSeparator(const std::string &title = "")
{
    if (!title.empty())
    {
        std::cout << "\n========== " << title << " ==========" << std::endl;
    }
    else
    {
        std::cout << "\n----------------------------------------" << std::endl;
    }
}

/**
 * @brief 打印传感器数据
 */
void printSensorData()
{
    float ax, ay, az, gx, gy, gz;
    uint16_t mv;

    bool imu_ok = drivers::ControllerBoard::imuDataGet(ax, ay, az, gx, gy, gz);
    bool volt_ok = drivers::ControllerBoard::voltageGet(mv);

    auto bms_data = drivers::BMS::getBmsData();
    auto imu_data = drivers::IMU::getImuData();

    std::cout << "┌─────────────────────────────────────────────────────────────┐" << std::endl;

    if (imu_ok && imu_data.valid)
    {
        std::cout << "│ IMU:  ax=" << std::fixed << std::setprecision(4) << ax
                  << " ay=" << ay << " az=" << az
                  << " | gx=" << gx << " gy=" << gy << " gz=" << gz << " │" << std::endl;
    }
    else
    {
        std::cout << "│ IMU:  Waiting for data...                                  │" << std::endl;
    }

    if (volt_ok && bms_data.valid)
    {
        std::cout << "│ BMS:  " << std::fixed << std::setprecision(3)
                  << bms_data.voltage_mv / 1000.0f << "V"
                  << " | SOC: " << std::setw(3) << (int)bms_data.soc << "%"
                  << " | valid: " << (bms_data.valid ? "true" : "false")
                  << "                                    │" << std::endl;
    }
    else
    {
        std::cout << "│ BMS:  Waiting for data...                                  │" << std::endl;
    }

    std::cout << "└─────────────────────────────────────────────────────────────┘" << std::endl;
}

/**
 * @brief 测试单个电机
 */
void testSingleMotor(uint8_t motor_id, float speed, const std::string &name)
{
    std::cout << "▶ " << name << " (ID:" << (int)motor_id
              << ") 转速 " << std::fixed << std::setprecision(1) << speed << " r/s" << std::endl;
    drivers::ControllerBoard::motorCtrl(motor_id, speed);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    drivers::ControllerBoard::motorStop(motor_id);
    std::cout << "  ✓ 停止" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

/**
 * @brief 测试方向系数（全部正转，车辆应向前直行）
 */
void testDirection(const MotorMapping &mapping, const MotorDirection &dir)
{
    std::cout << "\n📐 方向系数验证:" << std::endl;
    std::cout << "  左前 (LF): " << dir.left_front << std::endl;
    std::cout << "  右前 (RF): " << dir.right_front << std::endl;
    std::cout << "  左后 (LR): " << dir.left_rear << std::endl;
    std::cout << "  右后 (RR): " << dir.right_rear << std::endl;

    std::cout << "\n▶ 全部正转 (车辆应向前直行)" << std::endl;

    std::map<uint8_t, float> cmds;
    cmds[static_cast<uint8_t>(mapping.left_front)] = 1.0f * dir.left_front;
    cmds[static_cast<uint8_t>(mapping.right_front)] = 1.0f * dir.right_front;
    cmds[static_cast<uint8_t>(mapping.left_rear)] = 1.0f * dir.left_rear;
    cmds[static_cast<uint8_t>(mapping.right_rear)] = 1.0f * dir.right_rear;

    drivers::ControllerBoard::motorCtrl(cmds);
    std::this_thread::sleep_for(std::chrono::seconds(2));

    auto all_ids = getAllMotorIds(mapping);
    drivers::ControllerBoard::motorStop(all_ids);
    std::cout << "  ✓ 全部停止" << std::endl;
}

/**
 * @brief 测试多个电机同时运行
 */
void testMultiMotor(const MotorMapping &mapping)
{
    std::cout << "\n▶ 4个电机同时运行 (左轮正转，右轮反转)" << std::endl;

    std::map<uint8_t, float> motors;
    motors[static_cast<uint8_t>(mapping.left_front)] = 0.8f;
    motors[static_cast<uint8_t>(mapping.right_front)] = -0.5f;
    motors[static_cast<uint8_t>(mapping.left_rear)] = 0.8f;
    motors[static_cast<uint8_t>(mapping.right_rear)] = -0.5f;

    drivers::ControllerBoard::motorCtrl(motors);
    std::this_thread::sleep_for(std::chrono::seconds(3));

    auto all_ids = getAllMotorIds(mapping);
    drivers::ControllerBoard::motorStop(all_ids);
    std::cout << "  ✓ 全部停止" << std::endl;
}

int main()
{
    LOG_INFO("=== Robot Control Test Started ===");

    std::cout << "\n⏳ 等待3秒，让系统稳定..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // ============ 加载电机映射 ============
    MotorMapping mapping = loadMotorMapping();
    MotorDirection dir = loadMotorDirection();
    auto all_ids = getAllMotorIds(mapping);

    std::cout << "\n📋 电机映射:" << std::endl;
    std::cout << "  左前 (LF): " << mapping.left_front << std::endl;
    std::cout << "  右前 (RF): " << mapping.right_front << std::endl;
    std::cout << "  左后 (LR): " << mapping.left_rear << std::endl;
    std::cout << "  右后 (RR): " << mapping.right_rear << std::endl;
    std::cout << "  电机数量: " << mapping.count << std::endl;
    std::cout << "  速度范围: " << mapping.min_speed << " ~ " << mapping.max_speed << " r/s" << std::endl;

    // ============ 传感器测试 ============
    printSeparator("传感器测试");

    for (int i = 0; i < 20; i++)
    {
        printSensorData();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // ============ 电机测试 ============
    printSeparator("电机测试");

    // 测试各个电机
    testSingleMotor(static_cast<uint8_t>(mapping.left_front), 1.0f, "左前轮 正转");
    testSingleMotor(static_cast<uint8_t>(mapping.left_front), -1.0f, "左前轮 反转");
    testSingleMotor(static_cast<uint8_t>(mapping.right_front), 0.8f, "右前轮 正转");
    testSingleMotor(static_cast<uint8_t>(mapping.right_front), -0.8f, "右前轮 反转");
    testSingleMotor(static_cast<uint8_t>(mapping.left_rear), 1.0f, "左后轮 正转");
    testSingleMotor(static_cast<uint8_t>(mapping.left_rear), -1.0f, "左后轮 反转");
    testSingleMotor(static_cast<uint8_t>(mapping.right_rear), 0.8f, "右后轮 正转");
    testSingleMotor(static_cast<uint8_t>(mapping.right_rear), -0.8f, "右后轮 反转");

    // ============ 方向系数测试 ============
    printSeparator("方向系数测试");
    testDirection(mapping, dir);

    // ============ 多电机同步测试 ============
    printSeparator("多电机同步测试");
    testMultiMotor(mapping);

    // ============ 全部停止 ============
    drivers::ControllerBoard::motorStop(all_ids);

    // ============ 测试完成 ============
    printSeparator("测试完成");
    LOG_INFO("=== Robot Control Test Completed ===");
    std::cout << "\n✅ 所有测试已完成！" << std::endl;

    return 0;
}