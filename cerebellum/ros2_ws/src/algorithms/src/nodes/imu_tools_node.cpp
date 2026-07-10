#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/imu.hpp"
#include "geometry_msgs/msg/quaternion.hpp"
#include "algorithms/imu_tools.hpp"
#include "common/config_loader/config_loader.hpp"
#include "std_srvs/srv/trigger.hpp"

#include <memory>
#include <string>
#include <thread>
#include <chrono>
#include <mutex>
#include <atomic>
#include <vector>

// 在节点中定义校准结果结构体
struct CalibrationResult {
    bool success = false;
    Eigen::Vector3f bias = Eigen::Vector3f::Zero();
    Eigen::Vector3f stddev = Eigen::Vector3f::Zero();
    int samples = 0;
    std::string message;
};

class IMUToolsNode : public rclcpp::Node
{
public:
    IMUToolsNode()
        : Node("imu_tools_node")
    {
        RCLCPP_INFO(this->get_logger(), "=== IMUToolsNode Starting ===");

        loadConfig();

        imu_sub_ = this->create_subscription<sensor_msgs::msg::Imu>(
            config_.input_topic, 10,
            std::bind(&IMUToolsNode::onIMU, this, std::placeholders::_1));

        imu_pub_ = this->create_publisher<sensor_msgs::msg::Imu>(
            config_.output_topic, 10);

        imu_tools_.setFilterCoefficient(config_.alpha);
        imu_tools_.setBiasCompensationEnabled(config_.bias_enabled);

        if (config_.bias_enabled && config_.preset_bias.has_value()) {
            imu_tools_.setPresetBias(config_.preset_bias.value());
            RCLCPP_INFO(this->get_logger(), "Using preset bias: x=%.4f, y=%.4f, z=%.4f rad/s",
                       config_.preset_bias.value().x(),
                       config_.preset_bias.value().y(),
                       config_.preset_bias.value().z());
        }

        calibrate_service_ = this->create_service<std_srvs::srv::Trigger>(
            "~/calibrate_gyro",
            std::bind(&IMUToolsNode::onCalibrate, this,
                      std::placeholders::_1, std::placeholders::_2));

        RCLCPP_INFO(this->get_logger(), "IMUToolsNode initialized");
        RCLCPP_INFO(this->get_logger(), "  input:  %s", config_.input_topic.c_str());
        RCLCPP_INFO(this->get_logger(), "  output: %s", config_.output_topic.c_str());
        RCLCPP_INFO(this->get_logger(), "  alpha:  %.2f", config_.alpha);
        RCLCPP_INFO(this->get_logger(), "  frame:  %s", config_.frame_id.c_str());
        RCLCPP_INFO(this->get_logger(), "  bias:   %s", config_.bias_enabled ? "enabled" : "disabled");
    }

private:
    struct Config {
        std::string input_topic = "/imu/data_raw";
        std::string output_topic = "/imu/data";
        std::string frame_id = "imu_link";
        float alpha = 0.98f;
        int publish_rate = 100;

        bool bias_enabled = true;
        bool auto_calibrate = false;
        int bias_samples = 200;
        float bias_stability_threshold = 0.05f;
        std::optional<Eigen::Vector3f> preset_bias;
    } config_;

    // 校准状态
    std::atomic<bool> calibrating_{false};
    std::atomic<bool> calib_done_{false};
    int target_samples_ = 200;
    std::vector<Eigen::Vector3f> calib_samples_;
    std::mutex calib_mutex_;
    CalibrationResult calib_result_;

    void loadConfig()
    {
        try {
            YAML::Node config = common::ConfigLoader::loadDefault();
            auto imu_tools_config = config["algorithms"]["imu_tools"];

            if (imu_tools_config) {
                auto topics = imu_tools_config["topics"];
                if (topics) {
                    config_.input_topic = topics["input"].as<std::string>("/imu/data_raw");
                    config_.output_topic = topics["output"].as<std::string>("/imu/data");
                }

                config_.alpha = imu_tools_config["alpha"].as<float>(0.98f);
                config_.frame_id = imu_tools_config["frame_id"].as<std::string>("imu_link");
                config_.publish_rate = imu_tools_config["publish_rate"].as<int>(100);

                auto bias_config = imu_tools_config["bias_calibration"];
                if (bias_config) {
                    config_.bias_enabled = bias_config["enabled"].as<bool>(true);
                    config_.auto_calibrate = bias_config["auto_calibrate"].as<bool>(false);
                    config_.bias_samples = bias_config["samples"].as<int>(200);
                    config_.bias_stability_threshold = bias_config["stability_threshold"].as<float>(0.05f);

                    auto preset = bias_config["preset_bias"];
                    if (preset) {
                        Eigen::Vector3f bias;
                        bias.x() = preset["x"].as<float>(0.0f);
                        bias.y() = preset["y"].as<float>(0.0f);
                        bias.z() = preset["z"].as<float>(0.0f);
                        if (bias.norm() > 0.001f) {
                            config_.preset_bias = bias;
                        }
                    }
                }

                config_.alpha = std::clamp(config_.alpha, 0.0f, 1.0f);
                config_.bias_samples = std::clamp(config_.bias_samples, 10, 1000);
                config_.bias_stability_threshold = std::clamp(config_.bias_stability_threshold, 0.01f, 0.5f);

                RCLCPP_INFO(this->get_logger(), "Config loaded successfully");
            } else {
                RCLCPP_WARN(this->get_logger(), "No 'imu_tools' config found, using defaults");
            }
        } catch (const std::exception& e) {
            RCLCPP_WARN(this->get_logger(), "Failed to load config: %s", e.what());
            RCLCPP_WARN(this->get_logger(), "Using default values");
        }
    }

    /**
     * @brief 执行校准（在独立线程中运行）
     */
    CalibrationResult calibrateGyroBiasInternal(int samples, float stability_threshold)
    {
        CalibrationResult result;

        samples = std::clamp(samples, 10, 1000);

        {
            std::lock_guard<std::mutex> lock(calib_mutex_);
            calibrating_ = true;
            calib_done_ = false;
            calib_samples_.clear();
            calib_samples_.reserve(samples);
            target_samples_ = samples;
        }

        RCLCPP_INFO(this->get_logger(), "Collecting %d samples...", target_samples_);

        const int max_wait_ms = 5000;
        const int check_interval_ms = 10;
        int waited_ms = 0;

        while (waited_ms < max_wait_ms) {
            std::this_thread::sleep_for(std::chrono::milliseconds(check_interval_ms));
            waited_ms += check_interval_ms;

            if (waited_ms % 100 == 0) {
                std::lock_guard<std::mutex> lock(calib_mutex_);
                RCLCPP_INFO(this->get_logger(), "Collecting samples: %zu/%d",
                           calib_samples_.size(), target_samples_);
            }

            if (calib_done_.load()) {
                break;
            }
        }

        calibrating_ = false;

        std::lock_guard<std::mutex> lock(calib_mutex_);

        if (!calib_done_.load()) {
            result.success = false;
            result.message = "Calibration timeout, collected " +
                            std::to_string(calib_samples_.size()) + " samples";
            RCLCPP_WARN(this->get_logger(), "Calibration timeout");
            calib_samples_.clear();
            return result;
        }

        return calib_result_;
    }

    /**
     * @brief 处理单个校准样本（在 onIMU 中调用）
     */
    void processCalibrationSample(const Eigen::Vector3f& sample)
    {
        std::lock_guard<std::mutex> lock(calib_mutex_);

        if (!calibrating_.load()) {
            return;
        }

        calib_samples_.push_back(sample);

        if (calib_samples_.size() > 2000) {
            calib_samples_.clear();
            return;
        }

        if (calib_samples_.size() >= static_cast<size_t>(target_samples_)) {
            // 计算均值
            Eigen::Vector3f sum = Eigen::Vector3f::Zero();
            for (const auto& s : calib_samples_) {
                sum += s;
            }
            Eigen::Vector3f bias = sum / static_cast<float>(calib_samples_.size());

            // 计算标准差
            float var_x = 0, var_y = 0, var_z = 0;
            for (const auto& s : calib_samples_) {
                Eigen::Vector3f diff = s - bias;
                var_x += diff.x() * diff.x();
                var_y += diff.y() * diff.y();
                var_z += diff.z() * diff.z();
            }
            var_x /= calib_samples_.size();
            var_y /= calib_samples_.size();
            var_z /= calib_samples_.size();

            Eigen::Vector3f stddev(std::sqrt(var_x), std::sqrt(var_y), std::sqrt(var_z));

            RCLCPP_INFO(this->get_logger(),
                       "Calibration stats: bias=(%.4f, %.4f, %.4f), stddev=(%.4f, %.4f, %.4f)",
                       bias.x(), bias.y(), bias.z(),
                       stddev.x(), stddev.y(), stddev.z());

            if (stddev.norm() < config_.bias_stability_threshold) {
                imu_tools_.setPresetBias(bias);

                calib_result_.success = true;
                calib_result_.bias = bias;
                calib_result_.stddev = stddev;
                calib_result_.samples = calib_samples_.size();
                calib_result_.message = "Calibration successful";

                RCLCPP_INFO(this->get_logger(), "Calibration successful! Bias set.");
            } else {
                calib_result_.success = false;
                calib_result_.bias = bias;
                calib_result_.stddev = stddev;
                calib_result_.samples = calib_samples_.size();
                calib_result_.message = "Data unstable: stddev_norm=" +
                                       std::to_string(stddev.norm()) +
                                       " > threshold=" +
                                       std::to_string(config_.bias_stability_threshold);
                RCLCPP_WARN(this->get_logger(), "Calibration failed: data unstable");
            }

            calib_done_ = true;
            calibrating_ = false;

            RCLCPP_INFO(this->get_logger(), "Calibration complete, %zu samples collected",
                       calib_samples_.size());
        }
    }

    /**
     * @brief 校准服务回调 - 在独立线程中执行，不阻塞主线程
     */
    void onCalibrate(
        const std::shared_ptr<std_srvs::srv::Trigger::Request> request,
        std::shared_ptr<std_srvs::srv::Trigger::Response> response)
    {
        (void)request;

        RCLCPP_WARN(this->get_logger(), "=== 陀螺仪校准开始 ===");
        RCLCPP_WARN(this->get_logger(), "=== 请保持机器静止 ===");

        std::thread([this, response]() {
            auto result = calibrateGyroBiasInternal(
                config_.bias_samples,
                config_.bias_stability_threshold);

            if (result.success) {
                response->success = true;
                response->message = "Calibration successful. Bias: " +
                    std::to_string(result.bias.x()) + ", " +
                    std::to_string(result.bias.y()) + ", " +
                    std::to_string(result.bias.z()) + " rad/s";
                RCLCPP_INFO(this->get_logger(), "Calibration completed successfully");
                RCLCPP_INFO(this->get_logger(), "  Bias: x=%.4f, y=%.4f, z=%.4f rad/s",
                           result.bias.x(), result.bias.y(), result.bias.z());
                RCLCPP_INFO(this->get_logger(), "  StdDev: x=%.4f, y=%.4f, z=%.4f rad/s",
                           result.stddev.x(), result.stddev.y(), result.stddev.z());
            } else {
                response->success = false;
                response->message = "Calibration failed: " + result.message;
                RCLCPP_WARN(this->get_logger(), "Calibration failed: %s", result.message.c_str());
            }
        }).detach();
    }

    /**
     * @brief IMU 数据回调
     */
    void onIMU(const sensor_msgs::msg::Imu::SharedPtr msg)
    {
        double dt = 0.01;
        if (last_time_ > 0) {
            dt = (msg->header.stamp.sec - last_time_) +
                 (msg->header.stamp.nanosec - last_time_ns_) / 1e9;
            if (dt < 0.001) dt = 0.001;
            if (dt > 0.1) dt = 0.01;
        }
        last_time_ = msg->header.stamp.sec;
        last_time_ns_ = msg->header.stamp.nanosec;

        // 校准模式：收集样本
        if (calibrating_.load()) {
            processCalibrationSample(Eigen::Vector3f(
                msg->angular_velocity.x,
                msg->angular_velocity.y,
                msg->angular_velocity.z));
        }

        imu_tools_.update(
            msg->linear_acceleration.x,
            msg->linear_acceleration.y,
            msg->linear_acceleration.z,
            msg->angular_velocity.x,
            msg->angular_velocity.y,
            msg->angular_velocity.z,
            static_cast<float>(dt));

        auto attitude = imu_tools_.getAttitude();
        if (!attitude.valid) {
            return;
        }

        auto compensated_gyro = imu_tools_.getCompensatedGyro();

        auto pub_msg = sensor_msgs::msg::Imu();
        pub_msg.header.stamp = msg->header.stamp;
        pub_msg.header.frame_id = config_.frame_id;

        pub_msg.orientation.x = attitude.quaternion.x();
        pub_msg.orientation.y = attitude.quaternion.y();
        pub_msg.orientation.z = attitude.quaternion.z();
        pub_msg.orientation.w = attitude.quaternion.w();

        pub_msg.linear_acceleration = msg->linear_acceleration;

        pub_msg.angular_velocity.x = compensated_gyro.x();
        pub_msg.angular_velocity.y = compensated_gyro.y();
        pub_msg.angular_velocity.z = compensated_gyro.z();

        for (int i = 0; i < 9; ++i) {
            pub_msg.orientation_covariance[i] = 0.0;
            pub_msg.linear_acceleration_covariance[i] = 0.0;
            pub_msg.angular_velocity_covariance[i] = 0.0;
        }

        imu_pub_->publish(pub_msg);
    }

    // ROS2 通信
    rclcpp::Subscription<sensor_msgs::msg::Imu>::SharedPtr imu_sub_;
    rclcpp::Publisher<sensor_msgs::msg::Imu>::SharedPtr imu_pub_;
    rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr calibrate_service_;

    // IMU 工具
    algorithms::IMUTools imu_tools_;

    // 时间跟踪
    double last_time_ = 0;
    double last_time_ns_ = 0;
};

int main(int argc, char** argv)
{
    rclcpp::init(argc, argv);
    auto node = std::make_shared<IMUToolsNode>();
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}