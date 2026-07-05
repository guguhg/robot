#pragma once

#include <serial.h>
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <vector>

namespace drivers {

    using FrameCallbackFunc = std::function<void(const std::vector<uint8_t>&)>;

    class ControllerComm {
        public:
            explicit ControllerComm(const std::string& port, 
                                    int baudrate,
                                    FrameCallbackFunc callback = nullptr);
            ~ControllerComm();

            ControllerComm(const ControllerComm&) = delete;
            ControllerComm& operator=(const ControllerComm&) = delete;

            bool send(const std::vector<uint8_t>& data);
            bool send(const std::string& data);

            void startReceiving();
            void stopReceiving();

            bool isOpen() const;

        private:
            void receiveLoop();

            serial::Serial serial_;           // 串口对象
            FrameCallbackFunc callback_;      // 用户回调
            std::atomic<bool> running_;       // 线程运行标志
            std::thread receive_thread_;      // 接收线程
    };

}  // namespace drivers