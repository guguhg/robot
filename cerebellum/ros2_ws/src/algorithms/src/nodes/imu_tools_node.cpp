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
#include <optional>

/**
 * @brief 零漂校准结果结构体
 */
struct CalibrationResult
{
    bool success = false;
    Eigen::Vector3f bias = Eigen::Vector3f::Zero();
    Eigen::Vector3f stddev = Eigen::Vector3f::Zero();
    int samples = 0;
    std::string message;
};

/**
 * @brief IMU 工具 ROS2 节点
 * 
 * 职责：
 *   1. 订阅原始 IMU 数据 (/imu/data_raw)
 *   2. 调用 IMUTools 进行姿态解算
 *   3. 发布处理后的 IMU 数据 (/imu/data)
 *   4. 提供校准服务接口
 * 
 * 服务列表：
 *   - /imu_tools_node/calibrate_gyro        : 手动零漂校准
 *   - /imu_tools_node/calibrate_zero_point  : 手动零点校准
 * 
 * 配置项 (config.yaml)：
 *   zero_point_calibration:
 *     enabled: true
 *     auto_calibration: false     # 开机自动零点校准
 *     stationary_threshold: 0.02
 *     preset_zero_point:          # 预设零点四元数
 *       w: 1.0, x: 0.0, y: 0.0, z: 0.0
 *   
 *   bias_calibration:
 *     enabled: true
 *     auto_calibration: true      # 开机自动零漂校准
 *     samples: 200
 *     stability_threshold: 0.05
 *     preset_bias:                # 预设零偏值
 *       x: 0.0, y: 0.0, z: 0.0
 */
class IMUToolsNode : public rclcpp::Node
{
public:
    IMUToolsNode()
        : Node("imu_tools_node")
    {
        RCLCPP_INFO(this->get_logger(), "========================================");
        RCLCPP_INFO(this->get_logger(), "       IMU Tools Node Starting");
        RCLCPP_INFO(this->get_logger(), "========================================");

        // ---- 1. 加载配置文件 ----
        loadConfig();

        // ---- 2. 创建订阅和发布 ----
        imu_sub_ = this->create_subscription<sensor_msgs::msg::Imu>(
            config_.input_topic, 1,
            std::bind(&IMUToolsNode::onIMU, this, std::placeholders::_1));

        imu_pub_ = this->create_publisher<sensor_msgs::msg::Imu>(
            config_.output_topic, 10);

        // ---- 3. 配置 IMU 工具 ----
        imu_tools_.setFilterCoefficient(config_.alpha);
        imu_tools_.setBiasCompensationEnabled(config_.bias_enabled);
        imu_tools_.setZeroPointConfig(
            config_.zero_point_enabled,
            config_.stationary_threshold,
            config_.stationary_samples);

        // ---- 4. 加载预设值 ----
        
        // 4.1 加载预设零点四元数
        if (config_.preset_zero_point.has_value()) {
            imu_tools_.setPresetZeroPoint(config_.preset_zero_point.value());
            auto euler = quaternionToEuler(config_.preset_zero_point.value());
            RCLCPP_INFO(this->get_logger(), "✓ Loaded preset zero point:");
            RCLCPP_INFO(this->get_logger(), "    Quat: w=%.4f, x=%.4f, y=%.4f, z=%.4f",
                        config_.preset_zero_point.value().w(),
                        config_.preset_zero_point.value().x(),
                        config_.preset_zero_point.value().y(),
                        config_.preset_zero_point.value().z());
            RCLCPP_INFO(this->get_logger(), "    Euler: roll=%.2f°, pitch=%.2f°, yaw=%.2f°",
                        euler.x() * 180.0 / M_PI,
                        euler.y() * 180.0 / M_PI,
                        euler.z() * 180.0 / M_PI);
        } else {
            RCLCPP_WARN(this->get_logger(), "✗ No preset zero point configured!");
            RCLCPP_WARN(this->get_logger(), "  Run: ros2 service call /imu_tools_node/calibrate_zero_point std_srvs/srv/Trigger \"{}\"");
        }

        // 4.2 加载预设零偏值
        if (config_.bias_enabled && config_.preset_bias.has_value()) {
            imu_tools_.setPresetBias(config_.preset_bias.value());
            RCLCPP_INFO(this->get_logger(), "✓ Loaded preset gyro bias:");
            RCLCPP_INFO(this->get_logger(), "    Bias: x=%.4f, y=%.4f, z=%.4f rad/s",
                        config_.preset_bias.value().x(),
                        config_.preset_bias.value().y(),
                        config_.preset_bias.value().z());
        }

        // ---- 5. 创建服务 ----
        
        // 5.1 零漂校准服务
        calibrate_gyro_service_ = this->create_service<std_srvs::srv::Trigger>(
            "~/calibrate_gyro",
            std::bind(&IMUToolsNode::onCalibrateGyro, this,
                      std::placeholders::_1, std::placeholders::_2));

        // 5.2 零点校准服务
        calibrate_zero_point_service_ = this->create_service<std_srvs::srv::Trigger>(
            "~/calibrate_zero_point",
            std::bind(&IMUToolsNode::onCalibrateZeroPoint, this,
                      std::placeholders::_1, std::placeholders::_2));

        // ---- 6. 开机自动校准 ----
        
        // 6.1 零漂校准（开机自动）
        if (config_.auto_calibration && config_.bias_enabled) {
            RCLCPP_WARN(this->get_logger(), "========================================");
            RCLCPP_WARN(this->get_logger(), "  Auto zero-drift calibration ENABLED");
            RCLCPP_WARN(this->get_logger(), "  Please keep vehicle STILL for 2 seconds");
            RCLCPP_WARN(this->get_logger(), "========================================");

            auto_calibrate_timer_ = this->create_wall_timer(
                std::chrono::seconds(2),
                [this]() {
                    RCLCPP_INFO(this->get_logger(), "Starting auto zero-drift calibration...");

                    std::thread([this]() {
                        auto result = calibrateGyroBiasInternal(
                            config_.bias_samples,
                            config_.bias_stability_threshold);

                        if (result.success) {
                            RCLCPP_INFO(this->get_logger(), "✓ Auto zero-drift calibration SUCCESS!");
                            RCLCPP_INFO(this->get_logger(), "    Bias: x=%.4f, y=%.4f, z=%.4f rad/s",
                                       result.bias.x(), result.bias.y(), result.bias.z());
                            RCLCPP_INFO(this->get_logger(), "    StdDev: x=%.4f, y=%.4f, z=%.4f rad/s",
                                       result.stddev.x(), result.stddev.y(), result.stddev.z());
                            RCLCPP_WARN(this->get_logger(), "  Update preset_bias in config.yaml:");
                            RCLCPP_WARN(this->get_logger(), "    x: %.4f, y: %.4f, z: %.4f",
                                       result.bias.x(), result.bias.y(), result.bias.z());
                        } else {
                            RCLCPP_WARN(this->get_logger(), "✗ Auto zero-drift calibration FAILED: %s",
                                       result.message.c_str());
                        }
                    }).detach();

                    auto_calibrate_timer_->cancel();
                });
        }

        // 6.2 零点校准（开机自动，默认禁用）
        if (config_.auto_zero_calibration && config_.zero_point_enabled) {
            RCLCPP_WARN(this->get_logger(), "========================================");
            RCLCPP_WARN(this->get_logger(), "  Auto zero-point calibration ENABLED");
            RCLCPP_WARN(this->get_logger(), "  Ensure vehicle on LEVEL ground and STILL");
            RCLCPP_WARN(this->get_logger(), "========================================");

            auto_zero_calibrate_timer_ = this->create_wall_timer(
                std::chrono::seconds(3),
                [this]() {
                    RCLCPP_INFO(this->get_logger(), "Starting auto zero-point calibration...");

                    // 检查是否静止
                    auto gyro = imu_tools_.getCompensatedGyro();
                    if (gyro.norm() > config_.stationary_threshold) {
                        RCLCPP_WARN(this->get_logger(),
                                    "✗ Auto zero-point calibration FAILED: vehicle not stationary");
                        RCLCPP_WARN(this->get_logger(),
                                    "    gyro=%.4f rad/s > threshold=%.4f rad/s",
                                    gyro.norm(), config_.stationary_threshold);
                        auto_zero_calibrate_timer_->cancel();
                        return;
                    }

                    if (imu_tools_.calibrateZeroPoint()) {
                        RCLCPP_INFO(this->get_logger(), "✓ Auto zero-point calibration SUCCESS!");
                        Eigen::Quaternionf zero_quat = imu_tools_.getZeroPointQuat();
                        auto euler = quaternionToEuler(zero_quat);
                        RCLCPP_INFO(this->get_logger(),
                                    "    Quat: w=%.4f, x=%.4f, y=%.4f, z=%.4f",
                                    zero_quat.w(), zero_quat.x(), zero_quat.y(), zero_quat.z());
                        RCLCPP_INFO(this->get_logger(),
                                    "    Euler: roll=%.2f°, pitch=%.2f°, yaw=%.2f°",
                                    euler.x() * 180.0 / M_PI,
                                    euler.y() * 180.0 / M_PI,
                                    euler.z() * 180.0 / M_PI);
                        RCLCPP_WARN(this->get_logger(), "  Update preset_zero_point in config.yaml");
                    } else {
                        RCLCPP_WARN(this->get_logger(), "✗ Auto zero-point calibration FAILED");
                    }

                    auto_zero_calibrate_timer_->cancel();
                });
        }

        // ---- 7. 打印启动信息 ----
        RCLCPP_INFO(this->get_logger(), "========================================");
        RCLCPP_INFO(this->get_logger(), "  IMUToolsNode initialized");
        RCLCPP_INFO(this->get_logger(), "  ------------------------------------");
        RCLCPP_INFO(this->get_logger(), "  Input:  %s", config_.input_topic.c_str());
        RCLCPP_INFO(this->get_logger(), "  Output: %s", config_.output_topic.c_str());
        RCLCPP_INFO(this->get_logger(), "  Frame:  %s", config_.frame_id.c_str());
        RCLCPP_INFO(this->get_logger(), "  Alpha:  %.2f", config_.alpha);
        RCLCPP_INFO(this->get_logger(), "  ------------------------------------");
        RCLCPP_INFO(this->get_logger(), "  Zero point:  %s", config_.zero_point_enabled ? "enabled" : "disabled");
        RCLCPP_INFO(this->get_logger(), "  Zero auto:   %s", config_.auto_zero_calibration ? "ON" : "OFF");
        RCLCPP_INFO(this->get_logger(), "  Zero preset: %s", config_.preset_zero_point.has_value() ? "loaded" : "none");
        RCLCPP_INFO(this->get_logger(), "  ------------------------------------");
        RCLCPP_INFO(this->get_logger(), "  Gyro bias:   %s", config_.bias_enabled ? "enabled" : "disabled");
        RCLCPP_INFO(this->get_logger(), "  Bias auto:   %s", config_.auto_calibration ? "ON" : "OFF");
        RCLCPP_INFO(this->get_logger(), "  Bias preset: %s", config_.preset_bias.has_value() ? "loaded" : "none");
        RCLCPP_INFO(this->get_logger(), "========================================");
    }

private:
    // ============================================================================
    // 配置结构体
    // ============================================================================
    
    struct Config
    {
        // 话题配置
        std::string input_topic = "/imu/data_raw";
        std::string output_topic = "/imu/data";
        std::string frame_id = "imu_link";

        // 滤波参数
        float alpha = 0.98f;
        int publish_rate = 100;

        // 零点校准
        bool zero_point_enabled = true;
        bool auto_zero_calibration = false;      // 开机自动零点校准
        float stationary_threshold = 0.02f;
        int stationary_samples = 50;
        std::optional<Eigen::Quaternionf> preset_zero_point;

        // 零漂校准
        bool bias_enabled = true;
        bool auto_calibration = true;            // 开机自动零漂校准
        int bias_samples = 200;
        float bias_stability_threshold = 0.05f;
        std::optional<Eigen::Vector3f> preset_bias;

        // 输出配置
        bool publish_relative = true;
    } config_;

    // ============================================================================
    // 校准状态
    // ============================================================================
    
    std::atomic<bool> calibrating_{false};
    std::atomic<bool> calib_done_{false};
    int target_samples_ = 200;
    std::vector<Eigen::Vector3f> calib_samples_;
    std::mutex calib_mutex_;
    CalibrationResult calib_result_;
    
    rclcpp::TimerBase::SharedPtr auto_calibrate_timer_;
    rclcpp::TimerBase::SharedPtr auto_zero_calibrate_timer_;

    // ============================================================================
    // 辅助函数
    // ============================================================================
    
    Eigen::Vector3f quaternionToEuler(const Eigen::Quaternionf& q)
    {
        Eigen::Vector3f euler;
        euler.z() = std::atan2(2.0f * (q.w() * q.z() + q.x() * q.y()),
                               1.0f - 2.0f * (q.y() * q.y() + q.z() * q.z()));
        euler.y() = std::asin(std::clamp(2.0f * (q.w() * q.y() - q.z() * q.x()),
                                         -1.0f, 1.0f));
        euler.x() = std::atan2(2.0f * (q.w() * q.x() + q.y() * q.z()),
                               1.0f - 2.0f * (q.x() * q.x() + q.y() * q.y()));
        return euler;
    }

    void loadConfig()
    {
        try {
            YAML::Node config = common::ConfigLoader::loadDefault();
            auto imu_tools_config = config["algorithms"]["imu_tools"];

            if (imu_tools_config) {
                // 话题配置
                auto topics = imu_tools_config["topics"];
                if (topics) {
                    config_.input_topic = topics["input"].as<std::string>("/imu/data_raw");
                    config_.output_topic = topics["output"].as<std::string>("/imu/data");
                }

                // 滤波参数
                config_.alpha = imu_tools_config["alpha"].as<float>(0.98f);
                config_.frame_id = imu_tools_config["frame_id"].as<std::string>("imu_link");
                config_.publish_rate = imu_tools_config["publish_rate"].as<int>(100);

                // 零点校准配置
                auto zero_config = imu_tools_config["zero_point_calibration"];
                if (zero_config) {
                    config_.zero_point_enabled = zero_config["enabled"].as<bool>(true);
                    config_.auto_zero_calibration = zero_config["auto_calibration"].as<bool>(false);
                    config_.stationary_threshold = zero_config["stationary_threshold"].as<float>(0.02f);
                    config_.stationary_samples = zero_config["stationary_samples"].as<int>(50);

                    auto preset = zero_config["preset_zero_point"];
                    if (preset) {
                        Eigen::Quaternionf quat;
                        quat.w() = preset["w"].as<float>(1.0f);
                        quat.x() = preset["x"].as<float>(0.0f);
                        quat.y() = preset["y"].as<float>(0.0f);
                        quat.z() = preset["z"].as<float>(0.0f);
                        if (quat.norm() > 0.001f && quat.norm() < 1.1f) {
                            config_.preset_zero_point = quat.normalized();
                        }
                    }
                }

                // 零漂校准配置
                auto bias_config = imu_tools_config["bias_calibration"];
                if (bias_config) {
                    config_.bias_enabled = bias_config["enabled"].as<bool>(true);
                    config_.auto_calibration = bias_config["auto_calibration"].as<bool>(true);
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

                // 输出配置
                config_.publish_relative = imu_tools_config["publish_relative"].as<bool>(true);

                // 参数限幅
                config_.alpha = std::clamp(config_.alpha, 0.0f, 1.0f);
                config_.bias_samples = std::clamp(config_.bias_samples, 10, 1000);
                config_.bias_stability_threshold = std::clamp(config_.bias_stability_threshold, 0.01f, 0.5f);
                config_.stationary_threshold = std::clamp(config_.stationary_threshold, 0.001f, 0.1f);

                RCLCPP_INFO(this->get_logger(), "✓ Config loaded");
            } else {
                RCLCPP_WARN(this->get_logger(), "No 'imu_tools' config, using defaults");
            }
        } catch (const std::exception& e) {
            RCLCPP_WARN(this->get_logger(), "Failed to load config: %s", e.what());
            RCLCPP_WARN(this->get_logger(), "Using defaults");
        }
    }

    // ============================================================================
    // 零漂校准核心功能
    // ============================================================================
    
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

        // 等待样本收集完成（最多 5 秒）
        const int max_wait_ms = 5000;
        const int check_interval_ms = 10;
        int waited_ms = 0;

        while (waited_ms < max_wait_ms) {
            std::this_thread::sleep_for(std::chrono::milliseconds(check_interval_ms));
            waited_ms += check_interval_ms;

            if (waited_ms % 100 == 0) {
                std::lock_guard<std::mutex> lock(calib_mutex_);
                RCLCPP_INFO(this->get_logger(), "  %zu/%d samples", 
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
            result.message = "Timeout, collected " +
                             std::to_string(calib_samples_.size()) + " samples";
            calib_samples_.clear();
            return result;
        }

        return calib_result_;
    }

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
            // 计算均值（零偏）
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
                        "Stats: bias=(%.4f, %.4f, %.4f), stddev=(%.4f, %.4f, %.4f)",
                        bias.x(), bias.y(), bias.z(),
                        stddev.x(), stddev.y(), stddev.z());

            // 判断数据是否稳定
            if (stddev.norm() < config_.bias_stability_threshold) {
                imu_tools_.setPresetBias(bias);

                calib_result_.success = true;
                calib_result_.bias = bias;
                calib_result_.stddev = stddev;
                calib_result_.samples = calib_samples_.size();
                calib_result_.message = "Success";
            } else {
                calib_result_.success = false;
                calib_result_.bias = bias;
                calib_result_.stddev = stddev;
                calib_result_.samples = calib_samples_.size();
                calib_result_.message = "Unstable: stddev_norm=" +
                                        std::to_string(stddev.norm());
            }

            calib_done_ = true;
            calibrating_ = false;
        }
    }

    // ============================================================================
    // 服务回调
    // ============================================================================
    
    void onCalibrateGyro(
        const std::shared_ptr<std_srvs::srv::Trigger::Request> request,
        std::shared_ptr<std_srvs::srv::Trigger::Response> response)
    {
        (void)request;

        RCLCPP_WARN(this->get_logger(), "=== Manual zero-drift calibration START ===");
        RCLCPP_WARN(this->get_logger(), "=== Keep vehicle STILL ===");

        std::thread([this, response]() {
            auto result = calibrateGyroBiasInternal(
                config_.bias_samples,
                config_.bias_stability_threshold);

            if (result.success) {
                response->success = true;
                response->message = "Success. Bias: " +
                    std::to_string(result.bias.x()) + ", " +
                    std::to_string(result.bias.y()) + ", " +
                    std::to_string(result.bias.z()) + " rad/s";
                RCLCPP_INFO(this->get_logger(), "✓ Calibration SUCCESS");
                RCLCPP_INFO(this->get_logger(), "  Bias: x=%.4f, y=%.4f, z=%.4f rad/s",
                           result.bias.x(), result.bias.y(), result.bias.z());
                RCLCPP_WARN(this->get_logger(), "  Update preset_bias in config.yaml");
            } else {
                response->success = false;
                response->message = "Failed: " + result.message;
                RCLCPP_WARN(this->get_logger(), "✗ Calibration FAILED: %s",
                           result.message.c_str());
            }
        }).detach();
    }

    void onCalibrateZeroPoint(
        const std::shared_ptr<std_srvs::srv::Trigger::Request> request,
        std::shared_ptr<std_srvs::srv::Trigger::Response> response)
    {
        (void)request;

        RCLCPP_WARN(this->get_logger(), "=== Manual zero-point calibration START ===");
        RCLCPP_WARN(this->get_logger(), "=== Place vehicle on LEVEL ground and keep STILL ===");

        // 检查 IMU 是否已初始化
        auto attitude = imu_tools_.getAttitude();
        if (!attitude.valid) {
            response->success = false;
            response->message = "IMU not initialized yet";
            RCLCPP_WARN(this->get_logger(), "✗ IMU not initialized");
            return;
        }

        // 检查是否静止
        auto gyro = imu_tools_.getCompensatedGyro();
        if (gyro.norm() > config_.stationary_threshold) {
            response->success = false;
            response->message = "Vehicle not stationary: gyro=" +
                                std::to_string(gyro.norm()) + " rad/s";
            RCLCPP_WARN(this->get_logger(), "✗ Vehicle moving: gyro=%.4f rad/s",
                        gyro.norm());
            return;
        }

        // 执行校准
        if (imu_tools_.calibrateZeroPoint()) {
            response->success = true;
            response->message = "Success!";
            // 校准结果已由 IMUTools 打印
        } else {
            response->success = false;
            response->message = "Calibration failed";
            RCLCPP_WARN(this->get_logger(), "✗ Calibration FAILED");
        }
    }

    // ============================================================================
    // IMU 数据回调
    // ============================================================================
    
    void onIMU(const sensor_msgs::msg::Imu::SharedPtr msg)
    {
        // 计算时间增量
        double dt = 0.01;
        if (last_time_ > 0) {
            dt = (msg->header.stamp.sec - last_time_) +
                 (msg->header.stamp.nanosec - last_time_ns_) / 1e9;
            if (dt < 0.001) dt = 0.001;
            if (dt > 0.1) dt = 0.01;
        }
        last_time_ = msg->header.stamp.sec;
        last_time_ns_ = msg->header.stamp.nanosec;

        // 零漂校准模式：收集样本
        if (calibrating_.load()) {
            processCalibrationSample(Eigen::Vector3f(
                msg->angular_velocity.x,
                msg->angular_velocity.y,
                msg->angular_velocity.z));
        }

        // 更新 IMU 工具
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

        // 构建发布消息
        auto pub_msg = sensor_msgs::msg::Imu();
        pub_msg.header.stamp = msg->header.stamp;
        pub_msg.header.frame_id = config_.frame_id;

        // 发布四元数
        if (config_.publish_relative) {
            Eigen::Quaternionf relative_quat = imu_tools_.getRelativeQuaternion();
            pub_msg.orientation.x = relative_quat.x();
            pub_msg.orientation.y = relative_quat.y();
            pub_msg.orientation.z = relative_quat.z();
            pub_msg.orientation.w = relative_quat.w();

            Eigen::Vector3f euler = imu_tools_.getRelativeEuler();
            RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                                 "yaw:%f pitch:%f roll:%f\n", euler.z(), euler.y(), euler.x());
        } else {
            pub_msg.orientation.x = attitude.quaternion.x();
            pub_msg.orientation.y = attitude.quaternion.y();
            pub_msg.orientation.z = attitude.quaternion.z();
            pub_msg.orientation.w = attitude.quaternion.w();

            Eigen::Vector3f euler = imu_tools_.getRelativeEuler();
            RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                                 "yaw:%f pitch:%f roll:%f\n", euler.z(), euler.y(), euler.x());
        }

        // 加速度（透传）
        pub_msg.linear_acceleration = msg->linear_acceleration;

        // 角速度（补偿后）
        pub_msg.angular_velocity.x = compensated_gyro.x();
        pub_msg.angular_velocity.y = compensated_gyro.y();
        pub_msg.angular_velocity.z = compensated_gyro.z();

        // 协方差（暂设 0）
        for (int i = 0; i < 9; ++i) {
            pub_msg.orientation_covariance[i] = 0.0;
            pub_msg.linear_acceleration_covariance[i] = 0.0;
            pub_msg.angular_velocity_covariance[i] = 0.0;
        }

        imu_pub_->publish(pub_msg);
    }

    // ============================================================================
    // 成员变量
    // ============================================================================
    
    rclcpp::Subscription<sensor_msgs::msg::Imu>::SharedPtr imu_sub_;
    rclcpp::Publisher<sensor_msgs::msg::Imu>::SharedPtr imu_pub_;
    rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr calibrate_gyro_service_;
    rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr calibrate_zero_point_service_;

    algorithms::IMUTools imu_tools_;

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