"""
demo_launch.py — ROS2 Python Launch 文件

一个 launch 文件启动多个节点, 替代手动开 N 个终端。

用法:
  ros2 launch launch_lab demo_launch.py

  # 带参数启动:
  ros2 launch launch_lab demo_launch.py talker_rate:=2.0

工程意义:
  - 复杂机器人系统有 10+ 节点, 必须用 launch 编排
  - 支持条件启动、参数传递、命名空间隔离
  - 对标 ROS1 的 roslaunch, 但更强大
"""

from launch import LaunchDescription
from launch_ros.actions import Node
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration


def generate_launch_description():
    """生成 Launch 描述 — 这是 ROS2 launch 的入口函数"""

    # ★ 声明可配置参数 (可在命令行覆盖)
    talker_rate_arg = DeclareLaunchArgument(
        'talker_rate', default_value='1.0',
        description='Talker 发布频率 (Hz)'
    )

    # ★ 定义 Talker 节点
    talker_node = Node(
        package='launch_lab',
        executable='talker',
        name='my_talker',           # 可自定义节点名
        output='screen',            # 输出到终端
        parameters=[{
            'talker_rate': LaunchConfiguration('talker_rate'),
        }],
        remappings=[                # ★ Topic 重映射
            ('/launch_topic', '/renamed_topic'),
        ],
    )

    # ★ 定义 Listener 节点
    listener_node = Node(
        package='launch_lab',
        executable='listener',
        name='my_listener',
        output='screen',
        remappings=[
            ('/launch_topic', '/renamed_topic'),  # 匹配 Talker 的重映射
        ],
    )

    # ★ 返回 LaunchDescription: 所有节点一起启动
    return LaunchDescription([
        talker_rate_arg,
        talker_node,
        listener_node,
    ])
