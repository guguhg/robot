#pragma once
#include "drivers/controller_board/controller_comm.h"
#include "drivers/controller_board/protocol.h"
#include <memory>
#include <map>
#include <mutex>
#include <vector>
#include <chrono>

namespace drivers
{

    class ControllerBoard
    {
    public:
        explicit ControllerBoard();
        ~ControllerBoard();

        ControllerBoard(const ControllerBoard &) = delete;
        ControllerBoard &operator=(const ControllerBoard &) = delete;

        bool imuDataGet(float &accel_x, float &accel_y, float &accel_z, 
                        float &gyro_x, float &gyro_y, float &gyro_z);

        bool voltageGet(uint16_t &mv);

        bool motorCtrl(const uint8_t motor_id, const float speed_rs);
        bool motorCtrl(const std::map<uint8_t, float> &mt_op);
        bool motorStop(const uint8_t motor_id);
        bool motorStop(const std::vector<uint8_t> &mt_op);
        
    private:
        std::unique_ptr<ControllerComm> comm_handle_;

        std::mutex data_mutex_;
        uint16_t mv_;
        float accel_x_, accel_y_, accel_z_;
        float gyro_x_, gyro_y_, gyro_z_;
        uint64_t last_update_time_;
        
        void parseFrame(const std::vector<uint8_t>& frame);
        
        template<typename T>
        T bytesToValue(const uint8_t* data, size_t offset);

        bool sendFrame(uint8_t func, const std::vector<uint8_t> &params);
        void floatToBytes(float value, uint8_t *bytes);
    };

} // namespace drivers