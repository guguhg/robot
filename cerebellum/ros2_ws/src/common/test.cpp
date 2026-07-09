#include "common/config_loader/config_loader.hpp"
#include "common/logger/logger.hpp"
#include <iostream>

int main()
{
    try
    {
        // ============ 默认配置日志 ============
        auto &logger = common::Logger::getInstance();

        LOG_INFO("=== Logger Test ===");
        LOG_DEBUG("This is a debug message");
        LOG_INFO("This is an info message");
        LOG_WARN("This is a warning message");
        LOG_ERROR("This is an error message");
        LOG_FATAL("This is a fatal message");

        // ============ 手动配置日志 ============
        logger.setLevel(common::LogLevel::DEBUG);
        logger.setConsoleOutput(true);
        logger.setLogFile("/tmp/robot_test.log");

        LOG_INFO("=== manual Logger Test ===");
        LOG_DEBUG("This is a manual debug message");
        LOG_INFO("This is an manual info message");
        LOG_WARN("This is a manual warning message");
        LOG_ERROR("This is an manual error message");
        LOG_FATAL("This is a manual fatal message");

        // ============ 测试条件日志 ============
        bool debug_mode = true;
        LOG_DEBUG_IF(debug_mode, "Debug mode is enabled");
        LOG_INFO_IF(!debug_mode, "Debug mode is disabled");

        // ============ 测试格式化 ============
        int value = 42;
        std::string name = "robot";
        LOG_INFO("Value: %d, Name: %s", value, name.c_str());

        // ============ 加载配置 ============
        LOG_INFO("=== Config Test ===");
        YAML::Node config = common::ConfigLoader::loadDefault();
        common::ConfigLoader::dump(config, "Full Config");

        std::string port = config["drivers"]["controller_board"]["serial"]["port"].as<std::string>();
        int baudrate = config["drivers"]["controller_board"]["serial"]["baudrate"].as<int>();

        LOG_INFO("Port: %s, Baudrate: %d", port.c_str(), baudrate);

        LOG_INFO("=== Test Completed ===");
    }
    catch (const std::exception &e)
    {
        LOG_FATAL("Test failed: %s", e.what());
        return 1;
    }

    return 0;
}