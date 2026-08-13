/**
 * @file imu_dri.cpp
 * @brief IMU 驱动接口节点实现
 * 
 * 数据流:
 *   drivers::IMU → 单位转换 → 零漂修正 → 互补滤波 → 发布 /imu/data_raw
 * 
 * 轴映射由 URDF 的 imu_joint 完成，本节点不做轴映射。
 */

#include "dri_interfaces/imu_dri.hpp"

#include <cmath>
#include <algorithm>

namespace dri_interfaces {

// ============================================================================
// 构造函数
// ============================================================================

IMUDriver::IMUDriver(const rclcpp::NodeOptions& options)
    : Node("imu_driver", options)
{
    RCLCPP_INFO(this->get_logger(), "=== IMUDriver Starting (with complementary filter) ===");

    loadConfig();
    initROS();

    RCLCPP_INFO(this->get_logger(), "IMUDriver initialized");
    RCLCPP_INFO(this->get_logger(), "  topic:      %s", config_.topic_name.c_str());
    RCLCPP_INFO(this->get_logger(), "  rate:       %d Hz", config_.publish_rate);
    RCLCPP_INFO(this->get_logger(), "  frame_id:   %s", config_.frame_id.c_str());
    RCLCPP_INFO(this->get_logger(), "  accel_unit: %s", config_.accel_unit == 0 ? "g → m/s²" : "m/s² (透传)");
    RCLCPP_INFO(this->get_logger(), "  gyro_unit:  %s", config_.gyro_unit == 0 ? "deg/s → rad/s" : "rad/s (透传)");
    RCLCPP_INFO(this->get_logger(), "  alpha:      %.2f", config_.alpha);
    RCLCPP_INFO(this->get_logger(), "  bias:       [%.4f, %.4f, %.4f]",
                config_.bias.x(), config_.bias.y(), config_.bias.z());
}


// ============================================================================
// loadConfig()
// ============================================================================

void IMUDriver::loadConfig()
{
    try
    {
        YAML::Node config = common::ConfigLoader::loadDefault();
        auto imu_config = config["drivers"]["imu"];

        if (imu_config)
        {
            auto topics = imu_config["topics"];
            if (topics)
            {
                config_.topic_name = topics["data_raw"].as<std::string>("/imu/data_raw");
            }

            config_.publish_rate = imu_config["publish_rate"].as<int>(100);
            config_.frame_id = imu_config["frame_id"].as<std::string>("imu_link");
            config_.accel_unit = imu_config["accel_unit"].as<int>(0);
            config_.gyro_unit = imu_config["gyro_unit"].as<int>(0);
            config_.alpha = imu_config["alpha"].as<float>(0.98f);

            // 加载 bias
            auto bias_node = imu_config["bias"];
            if (bias_node && bias_node.IsSequence() && bias_node.size() == 3)
            {
                config_.bias.x() = bias_node[0].as<float>(0.0f);
                config_.bias.y() = bias_node[1].as<float>(0.0f);
                config_.bias.z() = bias_node[2].as<float>(0.0f);
            }

            // 参数校验
            if (config_.accel_unit != 0 && config_.accel_unit != 1)
            {
                RCLCPP_WARN(this->get_logger(), "accel_unit=%d 无效，使用 0", config_.accel_unit);
                config_.accel_unit = 0;
            }
            if (config_.gyro_unit != 0 && config_.gyro_unit != 1)
            {
                RCLCPP_WARN(this->get_logger(), "gyro_unit=%d 无效，使用 0", config_.gyro_unit);
                config_.gyro_unit = 0;
            }
            if (config_.publish_rate < 1) config_.publish_rate = 1;
            if (config_.publish_rate > 500) config_.publish_rate = 500;
            config_.alpha = std::clamp(config_.alpha, 0.0f, 1.0f);

            RCLCPP_INFO(this->get_logger(), "Config loaded successfully");
        }
        else
        {
            RCLCPP_WARN(this->get_logger(), "No 'drivers.imu' config, using defaults");
        }
    }
    catch (const std::exception& e)
    {
        RCLCPP_WARN(this->get_logger(), "Failed to load config: %s", e.what());
    }
}


// ============================================================================
// initROS()
// ============================================================================

void IMUDriver::initROS()
{
    imu_pub_ = this->create_publisher<sensor_msgs::msg::Imu>(
        config_.topic_name, 10);

    int interval_ms = 1000 / config_.publish_rate;
    publish_timer_ = this->create_wall_timer(
        std::chrono::milliseconds(interval_ms),
        std::bind(&IMUDriver::publishIMU, this));

    RCLCPP_INFO(this->get_logger(), "ROS2 initialized (publishing at %d Hz)", config_.publish_rate);
}


// ============================================================================
// readIMUData()
// ============================================================================

bool IMUDriver::readIMUData(drivers::IMUData& data)
{
    data = drivers::IMU::getImuData();
    return data.valid;
}


// ============================================================================
// publishIMU()
// ============================================================================

void IMUDriver::publishIMU()
{
    drivers::IMUData imu_data;
    if (!readIMUData(imu_data) || !imu_data.valid)
    {
        RCLCPP_DEBUG(this->get_logger(), "IMU data invalid, skipping publish");
        return;
    }

    auto msg = convertToROSMsg(imu_data);
    imu_pub_->publish(msg);
}


// ============================================================================
// convertToROSMsg()
// ============================================================================

sensor_msgs::msg::Imu IMUDriver::convertToROSMsg(const drivers::IMUData& data)
{
    sensor_msgs::msg::Imu msg;

    msg.header.stamp = this->now();
    msg.header.frame_id = config_.frame_id;

    // ---- 1. 原始数据 ----
    float ax = data.accel_x;
    float ay = data.accel_y;
    float az = data.accel_z;
    float gx = data.gyro_x;
    float gy = data.gyro_y;
    float gz = data.gyro_z;

    // ---- 2. 单位转换 ----
    const float G_TO_MS2 = 9.80665f;
    if (config_.accel_unit == 0)
    {
        ax *= G_TO_MS2;
        ay *= G_TO_MS2;
        az *= G_TO_MS2;
    }

    const float DEG_TO_RAD = M_PI / 180.0f;
    if (config_.gyro_unit == 0)
    {
        gx *= DEG_TO_RAD;
        gy *= DEG_TO_RAD;
        gz *= DEG_TO_RAD;
    }

    // ---- 3. 零漂修正 ----
    gx -= config_.bias.x();
    gy -= config_.bias.y();
    gz -= config_.bias.z();

    // ---- 4. 互补滤波 (每帧更新) ----
    double dt = 0.01;  // 默认 10ms
    static double last_time = 0;
    double now = this->now().seconds();
    if (last_time > 0)
    {
        dt = std::clamp(now - last_time, 0.001, 0.1);
    }
    last_time = now;

    updateFilter(ax, ay, az, gx, gy, gz, static_cast<float>(dt));

    // ---- 5. 填充消息 ----
    // 四元数 (由互补滤波计算)
    msg.orientation.x = quat_.x();
    msg.orientation.y = quat_.y();
    msg.orientation.z = quat_.z();
    msg.orientation.w = quat_.w();

    // 加速度
    msg.linear_acceleration.x = ax;
    msg.linear_acceleration.y = ay;
    msg.linear_acceleration.z = az;

    // 角速度 (补偿后)
    msg.angular_velocity.x = gx;
    msg.angular_velocity.y = gy;
    msg.angular_velocity.z = gz;

    // 协方差
    for (int i = 0; i < 9; ++i)
    {
        msg.orientation_covariance[i] = 0.0;
        msg.linear_acceleration_covariance[i] = 0.0;
        msg.angular_velocity_covariance[i] = 0.0;
    }

    return msg;
}


// ============================================================================
// 互补滤波
// ============================================================================

void IMUDriver::updateFilter(float ax, float ay, float az,
                              float gx, float gy, float gz,
                              float dt)
{
    std::lock_guard<std::mutex> lock(mutex_);

    // ---- 加速度归一化 ----
    Eigen::Vector3f accel(ax, ay, az);
    float accel_norm = accel.norm();
    if (accel_norm < 0.001f) return;
    accel.normalize();

    // ---- 陀螺仪积分 ----
    Eigen::Vector3f gyro(gx, gy, gz);
    Eigen::Quaternionf q_gyro = gyroUpdate(quat_, gyro, dt);

    // ---- 加速度计估算 ----
    Eigen::Quaternionf q_accel = accelUpdate(accel);

    // ---- 互补滤波融合 ----
    if (initialized_)
    {
        quat_ = complementaryFilter(q_gyro, q_accel);
    }
    else
    {
        quat_ = q_accel;
        initialized_ = true;
    }

    quat_.normalize();
}


Eigen::Quaternionf IMUDriver::gyroUpdate(const Eigen::Quaternionf& q,
                                          const Eigen::Vector3f& gyro,
                                          float dt)
{
    Eigen::Quaternionf q_delta;
    q_delta.x() = gyro.x() * dt / 2.0f;
    q_delta.y() = gyro.y() * dt / 2.0f;
    q_delta.z() = gyro.z() * dt / 2.0f;
    q_delta.w() = 1.0f;

    Eigen::Quaternionf result = q * q_delta;
    result.normalize();
    return result;
}


Eigen::Quaternionf IMUDriver::accelUpdate(const Eigen::Vector3f& accel)
{
    // 参考重力方向: (0, 0, 1) (加速度计测量的是重力的反方向)
    Eigen::Vector3f gravity(0.0f, 0.0f, 1.0f);
    Eigen::Vector3f axis = gravity.cross(accel);
    float angle = std::acos(std::clamp(gravity.dot(accel), -1.0f, 1.0f));

    if (axis.norm() < 0.001f)
    {
        return Eigen::Quaternionf::Identity();
    }

    axis.normalize();
    Eigen::Quaternionf q(Eigen::AngleAxisf(angle, axis));
    q.normalize();
    return q;
}


Eigen::Quaternionf IMUDriver::complementaryFilter(const Eigen::Quaternionf& q_gyro,
                                                   const Eigen::Quaternionf& q_accel)
{
    float alpha = config_.alpha;
    Eigen::Quaternionf result = q_gyro.slerp(1.0f - alpha, q_accel);
    result.normalize();
    return result;
}

}  // namespace dri_interfaces