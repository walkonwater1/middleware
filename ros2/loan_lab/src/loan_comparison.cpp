/**
 * loan_comparison.cpp — Regular vs Loan Message A/B 性能对比
 *
 * 演示要点:
 *   1. A组 (Regular): 常规 publish — 构造 msg → publish (有拷贝)
 *   2. B组 (Loan):   borrow_loaned_message → publish(move) (零拷贝)
 *   3. 两组同时运行, 对比: 吞吐量、单消息延迟
 *   4. 结论: 大消息 loan 优势明显, 小消息 (<100B) 常规即可
 *
 * 用法:
 *   ros2 run loan_lab loan_comparison
 *
 * 注意:
 *   此对比在同一进程运行两组 publisher + subscriber,
 *   消除了环境差异, 纯粹对比 API 路径
 *
 * DDS 对应:
 *   对标 DDS bench_lab.c 的 QoS 对比思路 — 控制变量法 A/B 实验
 */

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>
#include <numeric>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

using namespace std::chrono_literals;

class LoanComparison : public rclcpp::Node
{
public:
  LoanComparison()
  : Node("loan_comparison_node"),
    regular_count_(0), loan_count_(0),
    regular_latency_total_(0), loan_latency_total_(0),
    start_time_(this->now())
  {
    // ★ A组: 常规 Publisher + Subscriber
    pub_regular_ = this->create_publisher<std_msgs::msg::String>(
      "/comparison/regular", 10);
    sub_regular_ = this->create_subscription<std_msgs::msg::String>(
      "/comparison/regular", 10,
      std::bind(&LoanComparison::on_regular, this, std::placeholders::_1));

    // ★ B组: Loan Publisher + Subscriber
    pub_loan_ = this->create_publisher<std_msgs::msg::String>(
      "/comparison/loan", 10);
    sub_loan_ = this->create_subscription<std_msgs::msg::String>(
      "/comparison/loan", 10,
      std::bind(&LoanComparison::on_loan, this, std::placeholders::_1));

    // ★ 10ms timer — 两组交替发布 (各 100Hz)
    timer_ = this->create_wall_timer(
      5ms, std::bind(&LoanComparison::publish_both, this));

    // 每秒打印对比统计
    report_timer_ = this->create_wall_timer(
      1s, std::bind(&LoanComparison::report_stats, this));

    RCLCPP_INFO(this->get_logger(),
      "╔══════════════════════════════════════╗");
    RCLCPP_INFO(this->get_logger(),
      "║  Loan Message A/B 性能对比           ║");
    RCLCPP_INFO(this->get_logger(),
      "╠══════════════════════════════════════╣");
    RCLCPP_INFO(this->get_logger(),
      "║  A: /comparison/regular  (有拷贝)    ║");
    RCLCPP_INFO(this->get_logger(),
      "║  B: /comparison/loan     (零拷贝)    ║");
    RCLCPP_INFO(this->get_logger(),
      "╚══════════════════════════════════════╝");
    RCLCPP_INFO(this->get_logger(),
      "运行 10s 后打印统计结果...");
  }

private:
  void publish_both()
  {
    auto now = this->now();

    // ★ A组: 常规发布 — 构造 unique_ptr → publish
    {
      auto t1 = this->now();
      auto msg = std::make_unique<std_msgs::msg::String>();
      msg->data = "regular_" + std::to_string(regular_count_);
      pub_regular_->publish(std::move(msg));
      auto t2 = this->now();
      regular_latency_total_ += (t2 - t1).nanoseconds();
      regular_count_++;
    }

    // ★ B组: Loan 发布 — borrow → 直接写 → publish(move)
    {
      auto t1 = this->now();
      auto loaned = pub_loan_->borrow_loaned_message();
      loaned.get().data = "loan_" + std::to_string(loan_count_);
      pub_loan_->publish(std::move(loaned));
      auto t2 = this->now();
      loan_latency_total_ += (t2 - t1).nanoseconds();
      loan_count_++;
    }

    // 10 秒后停止
    if (regular_count_ >= 2000) {
      print_final_report();
      rclcpp::shutdown();
    }
  }

  void on_regular(std::shared_ptr<const std_msgs::msg::String> msg)
  {
    (void)msg;  // 仅计数, 不做额外处理
  }

  void on_loan(std::shared_ptr<const std_msgs::msg::String> msg)
  {
    (void)msg;
  }

  void report_stats()
  {
    auto elapsed = (this->now() - start_time_).seconds();
    RCLCPP_INFO(this->get_logger(),
      "[%.1fs] Regular: %d | Loan: %d",
      elapsed, regular_count_, loan_count_);
  }

  void print_final_report()
  {
    auto elapsed = (this->now() - start_time_).seconds();

    double regular_avg_ns = regular_count_ > 0 ?
      static_cast<double>(regular_latency_total_) / regular_count_ : 0;
    double loan_avg_ns = loan_count_ > 0 ?
      static_cast<double>(loan_latency_total_) / loan_count_ : 0;

    RCLCPP_INFO(this->get_logger(),
      "╔══════════════════════════════════════════╗");
    RCLCPP_INFO(this->get_logger(),
      "║         A/B 对比结果 (%.1fs)            ║", elapsed);
    RCLCPP_INFO(this->get_logger(),
      "╠══════════════════════════════════════════╣");
    RCLCPP_INFO(this->get_logger(),
      "║  指标           Regular        Loan     ║");
    RCLCPP_INFO(this->get_logger(),
      "║  ─────────      ────────      ────────  ║");
    RCLCPP_INFO(this->get_logger(),
      "║  总消息数:      %-12d  %-12d║",
      regular_count_, loan_count_);
    RCLCPP_INFO(this->get_logger(),
      "║  吞吐 (msg/s):  %-12.1f  %-12.1f║",
      regular_count_ / elapsed, loan_count_ / elapsed);
    RCLCPP_INFO(this->get_logger(),
      "║  均延迟 (ns):   %-12.0f  %-12.0f║",
      regular_avg_ns, loan_avg_ns);
    RCLCPP_INFO(this->get_logger(),
      "╠══════════════════════════════════════════╣");

    if (loan_avg_ns > 0 && regular_avg_ns > 0) {
      double speedup = regular_avg_ns / loan_avg_ns;
      RCLCPP_INFO(this->get_logger(),
        "║  Loan 延迟降低: %.1fx                     ║", speedup);
    }
    RCLCPP_INFO(this->get_logger(),
      "╚══════════════════════════════════════════╝");

    RCLCPP_INFO(this->get_logger(),
      "结论: 小消息 (<100B) 两者差距有限, ");
    RCLCPP_INFO(this->get_logger(),
      "      大消息 (>1KB, Lidar点云/图像) Loan 优势显著");
    RCLCPP_INFO(this->get_logger(),
      "      对标 CycloneDDS dds_loan_sample() + dds_writecdr()");
  }

  // A组: Regular
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr pub_regular_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr sub_regular_;
  int regular_count_;
  int64_t regular_latency_total_;

  // B组: Loan
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr pub_loan_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr sub_loan_;
  int loan_count_;
  int64_t loan_latency_total_;

  rclcpp::TimerBase::SharedPtr timer_;
  rclcpp::TimerBase::SharedPtr report_timer_;
  rclcpp::Time start_time_;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<LoanComparison>());
  rclcpp::shutdown();
  return 0;
}
