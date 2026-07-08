#pragma once

#include <cstring>
#include <cstdint>
#include <vector>
#include <map>

namespace drivers
{
    /**
     * @brief 0xAA 0x55 功能码(uint8_t) 参数长度（字节数uint8_t） 参数[功能子码 + 参数] CRC8
     * 
     */
    namespace protocol
    {
        /**
         * @brief 帧头
         * 
         */
        constexpr uint8_t FRAME_HEADER1 = 0xAA;
        constexpr uint8_t FRAME_HEADER2 = 0x55;

        /**
         * @brief 功能码
         * 
         */
        enum FunctionCode : uint8_t
        {
            FUNC_MOTOR = 0x03,
            FUNC_IMU = 0x07,
            FUNC_SYS = 0x00,
        };

        /**
         * @brief 功能子码，电机控制
         * 
         */
        enum MotorSubCmd : uint8_t
        {
            MOTOR_CTRL_SINGLE = 0x00,
            MOTOR_CTRL_MULTI = 0x01,
            MOTOR_STOP_SINGLE = 0x02,
            MOTOR_STOP_MULTI = 0x03,
        };

        /**
         * @brief 功能子码，读取电压
         * 
         */
        enum SysSubCmd : uint8_t
        {
            SYS_READ_VOLTAGE = 0x04,
        };

        // ============ CRC-8 ============
        // 参数: poly=0x31, init=0x00, refin=True, refout=True, xorout=0x00
        namespace crc8
        {
            /**
             * @brief 反转一个字节的位顺序
             */
            inline uint8_t reverse_byte(uint8_t byte)
            {
                uint8_t rev = 0;
                for (int i = 0; i < 8; ++i)
                {
                    rev = (rev << 1) | (byte & 1);
                    byte >>= 1;
                }
                return rev;
            }

            /**
             * @brief 计算 CRC-8
             * 参数: poly=0x31, init=0x00, refin=True, refout=True, xorout=0x00
             */
            inline uint8_t calculate(const uint8_t *data, size_t len)
            {
                uint8_t crc = 0x00;  // init = 0x00

                for (size_t i = 0; i < len; ++i)
                {
                    // refin=True: 输入字节位反转
                    uint8_t byte = reverse_byte(data[i]);
                    crc ^= byte;

                    for (int j = 0; j < 8; ++j)
                    {
                        if (crc & 0x80)
                        {
                            crc = (crc << 1) ^ 0x31;  // poly = 0x31
                        }
                        else
                        {
                            crc <<= 1;
                        }
                        crc &= 0xFF;
                    }
                }

                // refout=True: 结果位反转
                return reverse_byte(crc);
            }

            /**
             * @brief 计算CRC-8 vector版本
             * 
             * @param data 数据流
             * @return uint8_t crc值
             */
            inline uint8_t calculate(const std::vector<uint8_t> &data)
            {
                return calculate(data.data(), data.size());
            }
        }

    }

}