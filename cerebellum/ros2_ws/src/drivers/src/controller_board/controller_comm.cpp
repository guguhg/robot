#include "drivers/controller_board/controller_comm.h"
#include <iostream>
#include <chrono>

namespace drivers {
    
ControllerComm::ControllerComm(const std::string& port, 
                                int baudrate,
                                FrameCallbackFunc callback)
    : callback_(callback), running_(false) {
    
    try {
        // 配置串口参数
        serial_.setPort(port);
        serial_.setBaudrate(baudrate);
        
        // 设置超时：读超时100ms，写超时100ms
        serial::Timeout timeout = serial::Timeout::simpleTimeout(100);
        serial_.setTimeout(timeout);
        
        // 设置数据位、停止位、校验位（使用常用默认值）
        serial_.setBytesize(serial::eightbits);
        serial_.setParity(serial::parity_none);
        serial_.setStopbits(serial::stopbits_one);
        serial_.setFlowcontrol(serial::flowcontrol_none);
        
        // 打开串口
        serial_.open();
        
        if (serial_.isOpen()) {
            std::cout << "Serial port " << port << " opened successfully!" << std::endl;
        } else {
            std::cerr << "Failed to open serial port " << port << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Serial port initialization error: " << e.what() << std::endl;
    }
}

// ============ 析构函数 ============
ControllerComm::~ControllerComm() {
    stopReceiving();  // 确保线程停止
    if (serial_.isOpen()) {
        serial_.close();
    }
}

// ============ 启动接收线程 ============
void ControllerComm::startReceiving() {
    if (running_) return;  // 已经启动
    
    if (!serial_.isOpen()) {
        std::cerr << "Cannot start receiving: serial port is not open!" << std::endl;
        return;
    }
    
    running_ = true;
    receive_thread_ = std::thread(&ControllerComm::receiveLoop, this);
    std::cout << "Receiving thread started." << std::endl;
}

// ============ 停止接收线程 ============
void ControllerComm::stopReceiving() {
    if (running_) {
        running_ = false;  // 通知线程退出
        if (receive_thread_.joinable()) {
            receive_thread_.join();  // 等待线程完全退出
        }
        std::cout << "Receiving thread stopped." << std::endl;
    }
}

// ============ 接收线程主循环 ============
void ControllerComm::receiveLoop() {
    while (running_) {
        try {
            // 检查串口是否还开着
            if (!serial_.isOpen()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            
            // 读取数据（阻塞读，超时由timeout控制）
            std::vector<uint8_t> buffer;
            size_t bytes_read = serial_.read(buffer, 1024);  // 每次最多读1024字节
            
            if (bytes_read > 0) {
                // 有数据时，调用回调函数
                if (callback_) {
                    callback_(buffer);
                }
            }
        } catch (const serial::IOException& e) {
            // 串口读取异常，可能是设备断开
            std::cerr << "Serial read error: " << e.what() << std::endl;
            // 可以选择重试或退出
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        } catch (const std::exception& e) {
            std::cerr << "Unexpected error in receive loop: " << e.what() << std::endl;
        }
    }
}

// ============ 发送数据（vector版本） ============
bool ControllerComm::send(const std::vector<uint8_t>& data) {
    if (!serial_.isOpen()) {
        std::cerr << "Serial port is not open!" << std::endl;
        return false;
    }
    
    try {
        size_t bytes_written = serial_.write(data);
        return bytes_written == data.size();
    } catch (const std::exception& e) {
        std::cerr << "Send data error: " << e.what() << std::endl;
        return false;
    }
}

// ============ 发送数据（string版本） ============
bool ControllerComm::send(const std::string& data) {
    std::vector<uint8_t> vec(data.begin(), data.end());
    return send(vec);
}

// ============ 检查串口是否打开 ============
bool ControllerComm::isOpen() const {
    return serial_.isOpen();
}


}  // namespace drivers
