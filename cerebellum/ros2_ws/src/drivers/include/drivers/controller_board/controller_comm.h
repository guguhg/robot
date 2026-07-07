#pragma once

#include <serial.h>
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <vector>

namespace drivers
{
    /**
     * @brief 回调函数类型
     *
     */
    using FrameCallbackFunc = std::function<void(const std::vector<uint8_t> &)>;

    class ControllerComm
    {
    public:
        /**
         * @brief 串口构造函数，要求显式调用
         *
         * @param port 端口路径，如"/dev/ttyACM0"
         * @param baudrate 波特率
         * @param callback 接收回调函数
         */
        explicit ControllerComm(const std::string &port,
                                const int baudrate,
                                const int timeout,
                                FrameCallbackFunc callback = nullptr);

        /**
         * @brief 析构函数，停止接收线程，关闭串口
         *
         */
        ~ControllerComm();

        /**
         * @brief 禁止拷贝，确保硬件唯一性
         *
         */
        ControllerComm(const ControllerComm &) = delete;
        ControllerComm &operator=(const ControllerComm &) = delete;

        /**
         * @brief 发送字节流
         *
         * @param data 二进制字节流
         * @return true 发送成功
         * @return false 发送失败，串口未打开或捕获到异常
         */
        bool send(const std::vector<uint8_t> &data);

        /**
         * @brief 发送字符串
         *
         * @param data 字符串
         * @return true 发送成功
         * @return false 发送失败
         */
        bool send(const std::string &data);

        /**
         * @brief 开始接收线程
         *
         */
        void startReceiving();

        /**
         * @brief 停止接收线程
         *
         */
        void stopReceiving();

        /**
         * @brief 查询串口是否打开
         *
         * @return true 已打开
         * @return false 已关闭
         */
        bool isOpen() const;

    private:
        void receiveLoop(); // 接收线程主循环

        serial::Serial serial_;      // 串口实例
        FrameCallbackFunc callback_; // 接收处理回调函数
        std::atomic<bool> running_;  // 原子操作，线程运行标志位
        std::thread receive_thread_; // 接收线程实例
    };

} // namespace drivers