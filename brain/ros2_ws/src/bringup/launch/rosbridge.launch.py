#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
仅启动 rosbridge，用于外部控制测试
"""

from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    return LaunchDescription([

        Node(
            package='rosbridge_server',
            executable='rosbridge_websocket',
            name='rosbridge_websocket',
            output='screen',
            parameters=[{
                'port': 9090,
                'max_message_size': 10000000,
            }],
        ),

    ])
