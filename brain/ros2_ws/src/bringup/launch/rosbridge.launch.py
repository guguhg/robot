from launch import LaunchDescription
from launch_ros.actions import Node

def generate_launch_description():
    return LaunchDescription([

        # 第一个 rosbridge 实例，监听默认 9090 端口
        Node(
            package='rosbridge_server',
            executable='rosbridge_websocket',
            name='rosbridge_websocket_9090',
            output='screen',
            parameters=[{
                'port': 9090,
                'max_message_size': 10000000,
            }],
        ),

        # 第二个 rosbridge 实例，监听 9091 端口
        Node(
            package='rosbridge_server',
            executable='rosbridge_websocket',
            name='rosbridge_websocket_9091',
            output='screen',
            parameters=[{
                'port': 9091,
                'max_message_size': 10000000,
            }],
        ),
    ])