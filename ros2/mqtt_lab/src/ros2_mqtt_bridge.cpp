/**
 * ros2_mqtt_bridge.cpp — ROS2 ↔ MQTT 双向桥接节点
 *
 * 演示要点:
 *   1. ROS2→MQTT: 订阅 ROS2 Topic, 转发到 MQTT broker
 *   2. MQTT→ROS2: 订阅 MQTT Topic, 发布为 ROS2 Topic
 *   3. mosquitto C API: mosquitto_connect/publish/subscribe/loop
 *   4. MQTT QoS 0/1/2 与 DDS QoS 对比
 *   5. 双向桥接架构: 同时运行两条转发链路
 *
 * 用法:
 *   # 1. 安装 mosquitto (首次)
 *   bash mqtt_lab/scripts/setup_mosquitto.sh
 *
 *   # 2. 启动桥接
 *   ros2 run mqtt_lab ros2_mqtt_bridge
 *
 *   # 3. Terminal 2: 发布 ROS2 消息 (会被转发到 MQTT)
 *   ros2 topic pub /ros2_out std_msgs/msg/String "{data: hello_from_ros2}"
 *
 *   # 4. Terminal 3: 监听 MQTT (查看从 ROS2 转发的消息)
 *   mosquitto_sub -t "ros2/data"
 *
 *   # 5. Terminal 4: 发布 MQTT 消息 (会被转发到 ROS2)
 *   mosquitto_pub -t "mqtt/cmd" -m "hello_from_mqtt"
 *
 *   # 6. 验证 MQTT→ROS2:
 *   ros2 topic echo /mqtt_cmd
 *
 * 架构:
 *   ROS2 Topic "/ros2_out"  ──→  ros2_mqtt_bridge  ──→  MQTT Topic "ros2/data"
 *   MQTT Topic "mqtt/cmd"   ──→  ros2_mqtt_bridge  ──→  ROS2 Topic "/mqtt_cmd"
 *
 * DDS 对应:
 *   DDS 和 MQTT 都是 Publish/Subscribe 模型, 但:
 *   - DDS 用于局域网实时通信 (RTPS wire protocol, UDP 组播)
 *   - MQTT 用于云/广域网通信 (TCP, broker 中转)
 *   桥接层实现了两者的协议转换
 *
 * 工程意义:
 *   EHR 机器人部署场景:
 *   - 机器人内部: DDS/ROS2 (低延迟, 高可靠)
 *   - 机器人→云端: MQTT (穿透防火墙, 跨网络)
 *   - 云端→机器人: MQTT 远程指令
 */

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <cstring>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

#include <mosquitto.h>

using namespace std::chrono_literals;

class Ros2MqttBridge : public rclcpp::Node
{
public:
  Ros2MqttBridge()
  : Node("ros2_mqtt_bridge_node")
  {
    // ━━━ ROS2 → MQTT 方向 ━━━
    // 订阅 ROS2 topic, 回调中转发到 MQTT
    ros2_sub_ = this->create_subscription<std_msgs::msg::String>(
      "/ros2_out", 10,
      std::bind(&Ros2MqttBridge::on_ros2_message, this, std::placeholders::_1));

    // ━━━ MQTT → ROS2 方向 ━━━
    // 创建 ROS2 publisher, MQTT 回调中发布到此 topic
    ros2_pub_ = this->create_publisher<std_msgs::msg::String>(
      "/mqtt_cmd", 10);

    // ━━━ 初始化 mosquitto ━━━
    mosquitto_lib_init();

    // ★ 创建 MQTT 客户端 (ID: "ros2_bridge")
    mosq_ = mosquitto_new("ros2_bridge", true, this);
    if (!mosq_) {
      RCLCPP_ERROR(this->get_logger(), "mosquitto_new 失败");
      return;
    }

    // ★ 设置 MQTT 消息回调 (MQTT → ROS2 方向)
    mosquitto_message_callback_set(mosq_, &Ros2MqttBridge::on_mqtt_message_cb);

    // ★ 连接本地 mosquitto broker (默认端口 1883)
    int rc = mosquitto_connect(mosq_, "localhost", 1883, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
      RCLCPP_ERROR(this->get_logger(),
        "无法连接 MQTT Broker (localhost:1883) — 请确认 mosquitto 已启动");
      RCLCPP_ERROR(this->get_logger(),
        "  sudo systemctl start mosquitto");
    } else {
      RCLCPP_INFO(this->get_logger(),
        "✅ 已连接 MQTT Broker (localhost:1883)");
    }

    // ★ 订阅 MQTT topic (MQTT→ROS2 方向的数据源)
    rc = mosquitto_subscribe(mosq_, NULL, "mqtt/cmd", 0);
    if (rc == MOSQ_ERR_SUCCESS) {
      RCLCPP_INFO(this->get_logger(),
        "✅ 已订阅 MQTT: mqtt/cmd → ROS2: /mqtt_cmd");
    }

    // ★ 100ms timer: 驱动 MQTT 网络 I/O (必须周期性调用)
    mqtt_loop_timer_ = this->create_wall_timer(
      100ms, std::bind(&Ros2MqttBridge::mqtt_loop, this));

    RCLCPP_INFO(this->get_logger(),
      "╔══════════════════════════════════════════╗");
    RCLCPP_INFO(this->get_logger(),
      "║  ROS2 ↔ MQTT Bridge 已启动              ║");
    RCLCPP_INFO(this->get_logger(),
      "╠══════════════════════════════════════════╣");
    RCLCPP_INFO(this->get_logger(),
      "║  ROS2 → MQTT: /ros2_out → ros2/data     ║");
    RCLCPP_INFO(this->get_logger(),
      "║  MQTT → ROS2: mqtt/cmd → /mqtt_cmd      ║");
    RCLCPP_INFO(this->get_logger(),
      "╚══════════════════════════════════════════╝");
    RCLCPP_INFO(this->get_logger(),
      "测试: ros2 topic pub /ros2_out std_msgs/msg/String '{data: hi}'");
    RCLCPP_INFO(this->get_logger(),
      "      mosquitto_sub -t 'ros2/data'");
    RCLCPP_INFO(this->get_logger(),
      "      mosquitto_pub -t 'mqtt/cmd' -m 'cmd_from_cloud'");
    RCLCPP_INFO(this->get_logger(),
      "      ros2 topic echo /mqtt_cmd");
  }

  ~Ros2MqttBridge()
  {
    if (mosq_) {
      mosquitto_disconnect(mosq_);
      mosquitto_destroy(mosq_);
    }
    mosquitto_lib_cleanup();
  }

private:
  // ━━━ ROS2 → MQTT: ROS2 订阅回调 ━━━
  void on_ros2_message(std::shared_ptr<const std_msgs::msg::String> msg)
  {
    ros2_count_++;

    // ★ 转发到 MQTT: mosquitto_publish
    int rc = mosquitto_publish(mosq_, NULL, "ros2/data",
      msg->data.size(), msg->data.c_str(), 0, false);

    if (rc == MOSQ_ERR_SUCCESS) {
      RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
        "ROS2→MQTT [%d]: %s", ros2_count_, msg->data.c_str());
    } else {
      RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
        "MQTT 发送失败: %s", mosquitto_strerror(rc));
    }
  }

  // ━━━ MQTT → ROS2: MQTT 消息回调 (静态函数) ━━━
  static void on_mqtt_message_cb(struct mosquitto * mosq,
    void * userdata, const struct mosquitto_message * message)
  {
    auto * bridge = static_cast<Ros2MqttBridge *>(userdata);
    bridge->handle_mqtt_message(message);
  }

  void handle_mqtt_message(const struct mosquitto_message * message)
  {
    mqtt_count_++;

    // ★ 将 MQTT 消息发布到 ROS2 topic
    auto ros_msg = std::make_unique<std_msgs::msg::String>();
    ros_msg->data = std::string(
      static_cast<const char *>(message->payload),
      message->payloadlen);

    RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
      "MQTT→ROS2 [%d]: %s", mqtt_count_, ros_msg->data.c_str());

    ros2_pub_->publish(std::move(ros_msg));
  }

  // ━━━ 驱动 MQTT 网络 I/O ━━━
  void mqtt_loop()
  {
    // ★ 必须周期性调用 mosquitto_loop — 处理入站/出站消息
    int rc = mosquitto_loop(mosq_, 0, 1);
    if (rc != MOSQ_ERR_SUCCESS && rc != MOSQ_ERR_NO_CONN) {
      RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 10000,
        "MQTT loop 异常: %s", mosquitto_strerror(rc));
    }
  }

  // ROS2
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr ros2_sub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr ros2_pub_;
  rclcpp::TimerBase::SharedPtr mqtt_loop_timer_;

  // MQTT
  struct mosquitto * mosq_ = nullptr;
  int ros2_count_ = 0;
  int mqtt_count_ = 0;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<Ros2MqttBridge>());
  rclcpp::shutdown();
  return 0;
}
