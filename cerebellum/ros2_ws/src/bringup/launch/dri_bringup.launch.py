#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
小脑驱动节点启动文件
只需启动一个 cerebellum_dri 节点，内部包含所有功能
"""

import os
import yaml
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def get_config_path():
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
    
    print(f"[Bringup] Config path: {config_path}")
    print(f"[Bringup] ROS2 log level: {ros2_level}")

    return LaunchDescription([
        DeclareLaunchArgument(
            'log_level',
            default_value=ros2_level,
            description=f'Log level (default from config.yaml: {ros2_level})'
        ),

        Node(
            package='dri_interfaces',
            executable='cerebellum_dri',
            name='cerebellum_driver',
            output='screen',
            emulate_tty=True,
            arguments=['--ros-args', '--log-level', LaunchConfiguration('log_level')],
        ),
    ])