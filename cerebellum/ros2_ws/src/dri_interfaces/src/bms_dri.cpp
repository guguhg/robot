#include "dri_interfaces/bms_dri.hpp"

namespace dri_interfaces {

// ============ 构造函数 ============
BMSDriver::BMSDriver(const rclcpp::NodeOptions& options)
    : Node("bms_driver", options)
{
    RCLCPP_INFO(this->get_logger(), "=== BMSDriver Starting ===");

    loadConfig();
    initROS();

    RCLCPP_INFO(this->get_logger(), "BMSDriver initialized");
    RCLCPP_INFO(this->get_logger(), "voltage_topic: %s, soc_topic: %s, rate: %dHz",
                config_.voltage_topic.c_str(), config_.soc_topic.c_str(), config_.publish_rate);
}

// ============ 加载配置 ============
void BMSDriver::loadConfig()
{
    try {
        YAML::Node config = common::ConfigLoader::loadDefault();
        auto bms_config = config["drivers"]["bms"];

        auto topics = bms_config["topics"];
        if (topics) {
            config_.voltage_topic = topics["voltage"].as<std::string>("/bms/voltage");
            config_.soc_topic = topics["soc"].as<std::string>("/bms/soc");
        }

        config_.publish_rate = bms_config["publish_rate"].as<int>(10);

        if (config_.publish_rate < 1) config_.publish_rate = 1;
        if (config_.publish_rate > 100) config_.publish_rate = 100;

        RCLCPP_INFO(this->get_logger(), "Config loaded: rate=%dHz", config_.publish_rate);
    } catch (const std::exception& e) {
        RCLCPP_WARN(this->get_logger(), "Failed to load config: %s", e.what());
        RCLCPP_WARN(this->get_logger(), "Using default values");
    }
}

// ============ 初始化 ROS2 ============
void BMSDriver::initROS()
{
    voltage_pub_ = this->create_publisher<std_msgs::msg::Float32>(
        config_.voltage_topic, 10);

    soc_pub_ = this->create_publisher<std_msgs::msg::Float32>(
        config_.soc_topic, 10);

    int interval_ms = 1000 / config_.publish_rate;
    publish_timer_ = this->create_wall_timer(
        std::chrono::milliseconds(interval_ms),
        std::bind(&BMSDriver::publishBMS, this));

    RCLCPP_INFO(this->get_logger(), "ROS2 initialized (publish: %dHz)", config_.publish_rate);
}

// ============ 发布 BMS 数据 ============
void BMSDriver::publishBMS()
{
    auto bms_data = drivers::BMS::getBmsData();

    // 驱动层已通过 valid 处理超时
    if (!bms_data.valid) {
        return;
    }

    // 发布电压 (mV → V)
    auto voltage_msg = std_msgs::msg::Float32();
    voltage_msg.data = bms_data.voltage_mv / 1000.0f;
    voltage_pub_->publish(voltage_msg);

    // 发布电量 (%)
    auto soc_msg = std_msgs::msg::Float32();
    soc_msg.data = static_cast<float>(bms_data.soc);
    soc_pub_->publish(soc_msg);
}

}  // namespace dri_interfaces

// int main(int argc, char** argv)
// {
//     rclcpp::init(argc, argv);
//     auto node = std::make_shared<dri_interfaces::BMSDriver>();
//     rclcpp::spin(node);
//     rclcpp::shutdown();
//     return 0;
// }