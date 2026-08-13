#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
完整机器人启动（带调试 RViz）
- rosbridge (WebSocket 服务)
- Aurora 深度相机
- RTMP 推流（ros_to_rtmp.py）
- 导航 + AMCL + Nav2 + 方向感知避障
- 自动启动 RViz 用于调试
使用方法：
  ros2 launch bringup brain_full_debug_rosbridge.launch.py map:=/path/to/map.yaml
"""

from launch import LaunchDescription
from launch.actions import IncludeLaunchDescription, DeclareLaunchArgument, LogInfo
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch_ros.substitutions import FindPackageShare
from launch_ros.actions import Node
from launch.substitutions import PathJoinSubstitution, LaunchConfiguration, TextSubstitution


def generate_launch_description():
    # 声明 map 参数
    declare_map = DeclareLaunchArgument(
        'map',
        default_value='',
        description='⚠️ 必须指定地图 YAML 文件路径！例如: map:=/ros2_ws/src/map/map.yaml'
    )

    map_path = LaunchConfiguration('map')
    bringup_share = FindPackageShare('bringup')

    # ============================================================
    # 1. 启动 rosbridge
    # ============================================================
    rosbridge_launch = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            PathJoinSubstitution([bringup_share, 'launch', 'rosbridge.launch.py'])
        )
    )

    # ============================================================
    # 2. 启动 Aurora 相机
    # ============================================================
    aurora_launch = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            PathJoinSubstitution([bringup_share, 'launch', 'aurora_include.launch.py'])
        )
    )

    # ============================================================
    # 3. 启动 RTMP 推流（ros_to_rtmp.py）
    # ============================================================
    rtmp_node = Node(
        package='bringup',
        executable='ros_to_rtmp.py',
        name='ros_to_rtmp',
        output='screen',
    )

    # ============================================================
    # 4. 启动图像压缩（MJPEG）【暂时注释，需要时可启用】
    # ============================================================
    # compressor_launch = IncludeLaunchDescription(
    #     PythonLaunchDescriptionSource(
    #         PathJoinSubstitution([bringup_share, 'launch', 'image_compressor.launch.py'])
    #     )
    # )

    # ============================================================
    # 5. 启动导航 + 避障（传递 map 参数，开启 RViz）
    # ============================================================
    nav_launch = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            PathJoinSubstitution([bringup_share, 'launch', 'a_localization.launch.py'])
        ),
        launch_arguments={
            'map': map_path,
            'auto_rviz': 'true'
        }.items()
    )

    # 打印提示信息
    log_info = LogInfo(
        msg=[
            TextSubstitution(text='\n========================================\n'),
            TextSubstitution(text='🚀 完整系统启动\n'),
            TextSubstitution(text='   📷 相机: Aurora 930\n'),
            TextSubstitution(text='   🌐 frpc rosbridge: ws://localhost:9090\n'),
            TextSubstitution(text='   📡 RTMP 推流已启动\n'),
            TextSubstitution(text='   🗺️  地图: '),
            map_path,
            TextSubstitution(text='\n   🛡️ 避障已启用（手动控制生效）\n'),
            TextSubstitution(text='========================================\n'),
        ]
    )

    return LaunchDescription([
        declare_map,
        log_info,
        rosbridge_launch,
        aurora_launch,
        rtmp_node,                      # 新增 RTMP 推流
        # compressor_launch,            # 暂时注释
        nav_launch,
    ])