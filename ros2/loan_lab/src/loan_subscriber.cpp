/**
 * loan_subscriber.cpp — Loan Message 订阅器
 *
 * 演示要点:
 *   1. 标准订阅器 — loan 消息在接收端无需特殊处理
 *   2. 吞吐量统计: 实时计算 msg/s 速率
 *   3. 接收端零拷贝: take_loaned_message (ROS2 Rolling+)
 *
 * 用法:
 *   ros2 run loan_lab loan_subscriber    # Terminal 1 (先启动)
 *   ros2 run loan_lab loan_publisher     # Terminal 2 (后启动)
 *
 * 关于 take_loaned_message:
 *   ROS2 Humble 中订阅端默认使用普通 take (隐式反序列化),
 *   但从 Rolling 开始支持 subscriber->take_loaned_message()
 *   在订阅端也实现零拷贝读取
 *
 * 工程意义:
 *   高吞吐数据流 (如图像/点云) 在订阅端也需要避免拷贝,
 *   配套使用 borrow_loaned_message (pub) + take_loaned_message (sub)
 *   实现端到端零拷贝
 */

#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

using namespace std::chrono_literals;

class LoanSubscriber : public rclcpp::Node
{
public:
  LoanSubscriber()
  : Node("loan_subscriber_node"), msg_count_(0)
  {
    // ★ 标准订阅 (loan 消息对订阅端透明)
    subscription_ = this->create_subscription<std_msgs::msg::String>(
      "/loan_topic", 10,
      std::bind(&LoanSubscriber::on_message, this, std::placeholders::_1));

    // 每秒报告吞吐量
    rate_timer_ = this->create_wall_timer(
      1s, std::bind(&LoanSubscriber::report_rate, this));

    start_time_ = this->now();

    RCLCPP_INFO(this->get_logger(),
      "Loan Subscriber 启动: 监听 /loan_topic");
    RCLCPP_INFO(this->get_logger(),
      "每秒报告吞吐量 (msg/s)");
  }

private:
  void on_message(std::shared_ptr<const std_msgs::msg::String> msg)
  {
    // ★ 接收端: 标准回调, 无特殊处理
    //   底层反序列化后通过 shared_ptr 传递
    msg_count_++;
    last_data_ = msg->data;
  }

  void report_rate()
  {
    auto elapsed = (this->now() - start_time_).seconds();
    if (elapsed > 0) {
      double rate = msg_count_ / elapsed;
      RCLCPP_INFO(this->get_logger(),
        "吞吐: %.1f msg/s | 总计: %d | 最新: %s",
        rate, msg_count_, last_data_.c_str());
    }
  }

  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr subscription_;
  rclcpp::TimerBase::SharedPtr rate_timer_;
  rclcpp::Time start_time_;
  int msg_count_;
  std::string last_data_;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<LoanSubscriber>());
  rclcpp::shutdown();
  return 0;
}
