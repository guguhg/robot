#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
算法层启动文件 (Algorithms Bringup)
====================================
启动小脑的所有算法节点：

数据流:
  /imu/data_raw → imu_tools_node → /imu/data
  /cmd_vel → twist_node → /cmd_vel_limited
  /cmd_vel_limited → attitude_comp_node → /cmd_vel_compensated
  /cmd_vel_compensated → ik_node → /chassis/motor_cmd

节点列表:
  1. imu_tools_node          - IMU 姿态解算（四元数/欧拉角/旋转矩阵）
  2. twist_node              - 速度限幅 + 平滑滤波 + 超时保护
  3. attitude_comp_node      - IMU 姿态补偿（坡道/倾角修正）
  4. ik_node                 - 逆运动学（麦轮/差速/普通四轮）
  5. odom_node               - 里程计（待实现）
"""

import os
import yaml
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def get_config_path():
    """获取配置文件路径，优先级：环境变量 > 默认路径"""
    env_path = os.environ.get("ROBOT_CONFIG_PATH")
    if env_path and os.path.exists(env_path):
        return env_path
    
    default_paths = [
        "/ros2_ws/src/common/config/config.yaml",
        "/ros2_ws/install/common/share/common/config/config.yaml",
        "./config/config.yaml",
        "config/config.yaml",
    ]
    
    for path in default_paths:
        if os.path.exists(path):
            return path
    
    return None


def get_ros2_log_level():
    """从配置文件读取 ROS2 日志级别"""
    config_path = get_config_path()
    
    if config_path:
        try:
            with open(config_path, 'r') as f:
                config = yaml.safe_load(f)
                level = config.get('common', {}).get('logger', {}).get('ros2_level', 'INFO')
                return level.upper()
        except Exception as e:
            print(f"[Warning] Failed to parse config: {e}")
    
    return 'INFO'


def generate_launch_description():
    ros2_level = get_ros2_log_level()
    config_path = get_config_path()
    
    print(f"[Algorithms Bringup] Config path: {config_path}")
    print(f"[Algorithms Bringup] ROS2 log level: {ros2_level}")
    print(f"[Algorithms Bringup] Starting algorithm nodes...")

    return LaunchDescription([
        # ---------- 日志级别参数 ----------
        DeclareLaunchArgument(
            'log_level',
            default_value=ros2_level,
            description=f'Log level (default from config.yaml: {ros2_level})'
        ),

        # ============================================================
        # ① imu_tools_node (独立运行)
        # 订阅: /imu/data_raw (来自 imu_dri)
        # 发布: /imu/data (四元数 + 补偿后的角速度)
        # ============================================================
        Node(
            package='algorithms',
            executable='imu_tools_node',
            name='imu_tools',
            output='screen',
            emulate_tty=True,
            arguments=['--ros-args', '--log-level', LaunchConfiguration('log_level')],
        ),

        # ============================================================
        # ② twist_node
        # 订阅: /cmd_vel (来自大脑)
        # 处理: 速度限幅 + 平滑滤波 + 超时保护
        # 发布: /cmd_vel_limited
        # ============================================================
        Node(
            package='algorithms',
            executable='twist_node',
            name='twist_handler',
            output='screen',
            emulate_tty=True,
            arguments=['--ros-args', '--log-level', LaunchConfiguration('log_level')],
        ),

        # ============================================================
        # ③ attitude_compensator_node
        # 订阅: /cmd_vel_limited (来自 twist_node)
        #       /imu/data (来自 imu_tools_node)
        # 处理: IMU 姿态补偿 (坡道/倾角修正)
        # 发布: /cmd_vel_compensated
        # ============================================================
        Node(
            package='algorithms',
            executable='attitude_comp_node',
            name='attitude_compensator',
            output='screen',
            emulate_tty=True,
            arguments=['--ros-args', '--log-level', LaunchConfiguration('log_level')],
        ),

        # ============================================================
        # ④ inverse_kinematics_node
        # 订阅: /cmd_vel_compensated (来自 attitude_compensator)
        # 处理: Twist → 四轮速度 (麦轮/差速/普通四轮 IK)
        # 发布: /chassis/motor_cmd (给 chassis_dri)
        # ============================================================
        Node(
            package='algorithms',
            executable='ik_node',
            name='inverse_kinematics',
            output='screen',
            emulate_tty=True,
            arguments=['--ros-args', '--log-level', LaunchConfiguration('log_level')],
        ),

        # ============================================================
        # ⑤ fk_odometry_node (待实现)
        # 订阅: /chassis/motor_states (来自 chassis_dri)
        # 处理: FK + 积分 → 位置/姿态
        # 发布: /odom, /tf
        # ============================================================
        # Node(
        #     package='algorithms',
        #     executable='odom_node',
        #     name='fk_odometry',
        #     output='screen',
        #     emulate_tty=True,
        #     arguments=['--ros-args', '--log-level', LaunchConfiguration('log_level')],
        # ),
    ])