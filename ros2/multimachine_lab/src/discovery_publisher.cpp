/**
 * discovery_publisher.cpp — 多机发现发布器
 *
 * 演示要点:
 *   1. ROS_DOMAIN_ID 参数/环境变量控制网络隔离
 *   2. DDS SPDP/SEDP 自动发现 — 无需 Master, 无需配置 IP
 *   3. 发布自身身份信息 (hostname, PID, domain_id)
 *   4. 不同 Domain 的节点完全隔离, 互不干扰
 *
 * 用法:
 *   # 单机测试:
 *   ros2 run multimachine_lab discovery_publisher --ros-args -p domain_id:=0
 *
 *   # 多机测试 (Machine A):
 *   ROS_DOMAIN_ID=5 ros2 run multimachine_lab discovery_publisher --ros-args -p domain_id:=5
 *
 *   # 多机测试 (Machine B, 同一子网):
 *   ROS_DOMAIN_ID=5 ros2 run multimachine_lab discovery_subscriber --ros-args -p domain_id:=5
 *   # Machine B 自动发现 Machine A!
 *
 * DDS 对应:
 *   ROS_DOMAIN_ID → DDS Domain ID
 *   同 Domain 内 SPDP (Simple Participant Discovery Protocol) 通过组播
 *   自动发现所有 Participant, SEDP 发现 Topic/Reader/Writer
 *   这就是 ROS2 "无 Master" 的根本原因
 */

#include <chrono>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <unistd.h>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

using namespace std::chrono_literals;

class DiscoveryPublisher : public rclcpp::Node
{
public:
  DiscoveryPublisher()
  : Node("discovery_publisher_node"), count_(0)
  {
    // ★ 声明 domain_id 参数 (可与 ROS_DOMAIN_ID 环境变量叠加)
    this->declare_parameter("domain_id", 0);
    int domain = this->get_parameter("domain_id").as_int();

    // 读取环境变量交叉验证
    const char * env_domain = std::getenv("ROS_DOMAIN_ID");
    int env_id = env_domain ? std::atoi(env_domain) : -1;

    // 获取本机信息
    char hostname[256];
    gethostname(hostname, sizeof(hostname));

    publisher_ = this->create_publisher<std_msgs::msg::String>(
      "/discovery_channel", 10);

    // ★ 2Hz 发布身份信息
    timer_ = this->create_wall_timer(
      500ms, std::bind(&DiscoveryPublisher::publish_identity, this));

    RCLCPP_INFO(this->get_logger(),
      "╔══════════════════════════════════════╗");
    RCLCPP_INFO(this->get_logger(),
      "║  发布器 — 多机发现实验              ║");
    RCLCPP_INFO(this->get_logger(),
      "╠══════════════════════════════════════╣");
    RCLCPP_INFO(this->get_logger(),
      "║  Domain ID:   %d (参数) / %d (环境) ║",
      domain, env_id);
    RCLCPP_INFO(this->get_logger(),
      "║  Hostname:    %s                     ║", hostname);
    RCLCPP_INFO(this->get_logger(),
      "║  PID:         %d                     ║", getpid());
    RCLCPP_INFO(this->get_logger(),
      "║  Topic:       /discovery_channel     ║");
    RCLCPP_INFO(this->get_logger(),
      "╚══════════════════════════════════════╝");

    RCLCPP_INFO(this->get_logger(),
      "在同一子网的其他机器上运行 subscriber 即可自动发现本节点");
    RCLCPP_INFO(this->get_logger(),
      "关键: 无需配置 IP, 无需 Master — DDS SPDP 组播自动完成");
  }

private:
  void publish_identity()
  {
    char hostname[256];
    gethostname(hostname, sizeof(hostname));

    int domain = this->get_parameter("domain_id").as_int();

    std::ostringstream oss;
    oss << "{"
        << "\"hostname\":\"" << hostname << "\","
        << "\"pid\":" << getpid() << ","
        << "\"domain_id\":" << domain << ","
        << "\"seq\":" << count_ << ","
        << "\"timestamp\":" << this->now().nanoseconds()
        << "}";

    auto msg = std::make_unique<std_msgs::msg::String>();
    msg->data = oss.str();
    publisher_->publish(std::move(msg));

    count_++;
    RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
      "发布 #%d: %s (Domain %d)", count_, hostname, domain);
  }

  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_;
  rclcpp::TimerBase::SharedPtr timer_;
  int count_;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<DiscoveryPublisher>());
  rclcpp::shutdown();
  return 0;
}
