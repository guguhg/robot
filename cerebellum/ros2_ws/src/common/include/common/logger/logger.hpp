#pragma once

#include <string>
#include <sstream>
#include <iostream>
#include <mutex>
#include <fstream>
#include <cstdarg>

namespace common {

/**
 * @brief 日志级别（从低到高）
 */
enum class LogLevel : uint8_t {
    DEBUG = 0,
    INFO  = 1,
    WARN  = 2,
    ERROR = 3,
    FATAL = 4,
    NONE  = 5
};

/**
 * @brief 日志系统（纯 C++，不依赖 ROS2）
 * 
 * 用法:
 *   LOG_DEBUG("This is debug: %d", value);
 *   LOG_INFO("Serial port opened: %s", port.c_str());
 *   LOG_WARN("Battery low: %d mV", voltage);
 *   LOG_ERROR("Failed to open serial port!");
 *   LOG_FATAL("Critical error!");
 * 
 *   LOG_INFO_IF(debug_enabled, "Debug mode enabled");
 */
class Logger {
public:
    /**
     * @brief 获取单例实例
     */
    static Logger& getInstance();

    /**
     * @brief 设置日志级别（低于该级别的日志不会输出）
     */
    void setLevel(LogLevel level);

    /**
     * @brief 获取当前日志级别
     */
    LogLevel getLevel() const;

    /**
     * @brief 设置是否输出到控制台
     */
    void setConsoleOutput(bool enabled);

    /**
     * @brief 设置日志文件路径（空字符串表示不输出到文件）
     */
    void setLogFile(const std::string& file_path);

    /**
     * @brief 获取日志级别名称
     */
    static const char* levelToString(LogLevel level);

    /**
     * @brief 核心日志函数（printf 风格）
     * @param level 日志级别
     * @param file 文件名（由 __FILE__ 宏传入）
     * @param line 行号（由 __LINE__ 宏传入）
     * @param format 格式化字符串
     * @param ... 可变参数
     */
    void log(LogLevel level, const char* file, int line, const char* format, ...);

private:
    //私有构造、析构、禁止拷贝，确保日志句柄唯一性
    Logger();
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void writeToConsole(LogLevel level, const std::string& message);//输出到终端
    void writeToFile(const std::string& message);//输出到文件
    void initLogFile();//初始化日志文件

    LogLevel level_;//日志等级
    bool console_enabled_;//输出到终端标志
    std::string log_file_path_;//日志文件路径
    std::mutex mutex_;//资源互斥锁,保护私有资源
    bool file_initialized_;//判断文件是否能够打开标志位
};

}  // namespace common

// ============ ANSI 颜色宏 ============

#define LOG_COLOR_RESET   "\033[0m"
#define LOG_COLOR_BLACK   "\033[30m"
#define LOG_COLOR_RED     "\033[31m"
#define LOG_COLOR_GREEN   "\033[32m"
#define LOG_COLOR_YELLOW  "\033[33m"
#define LOG_COLOR_BLUE    "\033[34m"
#define LOG_COLOR_MAGENTA "\033[35m"
#define LOG_COLOR_CYAN    "\033[36m"
#define LOG_COLOR_WHITE   "\033[37m"
#define LOG_COLOR_GRAY    "\033[90m"

// ============ 日志宏（ROS2 风格） ============

/**
 * @brief 调试日志（灰色）
 */
#define LOG_DEBUG(...)   common::Logger::getInstance().log(common::LogLevel::DEBUG, __FILE__, __LINE__, __VA_ARGS__)

/**
 * @brief 信息日志（绿色）
 */
#define LOG_INFO(...)    common::Logger::getInstance().log(common::LogLevel::INFO,  __FILE__, __LINE__, __VA_ARGS__)

/**
 * @brief 警告日志（黄色）
 */
#define LOG_WARN(...)    common::Logger::getInstance().log(common::LogLevel::WARN,  __FILE__, __LINE__, __VA_ARGS__)

/**
 * @brief 错误日志（红色）
 */
#define LOG_ERROR(...)   common::Logger::getInstance().log(common::LogLevel::ERROR, __FILE__, __LINE__, __VA_ARGS__)

/**
 * @brief 致命日志（红色，程序可能终止）
 */
#define LOG_FATAL(...)   common::Logger::getInstance().log(common::LogLevel::FATAL, __FILE__, __LINE__, __VA_ARGS__)

// ============ 条件日志 ============

#define LOG_DEBUG_IF(cond, ...) if (cond) LOG_DEBUG(__VA_ARGS__)
#define LOG_INFO_IF(cond, ...)  if (cond) LOG_INFO(__VA_ARGS__)
#define LOG_WARN_IF(cond, ...)  if (cond) LOG_WARN(__VA_ARGS__)
#define LOG_ERROR_IF(cond, ...) if (cond) LOG_ERROR(__VA_ARGS__)
#define LOG_FATAL_IF(cond, ...) if (cond) LOG_FATAL(__VA_ARGS__)

// ============ 每 N 次日志 ============

#define LOG_DEBUG_N(n, ...) { static int _count = 0; if (++_count % (n) == 0) LOG_DEBUG(__VA_ARGS__); }
#define LOG_INFO_N(n, ...)  { static int _count = 0; if (++_count % (n) == 0) LOG_INFO(__VA_ARGS__); }
#define LOG_WARN_N(n, ...)  { static int _count = 0; if (++_count % (n) == 0) LOG_WARN(__VA_ARGS__); }
#define LOG_ERROR_N(n, ...) { static int _count = 0; if (++_count % (n) == 0) LOG_ERROR(__VA_ARGS__); }