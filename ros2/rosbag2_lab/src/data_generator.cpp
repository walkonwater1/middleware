/**
 * data_generator.cpp — 测试数据生成器
 *
 * 演示要点:
 *   1. 生成正弦波 Float64 数据 — 模拟传感器读数
 *   2. 50Hz 高频发布 (模拟真实传感器采样率)
 *   3. 为 bag_recorder 提供录制素材
 *   4. 自动运行 10 秒后退出 (500 条消息)
 *
 * 用法:
 *   ros2 run rosbag2_lab data_generator
 *   ros2 topic echo /sensor/data       # 查看正弦波数据
 *   ros2 topic hz /sensor/data         # 验证 50Hz 频率
 *
 * 工程意义:
 *   真实传感器 (Lidar/Camera/IMU) 以固定频率产生数据流,
 *   rosbag2 录制这些数据用于离线调试和回归测试
 */

#include <chrono>
#include <cmath>
#include <functional>
#include <memory>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/float64.hpp"

using namespace std::chrono_literals;

class DataGenerator : public rclcpp::Node
{
public:
  DataGenerator()
  : Node("data_generator_node"), t_(0.0), count_(0)
  {
    publisher_ = this->create_publisher<std_msgs::msg::Float64>(
      "/sensor/data", 10);

    // ★ 20ms = 50Hz — 典型传感器采样率
    timer_ = this->create_wall_timer(
      20ms, std::bind(&DataGenerator::publish_sine, this));

    RCLCPP_INFO(this->get_logger(),
      "数据生成器启动: 50Hz 正弦波 → /sensor/data");
    RCLCPP_INFO(this->get_logger(),
      "振幅=5.0, 频率=0.5Hz, 运行 10s 后自动退出");
  }

private:
  void publish_sine()
  {
    // ★ 正弦波: amplitude=5.0, freq=0.5Hz
    auto msg = std::make_unique<std_msgs::msg::Float64>();
    msg->data = 5.0 * std::sin(2.0 * M_PI * 0.5 * t_);

    publisher_->publish(std::move(msg));

    t_ += 0.02;  // 20ms step
    count_++;

    if (count_ % 50 == 0) {
      RCLCPP_INFO(this->get_logger(),
        "已发布 %d 条 (%.1fs)", count_, t_);
    }

    // 10 秒后自动退出
    if (count_ >= 500) {
      RCLCPP_INFO(this->get_logger(),
        "数据生成完成: %d 条消息, %.1f 秒", count_, t_);
      rclcpp::shutdown();
    }
  }

  rclcpp::Publisher<std_msgs::msg::Float64>::SharedPtr publisher_;
  rclcpp::TimerBase::SharedPtr timer_;
  double t_;
  int count_;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<DataGenerator>());
  rclcpp::shutdown();
  return 0;
}
