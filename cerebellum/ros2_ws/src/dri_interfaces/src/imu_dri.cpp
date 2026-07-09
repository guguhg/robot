#include "dri_interfaces/imu_dri.hpp"

namespace dri_interfaces {

// ============ 构造函数 ============
IMUDriver::IMUDriver(const rclcpp::NodeOptions& options)
    : Node("imu_driver", options)
{
    RCLCPP_INFO(this->get_logger(), "=== IMUDriver Starting ===");

    loadConfig();
    loadAxisMapping();
    initROS();

    RCLCPP_INFO(this->get_logger(), "IMUDriver initialized");
    RCLCPP_INFO(this->get_logger(), "topic: %s, rate: %dHz, frame_id: %s",
                config_.topic_name.c_str(), config_.publish_rate, config_.frame_id.c_str());
}

// ============ 加载配置 ============
void IMUDriver::loadConfig()
{
    try {
        YAML::Node config = common::ConfigLoader::loadDefault();
        auto imu_config = config["drivers"]["imu"];

        auto topics = imu_config["topics"];
        if (topics) {
            config_.topic_name = topics["data_raw"].as<std::string>("/imu/data_raw");
        }

        config_.publish_rate = imu_config["publish_rate"].as<int>(100);
        config_.frame_id = imu_config["frame_id"].as<std::string>("imu_link");

        auto axis_mapping = imu_config["axis_mapping"];
        if (axis_mapping) {
            config_.front = axis_mapping["front"].as<std::string>("x");
            config_.left = axis_mapping["left"].as<std::string>("y");
            config_.up = axis_mapping["up"].as<std::string>("z");
        }

        if (config_.publish_rate < 1) config_.publish_rate = 1;
        if (config_.publish_rate > 500) config_.publish_rate = 500;

        RCLCPP_INFO(this->get_logger(), "Config loaded: topic=%s, rate=%dHz",
                    config_.topic_name.c_str(), config_.publish_rate);
    } catch (const std::exception& e) {
        RCLCPP_WARN(this->get_logger(), "Failed to load config: %s", e.what());
        RCLCPP_WARN(this->get_logger(), "Using default values");
    }
}

// ============ 解析轴名称 ============
int IMUDriver::parseAxisName(const std::string& name, float& sign)
{
    sign = 1.0f;
    std::string axis = name;

    if (axis[0] == '-') {
        sign = -1.0f;
        axis = axis.substr(1);
    }

    if (axis == "x" || axis == "X") return 0;
    if (axis == "y" || axis == "Y") return 1;
    if (axis == "z" || axis == "Z") return 2;

    RCLCPP_WARN(this->get_logger(), "Unknown axis: '%s', using x", name.c_str());
    return 0;
}

// ============ 加载轴映射 ============
void IMUDriver::loadAxisMapping()
{
    axis_map_.front_idx = parseAxisName(config_.front, axis_map_.front_sign);
    axis_map_.left_idx = parseAxisName(config_.left, axis_map_.left_sign);
    axis_map_.up_idx = parseAxisName(config_.up, axis_map_.up_sign);
}

// ============ 映射单个轴 ============
float IMUDriver::mapAxis(const float values[3], int idx, float sign)
{
    return values[idx] * sign;
}

// ============ 初始化 ROS2 ============
void IMUDriver::initROS()
{
    imu_pub_ = this->create_publisher<sensor_msgs::msg::Imu>(
        config_.topic_name, 10);

    int interval_ms = 1000 / config_.publish_rate;
    publish_timer_ = this->create_wall_timer(
        std::chrono::milliseconds(interval_ms),
        std::bind(&IMUDriver::publishIMU, this));

    RCLCPP_INFO(this->get_logger(), "ROS2 initialized (publish: %dHz)", config_.publish_rate);
}

// ============ 发布 IMU 数据 ============
void IMUDriver::publishIMU()
{
    auto imu_data = drivers::IMU::getImuData();

    // 驱动层已通过 valid 处理超时
    if (!imu_data.valid) {
        return;
    }

    auto msg = convertToROSMsg(imu_data);
    imu_pub_->publish(msg);
}

// ============ 转换为 ROS 消息（坐标系校准） ============
sensor_msgs::msg::Imu IMUDriver::convertToROSMsg(const drivers::IMUData& data)
{
    sensor_msgs::msg::Imu msg;

    msg.header.stamp = this->now();
    msg.header.frame_id = config_.frame_id;

    float accel_raw[3] = {data.accel_x, data.accel_y, data.accel_z};
    float gyro_raw[3] = {data.gyro_x, data.gyro_y, data.gyro_z};

    // 坐标系校准：映射到 ROS2 标准 (X向前, Y向左, Z向上)
    msg.linear_acceleration.x = mapAxis(accel_raw, axis_map_.front_idx, axis_map_.front_sign);
    msg.linear_acceleration.y = mapAxis(accel_raw, axis_map_.left_idx, axis_map_.left_sign);
    msg.linear_acceleration.z = mapAxis(accel_raw, axis_map_.up_idx, axis_map_.up_sign);

    msg.angular_velocity.x = mapAxis(gyro_raw, axis_map_.front_idx, axis_map_.front_sign);
    msg.angular_velocity.y = mapAxis(gyro_raw, axis_map_.left_idx, axis_map_.left_sign);
    msg.angular_velocity.z = mapAxis(gyro_raw, axis_map_.up_idx, axis_map_.up_sign);

    // 协方差矩阵（暂设为 0）
    for (int i = 0; i < 9; ++i) {
        msg.linear_acceleration_covariance[i] = 0.0;
        msg.angular_velocity_covariance[i] = 0.0;
        msg.orientation_covariance[i] = 0.0;
    }

    return msg;
}

}  // namespace dri_interfaces

// int main(int argc, char** argv)
// {
//     rclcpp::init(argc, argv);
//     auto node = std::make_shared<dri_interfaces::IMUDriver>();
//     rclcpp::spin(node);
//     rclcpp::shutdown();
//     return 0;
// }