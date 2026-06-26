/**
 * bag_player.cpp — Rosbag2 编程式回放器
 *
 * 演示要点:
 *   1. rosbag2_cpp::Reader — C++ API 编程式回放 (非 CLI)
 *   2. has_next / read_next — 顺序遍历所有消息
 *   3. 反序列化 CDR → ROS2 消息
 *   4. 重新发布到 /sensor/replay topic
 *   5. 打印回放统计: 总消息数、时长、topic 信息
 *
 * 用法:
 *   ros2 run rosbag2_lab bag_player
 *   ros2 topic echo /sensor/replay    # 查看回放数据
 *
 * CLI 对比:
 *   ros2 bag play rosbag2_data/sensor_recording  ← 等效 CLI
 *
 * 工程意义:
 *   - 离线调试: 录制现场数据, 回实验室精确重现
 *   - 回归测试: 录制期望输入, 对比算法输出
 *   - 算法开发: 同一组数据反复测试不同参数
 */

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <cstring>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp/serialization.hpp"
#include "rclcpp/serialized_message.hpp"
#include "std_msgs/msg/float64.hpp"

#include "rosbag2_cpp/reader.hpp"
#include "rosbag2_storage/storage_options.hpp"

using namespace std::chrono_literals;

class BagPlayer : public rclcpp::Node
{
public:
  BagPlayer()
  : Node("bag_player_node")
  {
    // ★ 创建回放 publisher
    replay_publisher_ = this->create_publisher<std_msgs::msg::Float64>(
      "/sensor/replay", 10);

    // ★ Step 1: 打开 Reader
    rosbag2_storage::StorageOptions storage_options;
    storage_options.uri = "rosbag2_data/sensor_recording";
    storage_options.storage_id = "sqlite3";

    reader_ = std::make_unique<rosbag2_cpp::Reader>();
    reader_->open(storage_options);

    // ★ Step 2: 读取 bag 元数据
    auto metadata = reader_->get_metadata();
    RCLCPP_INFO(this->get_logger(),
      "Bag 文件: %s", storage_options.uri.c_str());
    RCLCPP_INFO(this->get_logger(),
      "消息数量: %zu", metadata.message_count);
    if (metadata.duration.count() > 0) {
      RCLCPP_INFO(this->get_logger(),
        "持续时间: %.1f 秒", metadata.duration.count() / 1e9);
    }

    // 打印录制的话题
    for (const auto & topic : metadata.topics_with_message_count) {
      RCLCPP_INFO(this->get_logger(),
        "  Topic: %s  Type: %s  Count: %zu",
        topic.topic_metadata.name.c_str(),
        topic.topic_metadata.type.c_str(),
        topic.message_count);
    }

    RCLCPP_INFO(this->get_logger(),
      "开始回放... (重新发布到 /sensor/replay)");

    play_count_ = 0;
    total_count_ = metadata.message_count;

    // ★ Step 3: 用 timer 控制回放速率 (50Hz)
    timer_ = this->create_wall_timer(
      20ms, std::bind(&BagPlayer::play_next, this));
  }

private:
  void play_next()
  {
    // ★ Step 4: 检查是否有下一条消息
    if (!reader_->has_next()) {
      RCLCPP_INFO(this->get_logger(),
        "回放完成! 共 %zu 条消息", play_count_);
      rclcpp::shutdown();
      return;
    }

    // ★ Step 5: 读取下一条 bag 消息
    auto bag_msg = reader_->read_next();

    // ★ Step 6: 反序列化 — 从 serialized_data 重建 ROS2 消息
    rclcpp::Serialization<std_msgs::msg::Float64> serializer;
    auto ros_msg = std::make_unique<std_msgs::msg::Float64>();

    // 从 bag_msg->serialized_data (shared_ptr<rcutils_uint8_array_t>) 反序列化
    if (bag_msg->serialized_data &&
        bag_msg->serialized_data->buffer_length > 0) {
      rclcpp::SerializedMessage serialized_msg;
      auto & rcl_ser = serialized_msg.get_rcl_serialized_message();
      rcl_ser.buffer = bag_msg->serialized_data->buffer;
      rcl_ser.buffer_length = bag_msg->serialized_data->buffer_length;

      serializer.deserialize_message(&serialized_msg, ros_msg.get());

      // ★ Step 7: 重新发布
      replay_publisher_->publish(std::move(ros_msg));

      play_count_++;

      if (play_count_ % 50 == 0) {
        RCLCPP_INFO(this->get_logger(),
          "回放进度: %zu / %zu", play_count_, total_count_);
      }
    }
  }

  std::unique_ptr<rosbag2_cpp::Reader> reader_;
  rclcpp::Publisher<std_msgs::msg::Float64>::SharedPtr replay_publisher_;
  rclcpp::TimerBase::SharedPtr timer_;
  size_t play_count_;
  size_t total_count_;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<BagPlayer>());
  rclcpp::shutdown();
  return 0;
}
