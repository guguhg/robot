#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
边建图边导航模式 + 深度相机
使用方法：
  ros2 launch bringup brain_slam_nav_pointcloud.launch.py
  建图过程中可以同时进行导航，建图完成后手动保存地图：
    ros2 run nav2_map_server map_saver_cli -f /ros2_ws/src/map/map_manual
"""

import os
from launch import LaunchDescription
from launch.actions import (
    RegisterEventHandler,
    TimerAction,
    LogInfo,
    GroupAction,
    IncludeLaunchDescription
)
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.event_handlers import OnProcessExit
from launch.substitutions import PathJoinSubstitution, Command, TextSubstitution
from launch_ros.substitutions import FindPackageShare
from launch_ros.actions import Node


def generate_launch_description():
    description_share = FindPackageShare('description')
    bringup_share = FindPackageShare('bringup')

    # ============================================================
    # 路径配置
    # ============================================================
    urdf_xacro_path = PathJoinSubstitution([
        description_share, 'urdf', 'description.urdf.xacro'
    ])
    controllers_yaml_path = PathJoinSubstitution([
        description_share, 'config', 'brain_controllers.yaml'
    ])
    ekf_yaml_path = PathJoinSubstitution([
        description_share, 'config', 'ekf.yaml'
    ])
    slam_params_path = PathJoinSubstitution([
        description_share, 'config', 'slam_params.yaml'
    ])
    # 使用点云版 Nav2 参数
    nav2_params_path = PathJoinSubstitution([
        description_share, 'config', 'nav2_params_pointcloud.yaml'
    ])
    rviz_config_path = PathJoinSubstitution([
        description_share, 'rviz', 'view_pointcloud.rviz'
    ])

    USE_SIM_TIME = False

    # ============================================================
    # 深度相机启动（Aurora 930，点云已关闭）
    # ============================================================
    aurora_include = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            PathJoinSubstitution([
                bringup_share,
                'launch',
                'aurora_include.launch.py'
            ])
        )
    )

    # ============================================================
    # 打印启动信息（修改提示，移除点云相关文字）
    # ============================================================
    log_info = LogInfo(
        msg=[
            TextSubstitution(text='\n========================================\n'),
            TextSubstitution(text='🗺️  边建图边导航 + 深度点云避障启动\n'),
            TextSubstitution(text='   📍 使用 SLAM Toolbox 建图（mapping 模式）\n'),
            TextSubstitution(text='   🧭 Nav2 同时运行，可发送导航目标，关闭 AMCL+map_server，使用 SLAM 发布 /map\n'),
            TextSubstitution(text='   📷 深度点云已启用（/aurora/points2）\n'),
            TextSubstitution(text='   🛡️ 点云避障已集成到局部代价地图\n'),
            TextSubstitution(text='   💾 建图完成后手动保存:\n'),
            TextSubstitution(text='      ros2 run nav2_map_server map_saver_cli -f /ros2_ws/src/map/map_manual\n'),
            TextSubstitution(text='========================================\n'),
        ]
    )

    # ============================================================
    # 基础节点
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
    # Controller Spawners
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
    # 激光雷达
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
    # EKF（延迟 2 秒）
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
    # SLAM（建图模式）
    # ============================================================
    slam_node = Node(
        package='slam_toolbox',
        executable='async_slam_toolbox_node',
        name='slam_toolbox',
        output='screen',
        parameters=[
            slam_params_path,
            {'use_sim_time': USE_SIM_TIME},
            {'mode': 'mapping'},
        ],
    )
    slam_with_delay = TimerAction(period=5.0, actions=[slam_node])

    # ============================================================
    # Nav2 核心节点（无 AMCL，无 map_server）
    # ============================================================
    nav2_planner = Node(
        package='nav2_planner',
        executable='planner_server',
        name='planner_server',
        output='screen',
        parameters=[nav2_params_path],
    )

    nav2_controller = Node(
        package='nav2_controller',
        executable='controller_server',
        name='controller_server',
        output='screen',
        parameters=[nav2_params_path],
    )

    nav2_behaviors = Node(
        package='nav2_behaviors',
        executable='behavior_server',
        name='behavior_server',
        output='screen',
        parameters=[nav2_params_path],
    )

    nav2_bt_navigator = Node(
        package='nav2_bt_navigator',
        executable='bt_navigator',
        name='bt_navigator',
        output='screen',
        parameters=[nav2_params_path],
    )

    nav2_waypoint_follower = Node(
        package='nav2_waypoint_follower',
        executable='waypoint_follower',
        name='waypoint_follower',
        output='screen',
        parameters=[nav2_params_path],
    )

    lifecycle_manager = Node(
        package='nav2_lifecycle_manager',
        executable='lifecycle_manager',
        name='lifecycle_manager_navigation',
        output='screen',
        parameters=[{
            'use_sim_time': USE_SIM_TIME,
            'autostart': True,
            'bond_timeout': 15.0,
            'node_names': [
                'planner_server',
                'controller_server',
                'behavior_server',
                'bt_navigator',
                'waypoint_follower',
            ],
        }],
    )

    nav2_nodes = GroupAction([
        nav2_planner,
        nav2_controller,
        nav2_behaviors,
        nav2_bt_navigator,
        nav2_waypoint_follower,
        lifecycle_manager,
    ])

    nav2_with_delay = TimerAction(period=8.0, actions=[nav2_nodes])

    # ============================================================
    # 键盘控制（独立终端）
    # ============================================================
    teleop_node = Node(
        package='teleop_twist_keyboard',
        executable='teleop_twist_keyboard',
        name='teleop_twist_keyboard',
        output='screen',
        prefix=['xterm -hold -e'],
        parameters=[{'use_sim_time': USE_SIM_TIME}],
    )

    # ============================================================
    # RViz
    # ============================================================
    rviz_node = Node(
        package='rviz2',
        executable='rviz2',
        name='rviz2',
        output='screen',
        prefix=['env LIBGL_ALWAYS_SOFTWARE=1'],
        arguments=['-d', rviz_config_path],
        parameters=[{'use_sim_time': USE_SIM_TIME}],
    )

    # ============================================================
    # 启动顺序
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
                    aurora_include,        
                    ekf_with_delay,
                    slam_with_delay,
                    nav2_with_delay,
                    teleop_node,
                    rviz_node,
                ],
            )
        ),
    ])