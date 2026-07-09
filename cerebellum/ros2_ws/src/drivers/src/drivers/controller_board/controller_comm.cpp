#include "drivers/controller_board/controller_comm.h"
#include "common/logger/logger.hpp"
#include <iostream>
#include <chrono>

namespace drivers
{

    /**
     * @brief 串口构造函数
     *
     * @param port 端口路径，如"/dev/ttyACM0"
     * @param baudrate 波特率
     * @param callback 接收回调函数
     */
    ControllerComm::ControllerComm(const std::string &port,
                                   int baudrate,
                                   int timeout,
                                   FrameCallbackFunc callback)
        : callback_(callback), running_(false)
    {
        // 打开串口
        try
        {
            serial_.setPort(port);
            serial_.setBaudrate(baudrate);

            serial::Timeout s_timeout = serial::Timeout::simpleTimeout(timeout);
            serial_.setTimeout(s_timeout);

            serial_.setBytesize(serial::eightbits);
            serial_.setParity(serial::parity_none);
            serial_.setStopbits(serial::stopbits_one);
            serial_.setFlowcontrol(serial::flowcontrol_none);

            serial_.open();

            if (serial_.isOpen())
            {
                LOG_INFO("Serial port %s opened successfully!\n", port.c_str());
            }
            else
            {
                LOG_ERROR("Failed to open serial port %s\n", port.c_str());
            }
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Serial port initialization error: %s\n", e.what());
        }
    }

    /**
     * @brief 析构函数，停止接收线程，关闭串口
     *
     */
    ControllerComm::~ControllerComm()
    {
        stopReceiving();
        if (serial_.isOpen())
        {
            serial_.close();
        }
    }

    /**
     * @brief 开始接收线程
     *
     */
    void ControllerComm::startReceiving()
    {
        // 原子量，线程运行标志
        if (running_)
            return;

        // 串口已关闭
        if (!serial_.isOpen())
        {
            LOG_ERROR("Cannot start receiving: serial port is not open!\n");
            return;
        }

        // 线程启动
        running_ = true;
        receive_thread_ = std::thread(&ControllerComm::receiveLoop, this); // 线程创建后，处于 "joinable" 状态
        // joinable	线程正在运行，父线程可以等待它结束
        // detached	线程已分离，独立运行，父线程不再管理
        LOG_DEBUG("Receiving thread started.\n");
    }

    /**
     * @brief 停止接收线程
     *
     */
    void ControllerComm::stopReceiving()
    {
        if (running_)
        {
            running_ = false;
            if (receive_thread_.joinable()) // 线程运行中，未被join(同步，受父线程控制)/detach(分离，异步，独立线程)，检查线程能否被阻塞
            {
                receive_thread_.join(); // 阻塞主线程，直到receive_thread_运行完成退出
            }
            LOG_DEBUG("Receiving thread stopped.\n");
        }
    }

    /**
     * @brief 接收主循环
     *
     */
    void ControllerComm::receiveLoop()
    {
        while (running_)
        {
            try
            {
                // 串口未打开，等一会刷新看看
                if (!serial_.isOpen())
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }

                // 一次最大读1KB
                std::vector<uint8_t> buffer;
                size_t bytes_read = serial_.read(buffer, 1024);

                // 读取成功，回调处理
                if (bytes_read > 0)
                {
                    if (callback_)
                    {
                        callback_(buffer);
                    }
                }
            }
            catch (const serial::IOException &e)
            {
                LOG_ERROR("Serial read error: %s\n", e.what());
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            catch (const std::exception &e)
            {
                LOG_ERROR("Unexpected error in receive loop: %s\n", e.what());
            }
        }
    }

    /**
     * @brief 发送字节流
     *
     * @param data 二进制字节流
     * @return true 发送成功
     * @return false 发送失败，串口未打开或捕获到异常
     */
    bool ControllerComm::send(const std::vector<uint8_t> &data)
    {
        if (!serial_.isOpen())
        {
            LOG_ERROR("Serial port is not open!\n");
            return false;
        }

        try
        {
            size_t bytes_written = serial_.write(data);
            return bytes_written == data.size();
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Send data error: %s\n", e.what());
            return false;
        }
    }

    /**
     * @brief 发送字符串
     *
     * @param data 字符串
     * @return true 发送成功
     * @return false 发送失败
     */
    bool ControllerComm::send(const std::string &data)
    {
        std::vector<uint8_t> vec(data.begin(), data.end());
        return send(vec);
    }

    /**
     * @brief 查询串口是否打开
     *
     * @return true 已打开
     * @return false 已关闭
     */
    bool ControllerComm::isOpen() const
    {
        return serial_.isOpen();
    }

} // namespace drivers