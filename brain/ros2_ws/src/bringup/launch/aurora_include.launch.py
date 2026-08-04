#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
深度相机启动文件（Aurora 930）
供其他 launch 文件 include 使用
"""

from launch import LaunchDescription
from launch.actions import IncludeLaunchDescription
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch_ros.substitutions import FindPackageShare
from launch.substitutions import PathJoinSubstitution


def generate_launch_description():
    aurora_launch = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            PathJoinSubstitution([
                FindPackageShare('deptrum-ros-driver-aurora930'),
                'launch',
                'aurora930_launch.py'
            ])
        ),
        launch_arguments={
            'resolution_mode_index': '0',      # 最低分辨率0,1,2
            'rgb_fps': '15',                   # RGB 帧率
            'rgb_enable': 'true',              # 开启 RGB 流
            'ir_enable': 'false',              # 关闭 IR 流
            'depth_enable': 'false',           # 关闭深度流
            'point_cloud_enable': 'false',     # 关闭点云
            'align_mode': 'false',             # 无需对齐（深度关闭）
            'depth_correction': 'false',       # 无需深度校正（深度关闭）
        }.items()
    )

    return LaunchDescription([
        aurora_launch
    ])