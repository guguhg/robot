#include "drivers/controller_board/controller_board.h"
#include "drivers/controller_board/protocol.h"
#include "common/config_loader/config_loader.hpp"
#include "common/logger/logger.hpp"
#include <iostream>
#include <cstring>
#include <chrono>
#include <mutex>
#include <iomanip>
#include <sstream>

namespace drivers
{

    /**
     * @brief 全局静态唯一句柄
     *
     * @return ControllerBoard&
     */
    ControllerBoard &ControllerBoard::getInstance()
    {
        static ControllerBoard instance;
        return instance;
    }

    /**
     * @brief 构造函数，从配置文件提取硬件与波特率、启动接收线程
     *
     */
    ControllerBoard::ControllerBoard()
        : mv_(0),
          accel_x_(0), accel_y_(0), accel_z_(0),
          gyro_x_(0), gyro_y_(0), gyro_z_(0),
          last_update_time_(0)
    {
        // 提取配置文件
        loadConfig();

        // 通信句柄实例化，注册接收回调函数
        comm_handle_ = std::make_unique<ControllerComm>(
            config_.port,
            config_.baudrate,
            config_.timeout_ms,
            [this](const std::vector<uint8_t> &data)
            {
                this->parseFrame(data);
            });

        // 开始接收线程处理
        comm_handle_->startReceiving();
        LOG_INFO("ControllerBoard initialized.\n");
    }

    /**
     * @brief 析构函数，停止销毁线程
     *
     */
    ControllerBoard::~ControllerBoard()
    {
        if (comm_handle_)
        {
            comm_handle_->stopReceiving();
        }
        LOG_INFO("ControllerBoard destroyed.\n");
    }

    /**
     * @brief 用于字节流转换为各种基础类型返回，小端模式
     *
     * @tparam T 基础类型
     * @param data 字节流
     * @param offset 偏移量
     * @return T 基础类型
     */
    template <typename T>
    T ControllerBoard::bytesToValue(const uint8_t *data, size_t offset)
    {
        T value = 0;
        size_t size = sizeof(T);

        // 编译判断，浮点类型
        if constexpr (std::is_floating_point_v<T>)
        {
            std::memcpy(&value, data + offset, size);
        }
        else
        {
            for (size_t i = 0; i < size; i++)
            {
                value |= (static_cast<T>(data[offset + i]) << (i * 8));
            }
        }
        return value;
    }

    /**
     * @brief 提取并解析帧，更新IMU与电压数据
     *
     * @param frame 字节流
     */
    void ControllerBoard::parseFrame(const std::vector<uint8_t> &frame)
    {
        size_t pos = 0;

        while (pos < frame.size())
        {
            size_t header_pos = pos;
            bool found_header = false;

            // 找到帧头
            for (size_t i = pos; i < frame.size() - 1; i++)
            {
                if (frame[i] == drivers::protocol::FRAME_HEADER1 && frame[i + 1] == drivers::protocol::FRAME_HEADER2)
                {
                    header_pos = i;
                    found_header = true;
                    break;
                }
            }

            // 未找到帧头
            if (!found_header)
            {
                break;
            }

            // 帧是否被截断
            if (header_pos + 4 >= frame.size())
            {
                break;
            }

            // 功能码提取
            uint8_t func = frame[header_pos + 2];
            // 数据长度提取
            uint8_t data_len = frame[header_pos + 3];

            // 数据长度异常，跳过该帧头，重新找下一个
            if (data_len > 200)
            {
                pos = header_pos + 1;
                continue;
            }

            // 帧总长度
            size_t expected_len = 4 + data_len + 1;

            // 数据被截断
            if (header_pos + expected_len > frame.size())
            {
                pos = header_pos;
                break;
            }

            // 提取参数段
            std::vector<uint8_t> params;
            if (data_len > 0)
            {
                params.insert(params.end(),
                              frame.begin() + header_pos + 4,
                              frame.begin() + header_pos + 4 + data_len);
            }

            // ============ CRC 校验 ============
            // 计算 CRC：功能码 + 数据长度 + 参数
            std::vector<uint8_t> crc_data;
            crc_data.push_back(func);
            crc_data.push_back(data_len);
            crc_data.insert(crc_data.end(), params.begin(), params.end());

            uint8_t received_crc = frame[header_pos + 4 + data_len];
            uint8_t calculated_crc = protocol::crc8::calculate(crc_data);

            if (received_crc != calculated_crc)
            {
                // CRC 校验失败，跳过这个帧头继续找下一个
                static int crc_error_count = 0;
                if (crc_error_count < 10)
                {
                    LOG_DEBUG("[CRC] func=0x%02X recv=0x%02X calc=0x%02X\n", func, received_crc, calculated_crc);
                    crc_error_count++;
                    if (crc_error_count == 10)
                    {
                        LOG_WARN("[CRC] Suppressing further errors...\n");
                    }
                }
                pos = header_pos + 1;
                continue;
            }

            // ============ 处理有效帧 ============
            switch (func)
            {
                // IMU数据
            case drivers::protocol::FUNC_IMU:
            {
                if (data_len >= 24)
                {
                    // 互斥锁，防止上层读的时候同时在写，导致只更新一半出现数据异常
                    std::lock_guard<std::mutex> lock(data_mutex_);
                    accel_x_ = bytesToValue<float>(params.data(), 0);
                    accel_y_ = bytesToValue<float>(params.data(), 4);
                    accel_z_ = bytesToValue<float>(params.data(), 8);
                    gyro_x_ = bytesToValue<float>(params.data(), 12);
                    gyro_y_ = bytesToValue<float>(params.data(), 16);
                    gyro_z_ = bytesToValue<float>(params.data(), 20);

                    // 更新时间戳
                    auto now = std::chrono::steady_clock::now();
                    last_update_time_ = std::chrono::duration_cast<std::chrono::microseconds>(
                                            now.time_since_epoch())
                                            .count();
                }
                break;
            }

                // 系统电压数据
            case drivers::protocol::FUNC_SYS:
            {
                if (data_len >= 3 && params[0] == drivers::protocol::SYS_READ_VOLTAGE)
                {
                    // 互斥锁，防止上层读的时候同时在写，导致只更新一半出现数据异常
                    std::lock_guard<std::mutex> lock(data_mutex_);
                    mv_ = bytesToValue<uint16_t>(params.data(), 1);
                }
                break;
            }

            default:
                break;
            }

            std::stringstream ss;
            ss << "[Rx] ";
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)frame[header_pos] << " "
               << std::setw(2) << (int)frame[header_pos + 1] << " "
               << std::setw(2) << (int)func << " "
               << std::setw(2) << (int)data_len << " ";
            for (uint8_t b : params)
            {
                ss << std::setw(2) << (int)b << " ";
            }
            ss << std::setw(2) << (int)received_crc;
            LOG_DEBUG("%s", ss.str().c_str());

            // 寻找下一帧
            pos = header_pos + expected_len;
        }
    }

    // 显式实例化模板
    template float ControllerBoard::bytesToValue<float>(const uint8_t *, size_t);
    template uint16_t ControllerBoard::bytesToValue<uint16_t>(const uint8_t *, size_t);

    /**
     * @brief 浮点类型转字节流，小端模式
     *
     * @param value 浮点值
     * @param bytes 字节流
     */
    void ControllerBoard::floatToBytes(float value, uint8_t *bytes)
    {
        uint32_t intVal;
        std::memcpy(&intVal, &value, sizeof(float));
        bytes[0] = (intVal >> 0) & 0xFF;
        bytes[1] = (intVal >> 8) & 0xFF;
        bytes[2] = (intVal >> 16) & 0xFF;
        bytes[3] = (intVal >> 24) & 0xFF;
    }

    /**
     * @brief 发送帧，自动压入帧头、功能码、长度、参数、CRC8
     *
     * @param func 功能码
     * @param params 参数
     * @return true 发送成功
     * @return false 发送失败，串口问题，可能是未打开串口或其他异常
     */
    bool ControllerBoard::sendFrame(uint8_t func, const std::vector<uint8_t> &params)
    {
        std::vector<uint8_t> frame;

        frame.push_back(protocol::FRAME_HEADER1);
        frame.push_back(protocol::FRAME_HEADER2);
        frame.push_back(func);
        frame.push_back(static_cast<uint8_t>(params.size()));
        frame.insert(frame.end(), params.begin(), params.end());

        // CRC 计算：功能码 + 数据长度 + 参数
        std::vector<uint8_t> crc_data;
        crc_data.push_back(func);
        crc_data.push_back(static_cast<uint8_t>(params.size()));
        crc_data.insert(crc_data.end(), params.begin(), params.end());

        uint8_t crc = protocol::crc8::calculate(crc_data);
        frame.push_back(crc);

        // 调试打印
        std::stringstream ss;
        ss << "[Tx] ";
        for (uint8_t b : frame)
        {
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)b << " ";
        }
        LOG_DEBUG("%s", ss.str().c_str());

        return comm_handle_->send(frame);
    }

    // ============ 静态公有接口实现 ============

    /**
     * @brief 获取IMU数据接口
     * 为什么不用结构体？因为该板载接口随时可能被弃用，但是上层会一直使用，应该是上层规划下层，而不是下层约束上层
     * @param accel_x 加速度x
     * @param accel_y 加速度y
     * @param accel_z 加速度z
     * @param gyro_x 陀螺仪x
     * @param gyro_y 陀螺仪y
     * @param gyro_z 陀螺仪z
     * @return true 读取成功
     * @return false 数据超过5s没更新，更新超时
     */
    bool ControllerBoard::imuDataGet(float &accel_x, float &accel_y, float &accel_z,
                                     float &gyro_x, float &gyro_y, float &gyro_z)
    {
        return getInstance().imuDataGet_private(accel_x, accel_y, accel_z,
                                                gyro_x, gyro_y, gyro_z);
    }

    /**
     * @brief 私有IMU数据获取实现
     *
     */
    bool ControllerBoard::imuDataGet_private(float &accel_x, float &accel_y, float &accel_z,
                                             float &gyro_x, float &gyro_y, float &gyro_z)
    {
        std::lock_guard<std::mutex> lock(data_mutex_);

        auto now = std::chrono::steady_clock::now();
        uint64_t now_us = std::chrono::duration_cast<std::chrono::microseconds>(
                              now.time_since_epoch())
                              .count();

        if (last_update_time_ == 0 || (now_us - last_update_time_) > (uint64_t)config_.data_timeout_ms * 1000)
        {
            return false;
        }

        accel_x = accel_x_;
        accel_y = accel_y_;
        accel_z = accel_z_;
        gyro_x = gyro_x_;
        gyro_y = gyro_y_;
        gyro_z = gyro_z_;

        return true;
    }

    /**
     * @brief 获取系统电压数据接口
     *
     * @param mv 毫伏
     * @return true 读取最新数据成功
     * @return false 数据超时
     */
    bool ControllerBoard::voltageGet(uint16_t &mv)
    {
        return getInstance().voltageGet_private(mv);
    }

    /**
     * @brief 私有电压数据获取实现
     *
     */
    bool ControllerBoard::voltageGet_private(uint16_t &mv)
    {
        std::lock_guard<std::mutex> lock(data_mutex_);

        auto now = std::chrono::steady_clock::now();
        uint64_t now_us = std::chrono::duration_cast<std::chrono::microseconds>(
                              now.time_since_epoch())
                              .count();

        if (last_update_time_ == 0 || (now_us - last_update_time_) > (uint64_t)config_.data_timeout_ms * 1000)
        {
            return false;
        }

        mv = mv_;
        return true;
    }

    /**
     * @brief 控制单个电机
     *
     * @param motor_id 电机ID 0~3，取决于控制板
     * @param speed_rs 转速，单位r/s，+正转 -反转
     * @return true 发送帧成功
     * @return false 发送帧失败、电机id超出范围、转速超出范围
     */
    bool ControllerBoard::motorCtrl(const uint8_t motor_id, const float speed_rs)
    {
        return getInstance().motorCtrl_private(motor_id, speed_rs);
    }

    /**
     * @brief 私有单个电机控制实现
     *
     */
    bool ControllerBoard::motorCtrl_private(const uint8_t motor_id, const float speed_rs)
    {
        if (motor_id >= config_.motor_count)
            return false;

        if (speed_rs < config_.min_speed || speed_rs > config_.max_speed)
            return false;

        // 参数：子命令(1) + motor_id(1) + speed(4) = 6 字节
        std::vector<uint8_t> params;

        // 子命令：单个电机控制
        params.push_back(protocol::MOTOR_CTRL_SINGLE);

        // 电机 ID
        params.push_back(motor_id);

        // 速度值 (float, 小端)
        uint8_t speed_bytes[4];
        floatToBytes(speed_rs, speed_bytes);
        params.insert(params.end(), speed_bytes, speed_bytes + 4);

        return sendFrame(protocol::FUNC_MOTOR, params);
    }

    /**
     * @brief 控制多个电机
     *
     * @param mt_op 字典，id:rs
     * @return true 发送帧成功
     * @return false 发送帧失败、电机id超出范围、转速超出范围
     */
    bool ControllerBoard::motorCtrl(const std::map<uint8_t, float> &mt_op)
    {
        return getInstance().motorCtrl_private(mt_op);
    }

    /**
     * @brief 私有多个电机控制实现
     *
     */
    bool ControllerBoard::motorCtrl_private(const std::map<uint8_t, float> &mt_op)
    {
        if (mt_op.empty())
        {
            LOG_WARN("motorCtrl: motor map is empty!\n");
            return false;
        }

        if (mt_op.size() > 255)
        {
            LOG_WARN("motorCtrl: too many motors!\n");
            return false;
        }

        // 参数：子命令(1) + 电机数量(1) + N*(motor_id(1) + speed(4))
        std::vector<uint8_t> params;

        // 子命令：多个电机控制
        params.push_back(protocol::MOTOR_CTRL_MULTI);

        // 电机数量
        params.push_back(static_cast<uint8_t>(mt_op.size()));

        // 每个电机的 ID 和速度
        for (const auto &pair : mt_op)
        {
            uint8_t motor_id = pair.first;
            float speed = pair.second;

            if (motor_id >= config_.motor_count)
                return false;

            if (speed < config_.min_speed || speed > config_.max_speed)
                return false;

            params.push_back(motor_id);

            uint8_t speed_bytes[4];
            floatToBytes(speed, speed_bytes);
            params.insert(params.end(), speed_bytes, speed_bytes + 4);
        }
        return sendFrame(protocol::FUNC_MOTOR, params);
    }

    /**
     * @brief 停止单个电机(急刹切断, Ctrl0速度是控制板PID制动)
     *
     * @param motor_id 电机id 0~3
     * @return true 发送帧成功
     * @return false 发送帧失败、电机id超出范围、转速超出范围
     */
    bool ControllerBoard::motorStop(const uint8_t motor_id)
    {
        return getInstance().motorStop_private(motor_id);
    }

    /**
     * @brief 私有单个电机停止实现
     *
     */
    bool ControllerBoard::motorStop_private(const uint8_t motor_id)
    {
        if (motor_id >= config_.motor_count)
            return false;

        // 参数：子命令(1) + motor_id(1) = 2 字节
        std::vector<uint8_t> params;

        // 子命令：单个电机停止
        params.push_back(protocol::MOTOR_STOP_SINGLE);

        // 电机 ID
        params.push_back(motor_id);

        return sendFrame(protocol::FUNC_MOTOR, params);
    }

    /**
     * @brief 停止多个电机(急刹切断, Ctrl0速度是控制板PID制动)
     *
     * @param mt_op vector，id
     * @return true 发送帧成功
     * @return false 发送帧失败、电机id超出范围、转速超出范围
     */
    bool ControllerBoard::motorStop(const std::vector<uint8_t> &mt_op)
    {
        return getInstance().motorStop_private(mt_op);
    }

    /**
     * @brief 私有多个电机停止实现
     *
     */
    bool ControllerBoard::motorStop_private(const std::vector<uint8_t> &mt_op)
    {
        if (mt_op.empty())
        {
            LOG_WARN("motorStop: motor list is empty!\n");
            return false;
        }

        // 参数：子命令(1) + 掩码(1) = 2 字节
        std::vector<uint8_t> params;

        // 子命令：多个电机停止
        params.push_back(protocol::MOTOR_STOP_MULTI);

        // 计算电机号掩码（最多支持8个电机）
        uint8_t mask = 0;
        for (uint8_t id : mt_op)
        {
            if (id < config_.motor_count)
            {
                mask |= (1 << id);
            }
            else
            {
                LOG_WARN("motorStop: motor id %d exceeds %d !\n", (int)id, config_.motor_count);
                return false;
            }
        }
        params.push_back(mask);

        return sendFrame(protocol::FUNC_MOTOR, params);
    }

    /**
     * @brief 从配置文件读取相关配置
     *
     */
    void ControllerBoard::loadConfig()
    {
        try
        {
            YAML::Node config = common::ConfigLoader::loadDefault()["drivers"];

            auto cb_config = config["controller_board"]["serial"];
            config_.port = cb_config["port"] ? cb_config["port"].as<std::string>() : "/dev/ttyACM0";
            config_.baudrate = cb_config["baudrate"] ? cb_config["baudrate"].as<int>() : 1000000;
            config_.timeout_ms = cb_config["timeout_ms"] ? cb_config["timeout_ms"].as<int>() : 100;

            config_.data_timeout_ms = config["sensors"]["timeout_ms"] ? config["sensors"]["timeout_ms"].as<int>() : 5000;

            auto motors_config = config["motors"];
            config_.motor_count = motors_config["count"] ? motors_config["count"].as<int>() : 4;
            config_.max_speed = motors_config["max_speed"] ? motors_config["max_speed"].as<float>() : 1.33f;
            config_.min_speed = motors_config["min_speed"] ? motors_config["min_speed"].as<float>() : -1.33f;

            LOG_INFO("[ControllerBoard] Config loaded successfully.");
            LOG_INFO("[ControllerBoard]\nport:%s\nbaudrate:%d\ntimeout_ms:%d\ndata_timeout_ms:%d\nmotor_count:%d\nmax_speed:%.2f\nmin_speed:%.2f\n",
                     config_.port.c_str(),
                     config_.baudrate,
                     config_.timeout_ms,
                     config_.data_timeout_ms,
                     config_.motor_count,
                     config_.max_speed,
                     config_.min_speed);
        }
        catch (const std::exception &e)
        {
            LOG_WARN("[ControllerBoard] Failed to load config: %s\n", e.what());
            LOG_WARN("[ControllerBoard] Using default values.\n");
        }
    }

}