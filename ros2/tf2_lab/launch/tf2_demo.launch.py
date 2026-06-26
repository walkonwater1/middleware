"""
tf2_demo.launch.py — 一键启动 TF2 变换链实验

启动 3 个节点:
  1. static_broadcaster   — 发布 map→odom 静态变换
  2. robot_broadcaster    — 发布 odom→base_link 动态 + base_link→laser 静态
  3. transform_listener   — 监听并计算全变换链

用法:
  ros2 launch tf2_lab tf2_demo.launch.py

验证:
  ros2 run tf2_tools tf2_echo map laser_frame
  ros2 run tf2_tools view_frames
"""

from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    """生成 Launch 描述 — 3 个节点同时启动, 构建完整 tf tree"""

    static_broadcaster = Node(
        package='tf2_lab',
        executable='static_broadcaster',
        name='static_broadcaster',
        output='screen',
    )

    robot_broadcaster = Node(
        package='tf2_lab',
        executable='robot_broadcaster',
        name='robot_broadcaster',
        output='screen',
    )

    transform_listener = Node(
        package='tf2_lab',
        executable='transform_listener',
        name='transform_listener',
        output='screen',
    )

    return LaunchDescription([
        static_broadcaster,
        robot_broadcaster,
        transform_listener,
    ])
