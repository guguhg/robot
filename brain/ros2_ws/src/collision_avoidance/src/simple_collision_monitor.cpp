#include "collision_avoidance/simple_collision_monitor.hpp"
#include <tf2_ros/transform_listener.h>
#include <tf2_geometry_msgs/tf2_geometry_msgs.hpp>
#include <tf2/transform_datatypes.h>
#include <geometry_msgs/msg/point_stamped.hpp>

namespace collision_avoidance {

// ============================================================
// 构造函数
// ============================================================
SimpleCollisionMonitor::SimpleCollisionMonitor()
: Node("simple_collision_monitor"), active_cmd_{.valid = false}
{
  // ---------- 声明参数 ----------
  declare_parameter("base_frame", "base_link");
  declare_parameter("scan_topic", "/scan");
  declare_parameter("cmd_vel_out_topic", "/cmd_vel_safe");
  declare_parameter("cmd_timeout", 0.5);
  declare_parameter("polygon_pub_topic", "polygon_visualization");
  declare_parameter("visualize_polygons", true);

  // ---------- 获取参数 ----------
  get_parameter("base_frame", base_frame_);
  std::string scan_topic, cmd_out;
  get_parameter("scan_topic", scan_topic);
  get_parameter("cmd_vel_out_topic", cmd_out);
  get_parameter("cmd_timeout", cmd_timeout_);
  get_parameter("polygon_pub_topic", polygon_pub_topic_);
  get_parameter("visualize_polygons", visualize_polygons_);

  // ---------- 加载多边形配置 ----------
  loadPolygons();

  // ---------- 硬编码速度源 ----------
  cmd_vel_sources_.clear();
  cmd_vel_sources_.push_back({"/manual_cmd_vel", 100, true});  // 手动控制 → 启用避障
  cmd_vel_sources_.push_back({"/cmd_vel", 50, false});         // Nav2 → 透传

  RCLCPP_INFO(get_logger(), "Using hardcoded cmd_vel sources:");
  for (const auto& src : cmd_vel_sources_) {
    RCLCPP_INFO(get_logger(), "  %s (priority=%d, collision=%s)",
                src.topic.c_str(), src.priority, src.collision_enabled ? "ON" : "OFF");
  }

  // ---------- 创建订阅（每个速度源一个） ----------
  for (const auto& source : cmd_vel_sources_) {
    auto sub = create_subscription<geometry_msgs::msg::Twist>(
      source.topic, 10,
      [this, source](const geometry_msgs::msg::Twist::SharedPtr msg) {
        cmdVelCallback(msg, source.topic, source.collision_enabled, source.priority);
      });
    cmd_vel_subs_.push_back(sub);
  }

  // ---------- 创建发布和订阅 ----------
  scan_sub_ = create_subscription<sensor_msgs::msg::LaserScan>(
    scan_topic, 10, std::bind(&SimpleCollisionMonitor::scanCallback, this, std::placeholders::_1));
  cmd_vel_pub_ = create_publisher<geometry_msgs::msg::Twist>(cmd_out, 10);

  if (visualize_polygons_) {
    polygon_pub_ = create_publisher<geometry_msgs::msg::PolygonStamped>(polygon_pub_topic_, 10);
  }

  // ---------- TF 监听 ----------
  tf_buffer_ = std::make_shared<tf2_ros::Buffer>(get_clock());
  tf_listener_ = std::make_shared<tf2_ros::TransformListener>(*tf_buffer_);

  RCLCPP_INFO(get_logger(), "SimpleCollisionMonitor initialized with %zu polygons, %zu cmd sources",
              polygons_.size(), cmd_vel_sources_.size());
}

// ============================================================
// 加载多边形配置
// ============================================================
void SimpleCollisionMonitor::loadPolygons() {
  std::vector<std::string> polygon_names;
  declare_parameter("polygon_names", std::vector<std::string>());
  get_parameter("polygon_names", polygon_names);

  for (const auto& name : polygon_names) {
    PolygonConfig cfg;
    std::vector<double> points_flat;
    std::string action;
    int min_pts;
    double ratio = 0.5, max_ang = 0.5;
    std::string direction = "forward";

    std::string base = "polygons." + name + ".";
    declare_parameter(base + "points", std::vector<double>());
    declare_parameter(base + "action", std::string("direction"));
    declare_parameter(base + "min_points", 4);
    declare_parameter(base + "slowdown_ratio", 0.5);
    declare_parameter(base + "max_angular", 0.5);
    declare_parameter(base + "direction", std::string("forward"));

    get_parameter(base + "points", points_flat);
    get_parameter(base + "action", action);
    get_parameter(base + "min_points", min_pts);
    get_parameter(base + "slowdown_ratio", ratio);
    get_parameter(base + "max_angular", max_ang);
    get_parameter(base + "direction", direction);

    if (points_flat.size() < 6) {
      RCLCPP_WARN(get_logger(), "Polygon %s has insufficient points, skipping", name.c_str());
      continue;
    }

    for (size_t i = 0; i < points_flat.size(); i += 2) {
      geometry_msgs::msg::Point p;
      p.x = points_flat[i];
      p.y = points_flat[i+1];
      p.z = 0.0;
      cfg.points.push_back(p);
    }
    cfg.action = action;
    cfg.min_points = min_pts;
    cfg.slowdown_ratio = ratio;
    cfg.max_angular = max_ang;
    cfg.direction = direction;
    polygons_[name] = cfg;
    RCLCPP_INFO(get_logger(), "Loaded polygon '%s' with %zu points, action=%s, min_points=%d, direction=%s",
                name.c_str(), cfg.points.size(), action.c_str(), min_pts, direction.c_str());
  }
}

// ============================================================
// 速度回调（仲裁器）
// ============================================================
void SimpleCollisionMonitor::cmdVelCallback(
    const geometry_msgs::msg::Twist::SharedPtr msg,
    const std::string& topic,
    bool collision_enabled,
    int priority) {
  
  // 检查当前是否有更高优先级的有效指令
  if (active_cmd_.valid && active_cmd_.priority > priority) {
    return;  // 忽略较低优先级的指令
  }

  // 检查当前指令是否超时
  bool current_expired = false;
  if (active_cmd_.valid) {
    current_expired = (now() - active_cmd_.timestamp).seconds() > cmd_timeout_;
  }

  // 更新激活指令（条件：无有效指令 || 超时 || 更高优先级）
  if (!active_cmd_.valid || current_expired || priority > active_cmd_.priority) {
    active_cmd_.cmd = *msg;
    active_cmd_.source_topic = topic;
    active_cmd_.collision_enabled = collision_enabled;
    active_cmd_.priority = priority;
    active_cmd_.timestamp = now();
    active_cmd_.valid = true;
  }
}

// ============================================================
// 扫描回调（核心避障逻辑）
// ============================================================
void SimpleCollisionMonitor::scanCallback(const sensor_msgs::msg::LaserScan::SharedPtr msg) {
  // 1. 检查是否有有效的速度指令
  if (!active_cmd_.valid || (now() - active_cmd_.timestamp).seconds() > cmd_timeout_) {
    geometry_msgs::msg::Twist zero;
    cmd_vel_pub_->publish(zero);
    active_cmd_.valid = false;
    return;
  }

  // 2. 如果该指令禁用了避障，直接透传
  if (!active_cmd_.collision_enabled) {
    cmd_vel_pub_->publish(active_cmd_.cmd);
    return;
  }

  // 3. 可视化多边形（统一发布到 /polygon_visualization）
  if (visualize_polygons_ && polygon_pub_) {
    for (const auto& kv : polygons_) {
      geometry_msgs::msg::PolygonStamped poly_msg;
      poly_msg.header.stamp = now();
      poly_msg.header.frame_id = base_frame_;
      for (const auto& pt : kv.second.points) {
        geometry_msgs::msg::Point32 p;
        p.x = pt.x;
        p.y = pt.y;
        p.z = 0.0;
        poly_msg.polygon.points.push_back(p);
      }
      polygon_pub_->publish(poly_msg);
    }
  }

  // 4. 获取 TF 变换（lidar_frame → base_frame）
  geometry_msgs::msg::TransformStamped transform;
  try {
    transform = tf_buffer_->lookupTransform(base_frame_, msg->header.frame_id, tf2::TimePointZero);
  } catch (tf2::TransformException &ex) {
    RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 1.0,
                         "Cannot get transform from %s to %s: %s",
                         base_frame_.c_str(), msg->header.frame_id.c_str(), ex.what());
    return;
  }

  // 5. 转换激光点到 base_frame
  std::vector<geometry_msgs::msg::Point> points_in_base;
  double angle_min = msg->angle_min;
  double angle_increment = msg->angle_increment;
  for (size_t i = 0; i < msg->ranges.size(); ++i) {
    double range = msg->ranges[i];
    if (range < msg->range_min || range > msg->range_max) continue;
    double angle = angle_min + i * angle_increment;
    geometry_msgs::msg::PointStamped p_lidar, p_base;
    p_lidar.header.frame_id = msg->header.frame_id;
    p_lidar.point.x = range * cos(angle);
    p_lidar.point.y = range * sin(angle);
    p_lidar.point.z = 0.0;
    tf2::doTransform(p_lidar, p_base, transform);
    points_in_base.push_back(p_base.point);
  }

  // 6. 检测多边形，收集触发限制的方向
  std::vector<std::string> active_directions;
  for (auto& kv : polygons_) {
    const auto& cfg = kv.second;
    int count = 0;
    for (const auto& pt : points_in_base) {
      if (pointInPolygon(pt, cfg.points)) count++;
    }
    if (count >= cfg.min_points) {
      RCLCPP_DEBUG(get_logger(), "Polygon %s: %d points inside", kv.first.c_str(), count);
      if (cfg.action == "direction") {
        active_directions.push_back(cfg.direction);
      }
    }
  }

  // 7. 应用方向限制
  if (!active_directions.empty()) {
    geometry_msgs::msg::Twist cmd_out = active_cmd_.cmd;
    applyDirectionConstraints(cmd_out, active_directions);
    cmd_vel_pub_->publish(cmd_out);
  } else {
    // 无触发，直接透传
    cmd_vel_pub_->publish(active_cmd_.cmd);
  }
}

// ============================================================
// 点是否在多边形内（射线法）
// ============================================================
bool SimpleCollisionMonitor::pointInPolygon(const geometry_msgs::msg::Point& pt,
                                             const std::vector<geometry_msgs::msg::Point>& polygon) {
  bool inside = false;
  for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
    const auto& pi = polygon[i];
    const auto& pj = polygon[j];
    bool intersect = ((pi.y > pt.y) != (pj.y > pt.y)) &&
                      (pt.x < (pj.x - pi.x) * (pt.y - pi.y) / (pj.y - pi.y) + pi.x);
    if (intersect) inside = !inside;
  }
  return inside;
}

// ============================================================
// 应用方向限制
// ============================================================
void SimpleCollisionMonitor::applyDirectionConstraints(
    geometry_msgs::msg::Twist& cmd,
    const std::vector<std::string>& directions) {
  for (const auto& dir : directions) {
    if (dir == "forward") {
      if (cmd.linear.x > 0.0) cmd.linear.x = 0.0;
    } else if (dir == "backward") {
      if (cmd.linear.x < 0.0) cmd.linear.x = 0.0;
    } else if (dir == "left") {
      if (cmd.angular.z > 0.0) cmd.angular.z = 0.0;
      if (cmd.linear.y > 0.0) cmd.linear.y = 0.0;
    } else if (dir == "right") {
      if (cmd.angular.z < 0.0) cmd.angular.z = 0.0;
      if (cmd.linear.y < 0.0) cmd.linear.y = 0.0;
    }
  }
}

} // namespace collision_avoidance