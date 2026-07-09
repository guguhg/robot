#include "drivers/controller_board/controller_board.h"
#include "drivers/bms/bms.h"
#include "common/config_loader/config_loader.hpp"
#include "common/logger/logger.hpp"

namespace drivers
{
    /**
     * @brief 获取全局静态私有单例句柄
     *
     * @return BMS& 句柄
     */
    BMS &BMS::getInstance()
    {
        static BMS instance;
        return instance;
    }

    /**
     * @brief 构造函数,读取配置文件:100%电压、20%电压、0%电压
     *
     */
    BMS::BMS()
    {
        try
        {
            YAML::Node config = common::ConfigLoader::loadDefault()["drivers"]["bms"]["voltage"];
            max_voltage_v_ = config["max"] ? config["max"].as<float>() : 12.6f;
            warn_voltage_v_ = config["warn"] ? config["warn"].as<float>() : 10.8f;
            critical_voltage_v_ = config["critical"] ? config["critical"].as<float>() : 9.6f;
            LOG_INFO("[BMS] Config loaded successfully.\n");
            LOG_INFO("[BMS]\nmax_voltage_v:%.1f\nwarn_voltage_v:%.1f\ncritical_voltage_v:%.1f\n",
                     max_voltage_v_, warn_voltage_v_, critical_voltage_v_);
        }
        catch (const std::exception &e)
        {
            LOG_WARN("[BMS] Failed to load config: %s\n", e.what());
            LOG_WARN("[BMS] Using default values.\n");
        }
    }

    /**
     * @brief BMS数据获取具体实现
     *
     * @return BMSData
     */
    BMSData BMS::getBmsData_private()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        uint16_t mv;
        bool success = drivers::ControllerBoard::voltageGet(mv); // 获取电压数据

        // 判断数据是否有效
        if (!success || mv == 0)
        {
            data_.valid = false;
            return data_; // 返回旧数据,并标注数据失效
        }

        // 更新数据
        data_.voltage_mv = mv;
        data_.valid = true;

        float voltage_v = mv / 1000.0f;

        // 分段计算 SOC
        if (voltage_v >= max_voltage_v_) // 100%
        {
            data_.soc = 100;
        }
        else if (voltage_v >= warn_voltage_v_) // >20%
        {
            float soc = 15.0f + (voltage_v - warn_voltage_v_) / (max_voltage_v_ - warn_voltage_v_) * 85.0f;
            data_.soc = static_cast<uint8_t>(soc + 0.5f);
        }
        else if (voltage_v >= critical_voltage_v_) // >0%
        {
            float soc = (voltage_v - critical_voltage_v_) / (warn_voltage_v_ - critical_voltage_v_) * 15.0f;
            data_.soc = static_cast<uint8_t>(soc + 0.5f);
        }
        else
        {
            data_.soc = 0;
        }

        // 限幅
        if (data_.soc > 100)
            data_.soc = 100;

        return data_;
    }

    /**
     * @brief 全局静态获取唯一BMS数据
     *
     * @return BMSData
     */
    BMSData BMS::getBmsData()
    {
        return getInstance().getBmsData_private();
    }

}