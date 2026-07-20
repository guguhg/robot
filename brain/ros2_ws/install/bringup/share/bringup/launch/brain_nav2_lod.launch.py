#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Brain Robot Nav2 启动文件
支持两种模式：
  1. 导航模式（使用已有地图）：ros2 launch bringup brain_nav2.launch.py map:=/path/to/map.yaml
  2. 边建图边导航（无地图）：ros2 launch bringup brain_nav2.launch.py
"""

import os
from launch import LaunchDescription
from launch.actions import (
    RegisterEventHandler, 
    TimerAction, 
    DeclareLaunchArgument,
    LogInfo,
    GroupAction
)
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
from launch.conditions import IfCondition, UnlessCondition


def generate_launch_description():
    # ============================================================
    # 1. 声明启动参数
    # ============================================================
    if 'DEPTH_CAMERA_TYPE' not in os.environ:
        os.environ['DEPTH_CAMERA_TYPE'] = 'AsCamera'
        
    declare_map_arg = DeclareLaunchArgument(
        'map',
        default_value='',
        description='地图 YAML 文件路径。留空则使用 SLAM 建图模式（边建图边导航）'
    )
    
    declare_nav2_params_arg = DeclareLaunchArgument(
        'nav2_params',
        default_value=PathJoinSubstitution([
            FindPackageShare('description'), 'config', 'nav2_params.yaml'
        ]),
        description='Nav2 配置文件路径'
    )
    
    declare_rviz_arg = DeclareLaunchArgument(
        'rviz_config',
        default_value=PathJoinSubstitution([
            FindPackageShare('description'), 'rviz', 'view.rviz'
        ]),
        description='RViz 配置文件路径'
    )
    
    declare_auto_rviz_arg = DeclareLaunchArgument(
        'auto_rviz',
        default_value='true',
        description='是否自动启动 RViz'
    )
    
    declare_use_sim_time_arg = DeclareLaunchArgument(
        'use_sim_time',
        default_value='false',
        description='是否使用仿真时间'
    )
    
    # ============================================================
    # 2. 获取 Launch 配置变量
    # ============================================================
    
    map_path = LaunchConfiguration('map')
    nav2_params_path = LaunchConfiguration('nav2_params')
    rviz_config_path = LaunchConfiguration('rviz_config')
    auto_rviz = LaunchConfiguration('auto_rviz')
    use_sim_time = LaunchConfiguration('use_sim_time')
    
    # 判断是否有地图（用于条件启动）
    has_map = PythonExpression(['"', map_path, '" != ""'])
    
    # ============================================================
    # 3. 获取路径
    # ============================================================
    
    bringup_share = FindPackageShare('bringup')
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
    
    slam_params_path = PathJoinSubstitution([
        description_share, 'config', 'slam_params.yaml'
    ])
    
    # ============================================================
    # 4. 打印启动模式信息
    # ============================================================
    
    log_mode = LogInfo(
        msg=[
            TextSubstitution(text='\n========================================\n'),
            TextSubstitution(text='🤖 Brain Robot Nav2 启动模式: '),
            PythonExpression([
                '"📂 使用已有地图: " + "', map_path, '"' if has_map else '🔄 边建图边导航（SLAM 模式）"'
            ]),
            TextSubstitution(text='\n'),
            PythonExpression([
                '"📍 SLAM 模式: localization（定位）"' if has_map else '📍 SLAM 模式: mapping（建图）"'
            ]),
            TextSubstitution(text='\n========================================\n'),
        ]
    )
    
    # ============================================================
    # 5. 基础节点（robot_state_publisher + controller_manager）
    # ============================================================
    
    robot_state_pub_node = Node(
        package='robot_state_publisher',
        executable='robot_state_publisher',
        name='robot_state_publisher',
        output='screen',
        parameters=[{
            'robot_description': Command(['xacro ', urdf_xacro_path]),
            'use_sim_time': use_sim_time,
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
            {'use_sim_time': use_sim_time},
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
        parameters=[{'use_sim_time': use_sim_time}],
    )
    
    brain_controller_spawner = Node(
        package='controller_manager',
        executable='spawner',
        name='spawn_brain_controller',
        arguments=['brain_controller', '--controller-manager', '/controller_manager'],
        output='screen',
        parameters=[{'use_sim_time': use_sim_time}],
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
            {'use_sim_time': use_sim_time},
        ],
        remappings=[('/MS200/scan', '/scan')],
    )
    
    # ============================================================
    # 8. EKF（延迟 2 秒启动）
    # ============================================================
    
    ekf_node = Node(
        package='robot_localization',
        executable='ekf_node',
        name='ekf_filter_node',
        output='screen',
        parameters=[
            ekf_yaml_path,
            {'use_sim_time': use_sim_time},
        ],
    )
    
    ekf_with_delay = TimerAction(
        period=2.0,
        actions=[ekf_node],
    )
    
    # ============================================================
    # 9. SLAM（根据是否有地图自动切换模式）
    # ============================================================
    
    # SLAM 模式：有地图 → localization，无地图 → mapping
    slam_mode = PythonExpression([
        '"localization" if "', map_path, '" != "" else "mapping"'
    ])
    
    slam_node = Node(
        package='slam_toolbox',
        executable='async_slam_toolbox_node',
        name='slam_toolbox',
        output='screen',
        parameters=[
            slam_params_path,
            {'use_sim_time': use_sim_time},
            {'mode': slam_mode},  # ✅ 自动切换模式
        ],
        # 有地图时也启动 SLAM（用于定位），无地图时建图
    )
    
    slam_with_delay = TimerAction(
        period=5.0,
        actions=[slam_node],
    )
    
    # ============================================================
    # 10. Map Server（加载已有地图，仅在指定地图时启动）
    # ============================================================
    
    map_server_node = Node(
        package='nav2_map_server',
        executable='map_server',
        name='map_server',
        output='screen',
        parameters=[
            {'yaml_filename': map_path},
            {'use_sim_time': use_sim_time},
        ],
        condition=IfCondition(has_map),
    )
    
    # ============================================================
    # Nav2 节点（Humble 版本）
    # ============================================================

    # AMCL 定位节点
    amcl_node = Node(
        package='nav2_amcl',
        executable='amcl',
        name='amcl',
        output='screen',
        parameters=[nav2_params_path],
        #condition=IfCondition(has_map),
    )

    # Nav2 核心节点
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

    # Humble 版本：behavior_server
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

    # Nav2 生命周期管理器
    lifecycle_manager = Node(
        package='nav2_lifecycle_manager',
        executable='lifecycle_manager',
        name='lifecycle_manager_navigation',
        output='screen',
        parameters=[{
            'use_sim_time': use_sim_time,
            'autostart': True,
            'node_names': [
                'planner_server',
                'controller_server',
                'behavior_server',  
                'bt_navigator',
                'waypoint_follower',
            ] + (['amcl'] if has_map else []),
        }],
    )

    # Nav2 节点组合
    nav2_nodes = GroupAction([
        amcl_node,
        nav2_planner,
        nav2_controller,
        nav2_behaviors,
        nav2_bt_navigator,
        nav2_waypoint_follower,
        lifecycle_manager,
    ])
    
    # Nav2 延迟启动（等待 EKF 和 SLAM 初始化完成）
    nav2_with_delay = TimerAction(
        period=10.0,  # 等待 EKF(2s) + SLAM(5s) + 额外缓冲
        actions=[nav2_nodes],
    )
    
    # ============================================================
    # 12. RViz（可配置是否自动启动）
    # ============================================================
    
    rviz_node = Node(
        package='rviz2',
        executable='rviz2',
        name='rviz2',
        output='screen',
        arguments=['-d', rviz_config_path],
        parameters=[{'use_sim_time': use_sim_time}],
        condition=IfCondition(auto_rviz),
    )
    
    # ============================================================
    # 13. 完整的启动顺序
    # ============================================================
    
    return LaunchDescription([
        # 声明参数
        declare_map_arg,
        declare_nav2_params_arg,
        declare_rviz_arg,
        declare_auto_rviz_arg,
        declare_use_sim_time_arg,
        
        # 打印启动模式
        log_mode,
        
        # === 第一阶段：基础节点 ===
        robot_state_pub_node,
        control_node,
        joint_state_broadcaster_spawner,
        
        # === 第二阶段：brain_controller ===
        RegisterEventHandler(
            event_handler=OnProcessExit(
                target_action=joint_state_broadcaster_spawner,
                on_exit=[brain_controller_spawner],
            )
        ),
        
        # === 第三阶段：所有传感器 + 定位 + 建图 + 导航 ===
        RegisterEventHandler(
            event_handler=OnProcessExit(
                target_action=brain_controller_spawner,
                on_exit=[
                    lidar_node,           # 激光雷达
                    ekf_with_delay,       # EKF（延迟 2 秒）
                    map_server_node,      # 地图服务器（有地图时）
                    slam_with_delay,      # SLAM（延迟 5 秒，模式自动切换）
                    nav2_with_delay,      # Nav2（延迟 10 秒）
                    rviz_node,            # RViz（可选）
                ],
            )
        ),
    ])