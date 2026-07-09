#pragma once
#include <mutex>
#include <cstdint>

namespace drivers
{
    /**
     * @brief 电池管理系统结构体
     *
     */
    struct BMSData
    {
        uint16_t voltage_mv = 0; // 电池电压:毫伏
        uint8_t soc = 0;         // 电池电量百分比
        bool valid = false;      // 本次数据是否最新(config.yaml配置超时时间)
    };

    class BMS
    {
    public:
        /**
         * @brief 全局静态获取BMS数据
         *
         * @return BMSData BMS数据
         */
        static BMSData getBmsData();

        // 禁止拷贝
        BMS(const BMS &) = delete;
        BMS &operator=(const BMS &) = delete;

    private:
        explicit BMS(); // 构造函数
        ~BMS() = default;
        static BMS &getInstance(); // 获取全局静态私有单例句柄

        BMSData data_; // BMS数据

        float max_voltage_v_ = 0;      // 电池满电电压
        float warn_voltage_v_ = 0;     // 电池20%电量电压
        float critical_voltage_v_ = 0; // 电池电量即将为0%

        BMSData getBmsData_private(); // 获取BMS数据具体实现
        std::mutex mutex_;            // 共享数据互斥锁
    };
}