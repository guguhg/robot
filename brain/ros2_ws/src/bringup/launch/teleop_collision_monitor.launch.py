#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
遥控避障模式：键盘遥控 + Collision Monitor 避障 + RViz 可视化
使用方法：
  ros2 launch bringup teleop_collisionMonitor.launch.py
  
该文件已废弃
"""

import os
from launch import LaunchDescription
from launch.actions import (
    RegisterEventHandler,
    TimerAction,
    LogInfo,
)
from launch.substitutions import TextSubstitution
from launch.event_handlers import OnProcessExit
from launch.substitutions import (
    PathJoinSubstitution,
    Command,
    LaunchConfiguration,
)
from launch_ros.substitutions import FindPackageShare
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory


def generate_launch_description():
    USE_SIM_TIME = False

    # ============================================================
    # 配置文件路径
    # ============================================================
    description_share = FindPackageShare('description')

    urdf_xacro_path = PathJoinSubstitution([
        description_share, 'urdf', 'description.urdf.xacro'
    ])
    controllers_yaml_path = PathJoinSubstitution([
        description_share, 'config', 'brain_controllers.yaml'
    ])
    ekf_yaml_path = PathJoinSubstitution([
        description_share, 'config', 'ekf.yaml'
    ])
    collision_monitor_yaml_path = PathJoinSubstitution([
        description_share, 'config', 'collision_monitor.yaml'
    ])
    rviz_config_path = PathJoinSubstitution([
        description_share, 'rviz', 'view_collisionMonitor.rviz'
    ])

    # ============================================================
    # 启动信息
    # ============================================================
    log_info = LogInfo(
        msg=[
            TextSubstitution(text='\n========================================\n'),
            TextSubstitution(text='🎮 遥控避障模式启动\n'),
            TextSubstitution(text='   🕹️  键盘遥控: teleop_twist_keyboard\n'),
            TextSubstitution(text='   🛡️  避障: Collision Monitor\n'),
            TextSubstitution(text='   📊  RViz 可视化已开启\n'),
            TextSubstitution(text='========================================\n'),
        ]
    )

    # ============================================================
    # 1. Robot State Publisher（TF: base_link → lidar_frame）
    # ============================================================
    robot_state_pub_node = Node(
        package='robot_state_publisher',
        executable='robot_state_publisher',
        name='robot_state_publisher',
        output='screen',
        parameters=[{
            'robot_description': Command(['xacro ', urdf_xacro_path]),
            'use_sim_time': USE_SIM_TIME,
            'publish_frequency': 50.0,
        }],
    )

    # ============================================================
    # 2. Controller Manager（硬件接口）
    # ============================================================
    control_node = Node(
        package='controller_manager',
        executable='ros2_control_node',
        name='controller_manager',
        output='screen',
        parameters=[
            controllers_yaml_path,
            {'robot_description': Command(['xacro ', urdf_xacro_path])},
            {'use_sim_time': USE_SIM_TIME},
        ],
    )

    # ============================================================
    # 3. Controller Spawners
    # ============================================================
    joint_state_broadcaster_spawner = Node(
        package='controller_manager',
        executable='spawner',
        name='spawn_joint_state_broadcaster',
        arguments=['joint_state_broadcaster', '--controller-manager', '/controller_manager'],
        output='screen',
        parameters=[{'use_sim_time': USE_SIM_TIME}],
    )

    brain_controller_spawner = Node(
        package='controller_manager',
        executable='spawner',
        name='spawn_brain_controller',
        arguments=['brain_controller', '--controller-manager', '/controller_manager'],
        output='screen',
        parameters=[{'use_sim_time': USE_SIM_TIME}],
    )

    # ============================================================
    # 4. 激光雷达（/scan）
    # ============================================================
    lidar_node = Node(
        package='oradar_lidar',
        executable='oradar_scan',
        name='MS200',
        output='screen',
        parameters=[
            {'device_model': 'MS200'},
            {'frame_id': 'lidar_frame'},
            {'scan_topic': 'MS200/scan'},
            {'port_name': '/dev/ttyUSB0'},
            {'baudrate': 230400},
            {'angle_min': 0.0},
            {'angle_max': 360.0},
            {'range_min': 0.05},
            {'range_max': 20.0},
            {'clockwise': False},
            {'motor_speed': 10},
            {'use_sim_time': USE_SIM_TIME},
        ],
        remappings=[('/MS200/scan', '/scan')],
    )

    # ============================================================
    # 5. EKF（TF: odom → base_link，延迟 2 秒）
    # ============================================================
    ekf_node = Node(
        package='robot_localization',
        executable='ekf_node',
        name='ekf_filter_node',
        output='screen',
        parameters=[
            ekf_yaml_path,
            {'use_sim_time': USE_SIM_TIME},
        ],
    )
    ekf_with_delay = TimerAction(period=2.0, actions=[ekf_node])

    # ============================================================
    # 6. 键盘遥控（/cmd_vel）
    # ============================================================
    teleop_keyboard_node = Node(
        package='teleop_twist_keyboard',
        executable='teleop_twist_keyboard',
        name='teleop_twist_keyboard',
        output='screen',
        prefix=['xterm -e'],
        parameters=[{'use_sim_time': USE_SIM_TIME}],
    )

    # ============================================================
    # 7. Collision Monitor（/cmd_vel → /cmd_vel_safe）
    # ============================================================
    collision_monitor_node = Node(
        package='nav2_collision_monitor',
        executable='collision_monitor',
        name='collision_monitor',
        output='screen',
        parameters=[
            collision_monitor_yaml_path,
            {'use_sim_time': USE_SIM_TIME},
        ],
        arguments=['--ros-args', '--log-level', 'debug'],
    )

    # ============================================================
    # 8. Lifecycle Manager（激活 collision_monitor）
    # ============================================================
    lifecycle_manager = Node(
        package='nav2_lifecycle_manager',
        executable='lifecycle_manager',
        name='lifecycle_manager_collision',
        output='screen',
        parameters=[{
            'use_sim_time': USE_SIM_TIME,
            'autostart': True,
            'bond_timeout': 15.0,
            'node_names': ['collision_monitor'],
        }],
    )

    # ============================================================
    # 9. RViz（可视化）
    # ============================================================
    rviz_node = Node(
        package='rviz2',
        executable='rviz2',
        name='rviz2',
        output='screen',
        arguments=['-d', rviz_config_path],
        parameters=[{'use_sim_time': USE_SIM_TIME}],
    )

    # ============================================================
    # 10. 启动顺序
    # ============================================================
    return LaunchDescription([
        log_info,

        robot_state_pub_node,
        control_node,
        joint_state_broadcaster_spawner,

        RegisterEventHandler(
            event_handler=OnProcessExit(
                target_action=joint_state_broadcaster_spawner,
                on_exit=[brain_controller_spawner],
            )
        ),

        RegisterEventHandler(
            event_handler=OnProcessExit(
                target_action=brain_controller_spawner,
                on_exit=[
                    lidar_node,
                    ekf_with_delay,
                    teleop_keyboard_node,
                    collision_monitor_node,
                    TimerAction(period=3.0, actions=[lifecycle_manager]),
                    rviz_node,
                ],
            )
        ),
    ])