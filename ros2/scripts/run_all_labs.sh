#!/bin/bash
# run_all_labs.sh — ROS2 实验一键启动菜单
set -e

WS="/home/lixin/eir/lixin/midlleware/ros2"
source /opt/ros/humble/setup.bash
source "$WS/install/setup.bash" 2>/dev/null || true

show_menu() {
    echo ""
    echo "╔══════════════════════════════════════════╗"
    echo "║  🤖 ROS2 实验 — 请选择                   ║"
    echo "╠══════════════════════════════════════════╣"
    echo "║  1) topic      基础Pub/Sub + 自定义消息  ║"
    echo "║  2) service    Service 请求/响应         ║"
    echo "║  3) action     Action 异步任务           ║"
    echo "║  4) qos        QoS 对比实验              ║"
    echo "║  5) lifecycle  Lifecycle 生命周期        ║"
    echo "║  6) compose    进程内 Composition        ║"
    echo "║  7) param      动态参数                  ║"
    echo "║  8) launch     Launch 文件编排           ║"
    echo "║  9) build      编译全部包                ║"
    echo "║  q) 退出                                ║"
    echo "╚══════════════════════════════════════════╝"
    echo ""
    read -r -p "输入 [1-9/q]: " c
    case "$c" in
        1) echo "--- 基础 Topic ---"; ros2 run topic_lab basic_pub & sleep 1; ros2 run topic_lab basic_sub; pkill basic_pub ;;
        2) echo "--- Service ---"; ros2 run service_lab battery_server & sleep 1; timeout 10 ros2 run service_lab battery_client; pkill battery_server ;;
        3) echo "--- Action ---"; ros2 run action_lab navigate_server & sleep 1; timeout 8 ros2 run action_lab navigate_client; pkill navigate_server ;;
        4) echo "--- QoS ---"; ros2 run qos_lab qos_subscriber --ros-args -p qos_mode:=reliable & sleep 1; timeout 8 ros2 run qos_lab qos_publisher --ros-args -p qos_mode:=reliable; pkill qos_subscriber ;;
        5) echo "--- Lifecycle ---"; ros2 run lifecycle_lab lifecycle_demo ;;
        6) echo "--- Composition ---"; timeout 8 ros2 run composition_lab composition_demo ;;
        7) echo "--- Parameter ---"; timeout 10 ros2 run param_lab param_demo ;;
        8) echo "--- Launch ---"; ros2 launch launch_lab demo_launch.py ;;
        9) echo "--- Build All ---"; cd "$WS"; colcon build --packages-select topic_lab service_lab action_lab qos_lab lifecycle_lab composition_lab param_lab launch_lab ;;
        q) exit 0 ;;
    esac
}

while true; do show_menu; done
