#include "drivers/controller_board/controller_comm.h"
#include <iostream>
#include <chrono>

namespace drivers
{

    ControllerComm::ControllerComm(const std::string &port,
                                   int baudrate,
                                   FrameCallbackFunc callback)
        : callback_(callback), running_(false)
    {

        try
        {
            serial_.setPort(port);
            serial_.setBaudrate(baudrate);

            serial::Timeout timeout = serial::Timeout::simpleTimeout(100);
            serial_.setTimeout(timeout);

            serial_.setBytesize(serial::eightbits);
            serial_.setParity(serial::parity_none);
            serial_.setStopbits(serial::stopbits_one);
            serial_.setFlowcontrol(serial::flowcontrol_none);

            serial_.open();

            if (serial_.isOpen())
            {
                std::cout << "Serial port " << port << " opened successfully!" << std::endl;
            }
            else
            {
                std::cerr << "Failed to open serial port " << port << std::endl;
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "Serial port initialization error: " << e.what() << std::endl;
        }
    }

    ControllerComm::~ControllerComm()
    {
        stopReceiving();
        if (serial_.isOpen())
        {
            serial_.close();
        }
    }

    void ControllerComm::startReceiving()
    {
        if (running_)
            return;

        if (!serial_.isOpen())
        {
            std::cerr << "Cannot start receiving: serial port is not open!" << std::endl;
            return;
        }

        running_ = true;
        receive_thread_ = std::thread(&ControllerComm::receiveLoop, this);
        std::cout << "Receiving thread started." << std::endl;
    }

    void ControllerComm::stopReceiving()
    {
        if (running_)
        {
            running_ = false;
            if (receive_thread_.joinable())
            {
                receive_thread_.join();
            }
            std::cout << "Receiving thread stopped." << std::endl;
        }
    }

    void ControllerComm::receiveLoop()
    {
        while (running_)
        {
            try
            {
                if (!serial_.isOpen())
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }

                std::vector<uint8_t> buffer;
                size_t bytes_read = serial_.read(buffer, 1024);

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
                std::cerr << "Serial read error: " << e.what() << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            catch (const std::exception &e)
            {
                std::cerr << "Unexpected error in receive loop: " << e.what() << std::endl;
            }
        }
    }

    bool ControllerComm::send(const std::vector<uint8_t> &data)
    {
        if (!serial_.isOpen())
        {
            std::cerr << "Serial port is not open!" << std::endl;
            return false;
        }

        try
        {
            size_t bytes_written = serial_.write(data);
            return bytes_written == data.size();
        }
        catch (const std::exception &e)
        {
            std::cerr << "Send data error: " << e.what() << std::endl;
            return false;
        }
    }

    bool ControllerComm::send(const std::string &data)
    {
        std::vector<uint8_t> vec(data.begin(), data.end());
        return send(vec);
    }

    bool ControllerComm::isOpen() const
    {
        return serial_.isOpen();
    }

} // namespace drivers