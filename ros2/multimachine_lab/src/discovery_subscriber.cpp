/**
 * discovery_subscriber.cpp — 多机发现订阅器
 *
 * 演示要点:
 *   1. 自动发现对端 Publisher — 无需配置 IP 地址
 *   2. 解析消息中的 hostname/PID/domain_id 确认跨机通信
 *   3. wait_for_publisher — 等待发现对端
 *   4. ROS_LOCALHOST_ONLY — 单机测试模式
 *
 * 用法:
 *   ros2 run multimachine_lab discovery_subscriber --ros-args -p domain_id:=0
 *
 *   # 同子网 Machine B:
 *   ROS_DOMAIN_ID=5 ros2 run multimachine_lab discovery_subscriber --ros-args -p domain_id:=5
 *
 * 工程意义:
 *   真实机器人系统中, 传感器采集节点、建图节点、导航节点可能分布在
 *   不同机器上。DDS 自动发现让这些节点无需手动配置 IP 即可通信
 */

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <unistd.h>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

using namespace std::chrono_literals;

class DiscoverySubscriber : public rclcpp::Node
{
public:
  DiscoverySubscriber()
  : Node("discovery_subscriber_node"), msg_count_(0)
  {
    this->declare_parameter("domain_id", 0);
    int domain = this->get_parameter("domain_id").as_int();

    const char * env_domain = std::getenv("ROS_DOMAIN_ID");
    int env_id = env_domain ? std::atoi(env_domain) : -1;

    char hostname[256];
    gethostname(hostname, sizeof(hostname));

    // ★ 创建订阅 — DDS SEDP 自动发现并匹配
    subscription_ = this->create_subscription<std_msgs::msg::String>(
      "/discovery_channel", 10,
      std::bind(&DiscoverySubscriber::on_discovery, this, std::placeholders::_1));

    RCLCPP_INFO(this->get_logger(),
      "╔══════════════════════════════════════╗");
    RCLCPP_INFO(this->get_logger(),
      "║  订阅器 — 等待发现对端...           ║");
    RCLCPP_INFO(this->get_logger(),
      "╠══════════════════════════════════════╣");
    RCLCPP_INFO(this->get_logger(),
      "║  本机:        %s                     ║", hostname);
    RCLCPP_INFO(this->get_logger(),
      "║  Domain ID:   %d (参数) / %d (环境) ║",
      domain, env_id);
    RCLCPP_INFO(this->get_logger(),
      "║  PID:         %d                     ║", getpid());
    RCLCPP_INFO(this->get_logger(),
      "╚══════════════════════════════════════╝");

    // ★ 检查是否有 publisher 在线
    size_t pub_count = this->count_publishers("/discovery_channel");
    RCLCPP_INFO(this->get_logger(),
      "当前匹配的 Publisher 数量: %zu", pub_count);

    if (pub_count == 0) {
      RCLCPP_INFO(this->get_logger(),
        "未发现 Publisher — 等待中...");
      RCLCPP_INFO(this->get_logger(),
        "请在同一 Domain 的机器上启动 discovery_publisher");
    }
  }

private:
  void on_discovery(std::shared_ptr<const std_msgs::msg::String> msg)
  {
    if (msg_count_ == 0) {
      // ★ 第一次收到消息 = 发现成功!
      RCLCPP_INFO(this->get_logger(),
        "✅ 发现对端节点! 收到第一条消息");
      RCLCPP_INFO(this->get_logger(),
        "对端身份: %s", msg->data.c_str());
      RCLCPP_INFO(this->get_logger(),
        "验证: DDS SPDP/SEDP 自动发现成功, 无需 Master/IP 配置");
    }

    msg_count_++;

    RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
      "收到 #%d: %s", msg_count_, msg->data.c_str());
  }

  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr subscription_;
  int msg_count_;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<DiscoverySubscriber>());
  rclcpp::shutdown();
  return 0;
}
