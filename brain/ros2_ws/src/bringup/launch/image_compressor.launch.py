#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
图像压缩节点
将原始 RGB 图像转换为 MJPEG 压缩格式，减少网络传输带宽
输入: /aurora/rgb/image_raw
输出: /aurora/rgb/image_raw/compressed
"""

from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    return LaunchDescription([

        Node(
            package='image_transport',
            executable='republish',
            name='image_compressor',
            arguments=['compressed', 'raw'],
            remappings=[
                ('in', '/aurora/rgb/image_raw'),
                ('out', '/aurora/rgb/image_raw/compressed')
            ],
            parameters=[{
                'image_transport': 'compressed',
                'compressed.jpeg_quality': 70,   # JPEG 质量 0-100，70 是平衡值
            }],
            output='screen',
        ),

    ])