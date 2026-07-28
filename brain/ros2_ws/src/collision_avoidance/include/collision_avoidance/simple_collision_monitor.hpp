#ifndef SIMPLE_COLLISION_MONITOR_HPP
#define SIMPLE_COLLISION_MONITOR_HPP

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/laser_scan.hpp>
#include <geometry_msgs/msg/twist.hpp>
#include <geometry_msgs/msg/point.hpp>
#include <tf2_ros/buffer.h>
#include <tf2_ros/transform_listener.h>
#include <tf2_geometry_msgs/tf2_geometry_msgs.hpp>
#include <geometry_msgs/msg/polygon_stamped.hpp>
#include <geometry_msgs/msg/point32.hpp>

#include <vector>
#include <string>
#include <map>
#include <memory>

namespace collision_avoidance {

/**
 * @brief 速度指令源配置
 */
struct CmdVelSource {
  std::string topic;           // 订阅的话题名
  int priority;                // 优先级（数值越大优先级越高）
  bool collision_enabled;      // true=启用避障, false=透传
};

/**
 * @brief 多边形配置
 */
struct PolygonConfig {
  std::vector<geometry_msgs::msg::Point> points;  // 多边形顶点
  std::string action;              // "direction", "stop", "slowdown", "limit_angular"
  int min_points;                  // 触发阈值点数
  double slowdown_ratio;           // 减速比例
  double max_angular;              // 最大角速度
  std::string direction;           // "forward", "backward", "left", "right"
};

/**
 * @brief 方向感知避障节点
 * 
 * 功能：
 *   - 订阅多个速度指令话题（硬编码：/manual_cmd_vel 和 /cmd_vel）
 *   - 根据优先级仲裁（手动 100 > Nav2 50）
 *   - 对手动指令启用避障，对 Nav2 指令透传
 *   - 发布修正后的速度到 /cmd_vel_safe
 */
class SimpleCollisionMonitor : public rclcpp::Node {
public:
  SimpleCollisionMonitor();
  ~SimpleCollisionMonitor() = default;

private:
  // ========== 回调函数 ==========
  void scanCallback(const sensor_msgs::msg::LaserScan::SharedPtr msg);
  void cmdVelCallback(const geometry_msgs::msg::Twist::SharedPtr msg, 
                      const std::string& topic, 
                      bool collision_enabled,
                      int priority);

  // ========== 工具函数 ==========
  bool pointInPolygon(const geometry_msgs::msg::Point& pt, 
                      const std::vector<geometry_msgs::msg::Point>& polygon);
  void applyDirectionConstraints(geometry_msgs::msg::Twist& cmd,
                                 const std::vector<std::string>& directions);

  // ========== 配置加载 ==========
  void loadPolygons();

  // ========== ROS 通信 ==========
  rclcpp::Subscription<sensor_msgs::msg::LaserScan>::SharedPtr scan_sub_;
  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr cmd_vel_pub_;
  rclcpp::Publisher<geometry_msgs::msg::PolygonStamped>::SharedPtr polygon_pub_;
  std::vector<rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr> cmd_vel_subs_;

  // ========== TF ==========
  std::shared_ptr<tf2_ros::Buffer> tf_buffer_;
  std::shared_ptr<tf2_ros::TransformListener> tf_listener_;

  // ========== 配置数据 ==========
  std::string base_frame_;
  std::map<std::string, PolygonConfig> polygons_;
  std::vector<CmdVelSource> cmd_vel_sources_;  // 硬编码，不由 YAML 加载
  std::string polygon_pub_topic_;
  bool visualize_polygons_;
  double cmd_timeout_;

  // ========== 当前激活的指令 ==========
  struct ActiveCmd {
    geometry_msgs::msg::Twist cmd;
    std::string source_topic;
    bool collision_enabled;
    int priority;
    rclcpp::Time timestamp;
    bool valid;
  };
  ActiveCmd active_cmd_;
};

} // namespace collision_avoidance

#endif