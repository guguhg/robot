#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
导航模式：使用指定地图 + AMCL 定位 + Nav2 导航 + 深度相机（点云避障）
使用方法：
  ros2 launch bringup brain_localization.launch.py map:=/path/to/map.yaml
"""

import os
from launch import LaunchDescription
from launch.actions import (
    RegisterEventHandler,
    TimerAction,
    LogInfo,
    GroupAction,
    DeclareLaunchArgument,
    IncludeLaunchDescription          # ✅ 新增
)
from launch.launch_description_sources import PythonLaunchDescriptionSource  # ✅ 新增
from launch.event_handlers import OnProcessExit
from launch.substitutions import (
    PathJoinSubstitution,
    Command,
    LaunchConfiguration,
    TextSubstitution,
    PythonExpression
)
from launch_ros.substitutions import FindPackageShare
from launch_ros.actions import Node
from launch.conditions import IfCondition


def generate_launch_description():
    # ============================================================
    # 1. 声明参数
    # ============================================================
    declare_map_arg = DeclareLaunchArgument(
        'map',
        default_value='',
        description='⚠️ 必须指定地图 YAML 文件路径！例如: map:=/ros2_ws/src/map/map.yaml'
    )

    declare_auto_rviz_arg = DeclareLaunchArgument(
        'auto_rviz',
        default_value='true',
        description='是否自动启动 RViz'
    )

    # ============================================================
    # 2. 获取配置变量
    # ============================================================
    map_path = LaunchConfiguration('map')
    auto_rviz = LaunchConfiguration('auto_rviz')
    USE_SIM_TIME = False

    has_map = PythonExpression(['"', map_path, '" != ""'])

    description_share = FindPackageShare('description')
    bringup_share = FindPackageShare('bringup')          # ✅ 新增

    urdf_xacro_path = PathJoinSubstitution([
        description_share, 'urdf', 'description.urdf.xacro'
    ])
    controllers_yaml_path = PathJoinSubstitution([
        description_share, 'config', 'brain_controllers.yaml'
    ])
    ekf_yaml_path = PathJoinSubstitution([
        description_share, 'config', 'ekf.yaml'
    ])
    # ✅ 改为点云版 Nav2 参数
    nav2_params_path = PathJoinSubstitution([
        description_share, 'config', 'nav2_params_pointcloud.yaml'
    ])
    rviz_config_path = PathJoinSubstitution([
        description_share, 'rviz', 'view_pointcloud.rviz'
    ])

    # ============================================================
    # ✅ 3. 深度相机启动（Aurora 930）
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
    # 4. 打印启动信息（修改提示）
    # ============================================================
    log_info = LogInfo(
        msg=[
            TextSubstitution(text='\n========================================\n'),
            TextSubstitution(text='🧭 导航模式 + 深度点云避障启动\n'),
            TextSubstitution(text='   📍 定位方式: AMCL（自适应蒙特卡洛定位）\n'),
            TextSubstitution(text='   🗺️  使用地图: '),
            PythonExpression(['"', map_path, '"' if has_map else '❌ 未指定！请使用 map:= 参数']),
            TextSubstitution(text='\n   📷 深度点云已启用（/aurora/points2）\n'),
            TextSubstitution(text='   🛡️ 点云避障已集成到局部代价地图\n'),
            TextSubstitution(text='========================================\n'),
        ]
    )

    # ============================================================
    # 5. 基础节点
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
    # 6. Controller Spawners
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
    # 7. 激光雷达
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
    # 8. EKF（延迟 2 秒）
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
    # 9. Map Server（加载地图）
    # ============================================================
    map_server_node = Node(
        package='nav2_map_server',
        executable='map_server',
        name='map_server',
        output='screen',
        parameters=[
            {'yaml_filename': map_path},
            {'use_sim_time': USE_SIM_TIME},
        ],
        condition=IfCondition(has_map),
    )

    # ============================================================
    # 10. Nav2 核心节点
    # ============================================================
    amcl_node = Node(
        package='nav2_amcl',
        executable='amcl',
        name='amcl',
        output='screen',
        parameters=[nav2_params_path],
        condition=IfCondition(has_map),
    )

    nav2_planner = Node(
        package='nav2_planner',
        executable='planner_server',
        name='planner_server',
        output='screen',
        parameters=[nav2_params_path],
        condition=IfCondition(has_map),
    )

    nav2_controller = Node(
        package='nav2_controller',
        executable='controller_server',
        name='controller_server',
        output='screen',
        parameters=[nav2_params_path],
        condition=IfCondition(has_map),
    )

    nav2_behaviors = Node(
        package='nav2_behaviors',
        executable='behavior_server',
        name='behavior_server',
        output='screen',
        parameters=[nav2_params_path],
        condition=IfCondition(has_map),
    )

    nav2_bt_navigator = Node(
        package='nav2_bt_navigator',
        executable='bt_navigator',
        name='bt_navigator',
        output='screen',
        parameters=[nav2_params_path],
        condition=IfCondition(has_map),
    )

    nav2_waypoint_follower = Node(
        package='nav2_waypoint_follower',
        executable='waypoint_follower',
        name='waypoint_follower',
        output='screen',
        parameters=[nav2_params_path],
        condition=IfCondition(has_map),
    )

    # ✅ lifecycle_manager 包含 map_server，由它负责激活
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
                'map_server',                  # 激活地图
                'amcl',
                'planner_server',
                'controller_server',
                'behavior_server',
                'bt_navigator',
                'waypoint_follower',
            ],
        }],
        condition=IfCondition(has_map),
    )

    nav2_nodes = GroupAction([
        amcl_node,
        nav2_planner,
        nav2_controller,
        nav2_behaviors,
        nav2_bt_navigator,
        nav2_waypoint_follower,
        lifecycle_manager,
    ])

    nav2_with_delay = TimerAction(period=8.0, actions=[nav2_nodes])

    # ============================================================
    # 11. 键盘控制（可选）
    # ============================================================
    teleop_node = Node(
        package='teleop_twist_keyboard',
        executable='teleop_twist_keyboard',
        name='teleop_twist_keyboard',
        output='screen',
        prefix=['xterm -e'],
        parameters=[{'use_sim_time': USE_SIM_TIME}],
        condition=IfCondition(has_map),
    )

    # ============================================================
    # 12. RViz
    # ============================================================
    rviz_node = Node(
        package='rviz2',
        executable='rviz2',
        name='rviz2',
        output='screen',
        prefix=['env LIBGL_ALWAYS_SOFTWARE=1'],
        arguments=['-d', rviz_config_path],
        parameters=[{'use_sim_time': USE_SIM_TIME}],
        condition=IfCondition(auto_rviz),
    )

    # ============================================================
    # 13. 启动顺序
    # ============================================================
    return LaunchDescription([
        declare_map_arg,
        declare_auto_rviz_arg,
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
                    aurora_include,          # ✅ 深度相机启动（点云已开启）
                    ekf_with_delay,
                    map_server_node,
                    nav2_with_delay,
                    teleop_node,
                    rviz_node,
                ],
            )
        ),
    ])