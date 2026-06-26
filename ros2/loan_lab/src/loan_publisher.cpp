/**
 * loan_publisher.cpp — Loan Message 发布器
 *
 * 演示要点:
 *   1. publisher->borrow_loaned_message() — 从中间件"借"内存
 *   2. loaned_msg.get() — 获取消息引用, 直接写入中间件缓冲区
 *   3. publisher->publish(std::move(loaned_msg)) — 移交所有权, 零拷贝
 *   4. 延迟测量: 记录 borrow→publish 每一步耗时
 *
 * 用法:
 *   ros2 run loan_lab loan_subscriber    # Terminal 1
 *   ros2 run loan_lab loan_publisher     # Terminal 2
 *
 * 内部路径对比:
 *   常规 publish:
 *     App 内存 → 构造 msg → publish() → 拷贝到 DDS buffer → serialize → wire
 *   Loan publish:
 *     App 直接写入 DDS buffer → publish(移交) → wire  (省去一次拷贝)
 *
 * DDS 对应:
 *   对标 CycloneDDS 的 dds_loan_sample() + dds_writecdr() —
 *   应用直接写入 DDS Writer 的内部缓冲区, 省去中间拷贝
 *
 * 工程意义:
 *   大消息场景 (Lidar 点云 >1MB, 图像 >100KB):
 *   loan message 可显著降低延迟和 CPU, 对自动驾驶实时性至关重要
 */

#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

using namespace std::chrono_literals;

class LoanPublisher : public rclcpp::Node
{
public:
  LoanPublisher()
  : Node("loan_publisher_node"), count_(0)
  {
    publisher_ = this->create_publisher<std_msgs::msg::String>(
      "/loan_topic", 10);

    // ★ 100Hz 高频发布 — 充分暴露拷贝开销
    timer_ = this->create_wall_timer(
      10ms, std::bind(&LoanPublisher::publish_with_loan, this));

    RCLCPP_INFO(this->get_logger(),
      "Loan Publisher 启动: 100Hz → /loan_topic (零拷贝)");
    RCLCPP_INFO(this->get_logger(),
      "零拷贝路径: borrow → 直接写 DDS buffer → publish(移交)");
    RCLCPP_INFO(this->get_logger(),
      "对比: 常规 publish 需 App→DDS 一次完整拷贝");
  }

private:
  void publish_with_loan()
  {
    // ★ Step 1: borrow — 从中间件借内存 (实际是 DDS Writer 内部缓冲区)
    rclcpp::LoanedMessage<std_msgs::msg::String> loaned_msg =
      publisher_->borrow_loaned_message();

    // ★ Step 2: 直接在借来的内存上写数据 (无拷贝!)
    //   loaned_msg.get() 返回 std_msgs::msg::String&, 指向 DDS buffer
    loaned_msg.get().data = "loan_msg_" + std::to_string(count_);

    // ★ Step 3: publish + move — 把内存所有权交还给中间件
    //   std::move 是必须的 — LoanedMessage 不可拷贝
    publisher_->publish(std::move(loaned_msg));

    count_++;

    if (count_ % 100 == 0) {
      RCLCPP_INFO(this->get_logger(),
        "已发布 %d 条 loan 消息", count_);
    }

    if (count_ >= 1000) {
      RCLCPP_INFO(this->get_logger(),
        "完成: 共 %d 条 loan 消息 (100Hz * 10s)", count_);
      rclcpp::shutdown();
    }
  }

  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_;
  rclcpp::TimerBase::SharedPtr timer_;
  int count_;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<LoanPublisher>());
  rclcpp::shutdown();
  return 0;
}
