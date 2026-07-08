#include "common/logger/logger.hpp"
#include "common/config_loader/config_loader.hpp"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <fstream>
#include <cstdarg>
#include <cstdio>
#include <algorithm>
#include <cstring>

namespace common
{

    /**
     * @brief 颜色映射
     *
     * @param level 日志等级
     * @return ANSI 颜色宏
     */
    static const char *getLevelColor(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::DEBUG:
            return LOG_COLOR_GRAY;
        case LogLevel::INFO:
            return LOG_COLOR_GREEN;
        case LogLevel::WARN:
            return LOG_COLOR_YELLOW;
        case LogLevel::ERROR:
            return LOG_COLOR_RED;
        case LogLevel::FATAL:
            return LOG_COLOR_RED;
        default:
            return LOG_COLOR_RESET;
        }
    }

    /**
     * @brief 单例
     *
     * @return 单例引用
     */
    Logger &Logger::getInstance()
    {
        static Logger instance;
        return instance;
    }

    /**
     * @brief 构造函数，设置初始日志等级，是否输出到终端，是否初始化日志文件
     *
     */
    Logger::Logger()
        : level_(LogLevel::INFO), console_enabled_(true), file_initialized_(false)
    {
        try
        {
            YAML::Node logger_config = common::ConfigLoader::loadDefault()["common"]["logger"];

            // 读取级别（数字）
            int level_num = logger_config["level"] ? logger_config["level"].as<int>() : 1;
            setLevel(static_cast<LogLevel>(level_num));

            // 读取控制台输出
            bool console = logger_config["console"] ? logger_config["console"].as<bool>() : true;
            setConsoleOutput(console);

            // 读取文件路径
            std::string file_path = logger_config["file"] ? logger_config["file"].as<std::string>() : "";
            setLogFile(file_path);

            std::cout << "[Logger]" << std::endl;
            std::cout << "level_num:" << level_num << std::endl;
            std::cout << "console:" << console << std::endl;
            std::cout << "file_path:" << file_path << std::endl;
        }
        catch (const std::exception &e)
        {
            std::cerr << "[Logger] Failed to load config: " << e.what() << std::endl;
            std::cerr << "[Logger] Using default values." << std::endl;
        }
    }

    /**
     * @brief 析构函数，不需要额外清理
     *
     */
    Logger::~Logger()
    {
    }

    /**
     * @brief 设置日志等级，低于该值的日志不会输出
     *
     * @param level 日志等级
     */
    void Logger::setLevel(LogLevel level)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        level_ = level;
    }

    /**
     * @brief 获取日志等级
     *
     * @return 日志等级
     */
    LogLevel Logger::getLevel() const
    {
        return level_;
    }

    /**
     * @brief 设置是否输出到终端
     *
     * @param enabled true是    false否
     */
    void Logger::setConsoleOutput(bool enabled)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        console_enabled_ = enabled;
    }

    /**
     * @brief 设置输出到日志文件的路径
     *
     * @param file_path 文件路径
     */
    void Logger::setLogFile(const std::string &file_path)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        log_file_path_ = file_path;
        file_initialized_ = false;
    }

    /**
     * @brief 获取日志等级的名称字符串
     *
     * @param level 日志等级
     * @return const char* 名称字符串
     */
    const char *Logger::levelToString(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::DEBUG:
            return "DEBUG";
        case LogLevel::INFO:
            return "INFO";
        case LogLevel::WARN:
            return "WARN";
        case LogLevel::ERROR:
            return "ERROR";
        case LogLevel::FATAL:
            return "FATAL";
        default:
            return "UNKNOWN";
        }
    }

    /**
     * @brief 判断文件是否能够打开,并设置全局标志位
     *
     */
    void Logger::initLogFile()
    {
        if (file_initialized_)
            return;

        if (log_file_path_.empty())
        {
            file_initialized_ = true;
            return;
        }

        std::ofstream file(log_file_path_, std::ios::app);
        if (file.is_open())
        {
            file_initialized_ = true;
            file.close();
        }
        else
        {
            // 如果无法打开文件，输出到控制台
            std::cerr << "[Logger] Failed to open log file: " << log_file_path_ << std::endl;
            file_initialized_ = true;
        }
    }

    /**
     * @brief 格式化输出日志
     *
     * @param level 日志等级
     * @param file 当前运行的程序所在文件路径
     * @param line 行数
     * @param format 格式
     * @param ... 参数
     */
    void Logger::log(LogLevel level, const char *file, int line, const char *format, ...)
    {
        // 级别过滤
        if (level < level_)
        {
            return;
        }

        // 获取时间
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch()) %
                  1000;

        std::tm tm_buf;
        localtime_r(&time_t, &tm_buf);

        // 格式化消息
        va_list args;
        va_start(args, format);
        char buffer[2048];
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        // 提取文件名（去掉路径）
        const char *filename = strrchr(file, '/');
        if (filename)
        {
            filename++;
        }
        else
        {
            filename = file;
        }

        // 构建完整日志
        std::stringstream ss;
        ss << "[" << std::put_time(&tm_buf, "%H:%M:%S") << "."
           << std::setw(3) << std::setfill('0') << ms.count() << "] ";
        ss << "[" << levelToString(level) << "] ";
        ss << "[" << filename << ":" << line << "] ";
        ss << buffer;

        std::string message = ss.str();

        // 输出到控制台
        if (console_enabled_)
        {
            writeToConsole(level, message);
        }

        // 输出到文件
        if (!log_file_path_.empty())
        {
            writeToFile(message);
        }
    }

    /**
     * @brief 写入到控制台
     *
     * @param level 日志等级
     * @param message 日志信息
     */
    void Logger::writeToConsole(LogLevel level, const std::string &message)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        const char *color = getLevelColor(level);

        if (level >= LogLevel::ERROR)
        {
            std::cerr << color << message << LOG_COLOR_RESET << std::endl;
        }
        else
        {
            std::cout << color << message << LOG_COLOR_RESET << std::endl;
        }
    }

    /**
     * @brief 写入到文件
     *
     * @param message 日志信息
     */
    void Logger::writeToFile(const std::string &message)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        initLogFile(); // 判断文件是否能够打开

        if (!file_initialized_ || log_file_path_.empty())
        {
            return;
        }

        std::ofstream file(log_file_path_, std::ios::app);
        if (file.is_open())
        {
            file << message << std::endl;
        }
    }

} // namespace common