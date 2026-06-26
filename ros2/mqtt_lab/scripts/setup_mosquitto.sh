#!/bin/bash
# setup_mosquitto.sh — 安装并配置 mosquitto MQTT Broker
set -e

echo "━━━ 安装 mosquitto MQTT Broker ━━━"

# 检查是否已安装
if dpkg -l | grep -q "mosquitto " && dpkg -l | grep -q "libmosquitto-dev"; then
    echo "mosquitto 已安装, 跳过安装步骤"
else
    echo "安装 mosquitto + libmosquitto-dev..."
    sudo apt update
    sudo apt install -y mosquitto mosquitto-clients libmosquitto-dev
    echo "✅ 安装完成"
fi

echo ""

# 启动 broker
echo "━━━ 启动 mosquitto broker ━━━"
if systemctl is-active --quiet mosquitto 2>/dev/null; then
    echo "mosquitto 已在运行"
else
    sudo systemctl start mosquitto
    echo "✅ mosquitto 已启动"
fi

# 检查状态
echo ""
echo "━━━ Broker 状态 ━━━"
sudo systemctl status mosquitto --no-pager 2>/dev/null | head -5 || echo "  用 'sudo systemctl status mosquitto' 查看状态"
echo ""

# 快速测试
echo "━━━ 功能测试 ━━━"
echo "测试 pub/sub (3 秒)..."
timeout 3 mosquitto_sub -t "test/setup" -C 1 &
sleep 1
mosquitto_pub -t "test/setup" -m "mosquitto_ok" 2>/dev/null
wait 2>/dev/null
echo "✅ MQTT Broker 工作正常"
echo ""

echo "━━━ 使用方式 ━━━"
echo "  启动桥接: ros2 run mqtt_lab ros2_mqtt_bridge"
echo "  MQTT 监听: mosquitto_sub -t 'ros2/data'"
echo "  MQTT 发布: mosquitto_pub -t 'mqtt/cmd' -m 'hello'"
echo "  ROS2 发布: ros2 topic pub /ros2_out std_msgs/msg/String '{data: hi}'"
echo "  ROS2 监听: ros2 topic echo /mqtt_cmd"
echo ""
