#include "drivers/controller_board/controller_board.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <map>

int main()
{
    auto* board = drivers::ControllerBoard::getInstance();

    std::cout << "等待3秒..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // ============ 传感器测试 ============
    std::cout << "\n===== 传感器测试 =====" << std::endl;
    uint16_t mv;
    float ax, ay, az, gx, gy, gz;

    for (int i = 0; i < 5; i++)
    {
        if (board->imuDataGet(ax, ay, az, gx, gy, gz) && board->voltageGet(mv))
        {
            std::cout << "ax=" << ax << " ay=" << ay << " az=" << az
                      << " | gx=" << gx << " gy=" << gy << " gz=" << gz
                      << " | V=" << mv / 1000.0f << "V" << std::endl;
        }
        else
        {
            std::cout << "等待数据..." << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // ============ 电机测试 ============
    std::cout << "\n===== 电机测试 =====" << std::endl;

    // 测试电机0 正转
    std::cout << "电机0 正转 1r/s ..." << std::endl;
    board->motorCtrl(0, 1.0f);
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // 测试电机0 反转
    std::cout << "电机0 反转 -1r/s ..." << std::endl;
    board->motorCtrl(0, -1.0f);
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // 停止电机0
    std::cout << "电机0 停止" << std::endl;
    board->motorStop(0);

    std::this_thread::sleep_for(std::chrono::seconds(1));

    // ============ 多个电机同时控制 ============
    std::cout << "\n4个电机同时运行 ..." << std::endl;
    std::map<uint8_t, float> motors;
    motors[0] = 1.0f;
    motors[1] = -0.5f;
    motors[2] = 1.0f;
    motors[3] = -0.5f;
    board->motorCtrl(motors);
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // 全部停止
    std::cout << "全部停止" << std::endl;
    std::vector<uint8_t> stop_list = {0, 1, 2, 3};
    board->motorStop(stop_list);

    std::cout << "\n===== 测试完成 =====" << std::endl;
    return 0;
}