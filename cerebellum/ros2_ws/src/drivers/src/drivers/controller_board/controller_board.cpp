#include "drivers/controller_board/controller_board.h"
#include <iostream>
#include <cstring>
#include <chrono>
#include <mutex>

namespace drivers
{
    ControllerBoard::ControllerBoard()
        : mv_(0),
          accel_x_(0), accel_y_(0), accel_z_(0),
          gyro_x_(0), gyro_y_(0), gyro_z_(0),
          last_update_time_(0)
    {
        std::string port = "/dev/ttyACM0";
        int baudrate = 1000000;

        comm_handle_ = std::make_unique<ControllerComm>(
            port,
            baudrate,
            [this](const std::vector<uint8_t> &data)
            {
                this->parseFrame(data);
            });

        comm_handle_->startReceiving();
        std::cout << "ControllerBoard initialized." << std::endl;
    }

    ControllerBoard::~ControllerBoard()
    {
        if (comm_handle_)
        {
            comm_handle_->stopReceiving();
        }
        std::cout << "ControllerBoard destroyed." << std::endl;
    }

    template <typename T>
    T ControllerBoard::bytesToValue(const uint8_t *data, size_t offset)
    {
        T value = 0;
        size_t size = sizeof(T);

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

    void ControllerBoard::parseFrame(const std::vector<uint8_t> &frame)
    {
        size_t pos = 0;

        while (pos < frame.size())
        {
            size_t header_pos = pos;
            bool found_header = false;

            for (size_t i = pos; i < frame.size() - 1; i++)
            {
                if (frame[i] == drivers::protocol::FRAME_HEADER1 && frame[i + 1] == drivers::protocol::FRAME_HEADER2)
                {
                    header_pos = i;
                    found_header = true;
                    break;
                }
            }

            if (!found_header)
            {
                break;
            }

            if (header_pos + 4 >= frame.size())
            {
                break;
            }

            uint8_t func = frame[header_pos + 2];
            uint8_t data_len = frame[header_pos + 3];

            if (data_len > 200)
            {
                pos = header_pos + 1;
                continue;
            }

            size_t expected_len = 4 + data_len + 1;

            if (header_pos + expected_len > frame.size())
            {
                pos = header_pos;
                break;
            }

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
                    std::cerr << "[CRC] func=0x" << std::hex << (int)func
                              << " recv=0x" << (int)received_crc
                              << " calc=0x" << (int)calculated_crc << std::dec << std::endl;
                    crc_error_count++;
                    if (crc_error_count == 10)
                    {
                        std::cerr << "[CRC] Suppressing further errors..." << std::endl;
                    }
                }
                pos = header_pos + 1;
                continue;
            }

            // ============ 处理有效帧 ============
            switch (func)
            {
            case drivers::protocol::FUNC_IMU:
            {
                if (data_len >= 24)
                {
                    std::lock_guard<std::mutex> lock(data_mutex_);
                    accel_x_ = bytesToValue<float>(params.data(), 0);
                    accel_y_ = bytesToValue<float>(params.data(), 4);
                    accel_z_ = bytesToValue<float>(params.data(), 8);
                    gyro_x_ = bytesToValue<float>(params.data(), 12);
                    gyro_y_ = bytesToValue<float>(params.data(), 16);
                    gyro_z_ = bytesToValue<float>(params.data(), 20);

                    auto now = std::chrono::steady_clock::now();
                    last_update_time_ = std::chrono::duration_cast<std::chrono::microseconds>(
                                            now.time_since_epoch())
                                            .count();
                }
                break;
            }

            case drivers::protocol::FUNC_SYS:
            {
                if (data_len >= 3 && params[0] == drivers::protocol::SYS_READ_VOLTAGE)
                {
                    std::lock_guard<std::mutex> lock(data_mutex_);
                    mv_ = bytesToValue<uint16_t>(params.data(), 1);
                }
                break;
            }

            default:
                break;
            }

            pos = header_pos + expected_len;
        }
    }

    bool ControllerBoard::imuDataGet(float &accel_x, float &accel_y, float &accel_z,
                                     float &gyro_x, float &gyro_y, float &gyro_z)
    {
        std::lock_guard<std::mutex> lock(data_mutex_);

        auto now = std::chrono::steady_clock::now();
        uint64_t now_us = std::chrono::duration_cast<std::chrono::microseconds>(
                              now.time_since_epoch())
                              .count();

        if (last_update_time_ == 0 || (now_us - last_update_time_) > 5000000)
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

    bool ControllerBoard::voltageGet(uint16_t &mv)
    {
        std::lock_guard<std::mutex> lock(data_mutex_);

        auto now = std::chrono::steady_clock::now();
        uint64_t now_us = std::chrono::duration_cast<std::chrono::microseconds>(
                              now.time_since_epoch())
                              .count();

        if (last_update_time_ == 0 || (now_us - last_update_time_) > 5000000)
        {
            return false;
        }

        mv = mv_;
        return true;
    }

    // 显式实例化模板
    template float ControllerBoard::bytesToValue<float>(const uint8_t *, size_t);
    template uint16_t ControllerBoard::bytesToValue<uint16_t>(const uint8_t *, size_t);

    void ControllerBoard::floatToBytes(float value, uint8_t *bytes)
    {
        uint32_t intVal;
        std::memcpy(&intVal, &value, sizeof(float));
        bytes[0] = (intVal >> 0) & 0xFF;
        bytes[1] = (intVal >> 8) & 0xFF;
        bytes[2] = (intVal >> 16) & 0xFF;
        bytes[3] = (intVal >> 24) & 0xFF;
    }

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
        std::cout << "[Tx] ";
        for (uint8_t b : frame)
            printf("%02X ", b);
        printf("\n");

        return comm_handle_->send(frame);
    }

    bool ControllerBoard::motorCtrl(const uint8_t motor_id, const float speed_rs)
    {
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

    bool ControllerBoard::motorCtrl(const std::map<uint8_t, float> &mt_op)
    {
        if (mt_op.empty())
        {
            std::cerr << "motorCtrl: motor map is empty!" << std::endl;
            return false;
        }

        if (mt_op.size() > 255)
        {
            std::cerr << "motorCtrl: too many motors!" << std::endl;
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

            params.push_back(motor_id);

            uint8_t speed_bytes[4];
            floatToBytes(speed, speed_bytes);
            params.insert(params.end(), speed_bytes, speed_bytes + 4);
        }

        return sendFrame(protocol::FUNC_MOTOR, params);
    }

    bool ControllerBoard::motorStop(const uint8_t motor_id)
    {
        // 参数：子命令(1) + motor_id(1) = 2 字节
        std::vector<uint8_t> params;

        // 子命令：单个电机停止
        params.push_back(protocol::MOTOR_STOP_SINGLE);

        // 电机 ID
        params.push_back(motor_id);

        return sendFrame(protocol::FUNC_MOTOR, params);
    }

    bool ControllerBoard::motorStop(const std::vector<uint8_t> &mt_op)
    {
        if (mt_op.empty())
        {
            std::cerr << "motorStop: motor list is empty!" << std::endl;
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
            if (id < 8)
            {
                mask |= (1 << id);
            }
            else
            {
                std::cerr << "motorStop: motor id " << (int)id << " exceeds 7!" << std::endl;
                return false;
            }
        }
        params.push_back(mask);

        return sendFrame(protocol::FUNC_MOTOR, params);
    }

}