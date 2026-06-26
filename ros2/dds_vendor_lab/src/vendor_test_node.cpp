/**
 * vendor_test_node.cpp — DDS 供应商切换实验节点
 *
 * 演示要点:
 *   1. rmw_get_implementation_identifier() — 读取当前 RMW 实现
 *   2. RMW_IMPLEMENTATION 环境变量切换 DDS 供应商
 *   3. 同一节点代码, 不同的 DDS 底层 — 代码零变更
 *   4. 参数选择 pub/sub 模式
 *   5. ros2 doctor 诊断信息
 *
 * 用法:
 *   # Terminal 1: Subscriber (默认 FastDDS)
 *   ros2 run dds_vendor_lab vendor_test_node --ros-args -p mode:=sub
 *
 *   # Terminal 2: Publisher (切换到 CycloneDDS — 需先安装)
 *   # sudo apt install ros-humble-rmw-cyclonedds-cpp
 *   RMW_IMPLEMENTATION=rmw_cyclonedds_cpp ros2 run dds_vendor_lab vendor_test_node --ros-args -p mode:=pub
 *
 *   # 两者可以互操作! — DDS RTPS wire protocol 保证兼容性
 *
 * RMW 层架构:
 *   rclcpp (ROS2 客户端库)
 *      ↓
 *   rmw (ROS Middleware 抽象层)
 *      ↓
 *   ┌──────────┬──────────────┬──────────────┐
 *   │ FastDDS  │ CycloneDDS   │ Connext(RTI) │  ← 不同的 DDS 实现
 *   └──────────┴──────────────┴──────────────┘
 *
 * DDS 对应:
 *   RMW 层对应 DDS 的标准化 — 类似的 DDS 供应商切换通过
 *   链接不同的 dds.dll (商用) 或 libddsc.so (Cyclone) 来实现
 */

#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"
#include "rmw/rmw.h"

using namespace std::chrono_literals;

class VendorTestNode : public rclcpp::Node
{
public:
  VendorTestNode()
  : Node("vendor_test_node"), count_(0)
  {
    // ★ Step 1: 读取 RMW 实现信息
    const char * rmw_id = rmw_get_implementation_identifier();
    const char * env_rmw = std::getenv("RMW_IMPLEMENTATION");

    // ★ Step 2: 参数选择模式
    this->declare_parameter("mode", "pub");
    std::string mode = this->get_parameter("mode").as_string();

    RCLCPP_INFO(this->get_logger(),
      "╔══════════════════════════════════════╗");
    RCLCPP_INFO(this->get_logger(),
      "║  DDS Vendor Test Node               ║");
    RCLCPP_INFO(this->get_logger(),
      "╠══════════════════════════════════════╣");
    RCLCPP_INFO(this->get_logger(),
      "║  模式:         %s                    ║",
      mode.c_str());
    RCLCPP_INFO(this->get_logger(),
      "║  RMW 实现:     %s (%s)          ║",
      rmw_id, env_rmw ? env_rmw : "默认");

    // 诊断: 列出当前活跃的 DDS 供应商
    RCLCPP_INFO(this->get_logger(),
      "║  Domain ID:    %ld (环境: %s)    ║",
      this->get_parameter("domain_id").as_int(),
      std::getenv("ROS_DOMAIN_ID") ? std::getenv("ROS_DOMAIN_ID") : "0");

    RCLCPP_INFO(this->get_logger(),
      "╚══════════════════════════════════════╝");

    if (mode == "pub") {
      publisher_ = this->create_publisher<std_msgs::msg::String>(
        "/vendor_test", 10);
      timer_ = this->create_wall_timer(
        200ms, std::bind(&VendorTestNode::publish_identity, this));
      RCLCPP_INFO(this->get_logger(),
        "Publisher 模式: 200ms → /vendor_test");
      RCLCPP_INFO(this->get_logger(),
        "消息包含 RMW 供应商信息, 供 subscriber 验证兼容性");
    } else {
      subscription_ = this->create_subscription<std_msgs::msg::String>(
        "/vendor_test", 10,
        std::bind(&VendorTestNode::on_message, this, std::placeholders::_1));
      RCLCPP_INFO(this->get_logger(),
        "Subscriber 模式: 监听 /vendor_test");
      RCLCPP_INFO(this->get_logger(),
        "等待 Publisher (可能是 FastDDS 或 CycloneDDS)...");
    }
  }

private:
  void publish_identity()
  {
    const char * rmw_id = rmw_get_implementation_identifier();

    auto msg = std::make_unique<std_msgs::msg::String>();
    msg->data = std::string("{")
      + "\"rmw\":\"" + rmw_id + "\","
      + "\"seq\":" + std::to_string(count_) + ","
      + "\"timestamp\":" + std::to_string(this->now().nanoseconds())
      + "}";

    publisher_->publish(std::move(msg));
    count_++;

    RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
      "发布 #%d via %s", count_, rmw_id);
  }

  void on_message(std::shared_ptr<const std_msgs::msg::String> msg)
  {
    const char * my_rmw = rmw_get_implementation_identifier();

    if (msg_count_ == 0) {
      RCLCPP_INFO(this->get_logger(),
        "✅ 收到第一条消息! 对端 RMW 信息: %s", msg->data.c_str());
      RCLCPP_INFO(this->get_logger(),
        "本机 RMW: %s ← 不同供应商间 RTPS 线兼容!", my_rmw);

      // 提取对端 RMW
      if (msg->data.find(my_rmw) != std::string::npos) {
        RCLCPP_INFO(this->get_logger(),
          "对端使用相同 RMW 供应商 (%s)", my_rmw);
      } else {
        RCLCPP_INFO(this->get_logger(),
          "✅ 跨供应商通信成功! 对端 RMW ≠ 本机 RMW (%s)", my_rmw);
      }
    }

    msg_count_++;
    RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
      "收到 #%d: %s", msg_count_, msg->data.c_str());
  }

  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr subscription_;
  rclcpp::TimerBase::SharedPtr timer_;
  int count_;
  int msg_count_ = 0;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<VendorTestNode>());
  rclcpp::shutdown();
  return 0;
}
