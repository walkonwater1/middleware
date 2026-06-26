/**
 * bag_recorder.cpp — Rosbag2 编程式录制器
 *
 * 演示要点:
 *   1. rosbag2_cpp::Writer — C++ API 编程式录制 (非 CLI)
 *   2. StorageOptions — 设置 bag 存储路径和格式 (sqlite3)
 *   3. TopicMetadata — 注册要录制的消息类型
 *   4. 手动序列化: rclcpp::Serialization → SerializedMessage
 *   5. 话题过滤 — 只录制 /sensor/data, 忽略其他
 *
 * 用法:
 *   # Terminal 1: 生成数据
 *   ros2 run rosbag2_lab data_generator
 *   # Terminal 2: 录制
 *   ros2 run rosbag2_lab bag_recorder
 *
 * CLI 对比:
 *   ros2 bag record -o my_bag /sensor/data   ← 等效 CLI
 *   ros2 bag info rosbag2_data/sensor_recording  ← 查看录制结果
 *
 * DDS 对应:
 *   rosbag2 录制的是 DDS CDR 编码后的 RTPS wire 数据
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

#include "rosbag2_cpp/writer.hpp"
#include "rosbag2_storage/storage_options.hpp"
#include "rosbag2_storage/topic_metadata.hpp"

using namespace std::chrono_literals;

class BagRecorder : public rclcpp::Node
{
public:
  BagRecorder()
  : Node("bag_recorder_node"), msg_count_(0)
  {
    // ★ Step 1: 配置存储选项
    rosbag2_storage::StorageOptions storage_options;
    storage_options.uri = "rosbag2_data/sensor_recording";
    storage_options.storage_id = "sqlite3";

    // ★ Step 2: 注册话题元数据 (录制前必须声明)
    rosbag2_storage::TopicMetadata topic_meta;
    topic_meta.name = "/sensor/data";
    topic_meta.type = "std_msgs/msg/Float64";
    topic_meta.serialization_format = "cdr";

    // ★ Step 3: 打开 Writer
    writer_ = std::make_unique<rosbag2_cpp::Writer>();
    writer_->open(storage_options);
    writer_->create_topic(topic_meta);

    RCLCPP_INFO(this->get_logger(),
      "Rosbag2 录制器启动 → %s", storage_options.uri.c_str());
    RCLCPP_INFO(this->get_logger(),
      "录制 Topic: %s (类型: %s)", topic_meta.name.c_str(), topic_meta.type.c_str());

    // ★ Step 4: 订阅 /sensor/data, 回调中写入 bag
    subscription_ = this->create_subscription<std_msgs::msg::Float64>(
      "/sensor/data", 10,
      std::bind(&BagRecorder::on_message, this, std::placeholders::_1));

    // 10 秒后自动停止
    stop_timer_ = this->create_wall_timer(
      10s, std::bind(&BagRecorder::stop_recording, this));

    RCLCPP_INFO(this->get_logger(), "录制中... (10s 后自动停止)");
  }

private:
  void on_message(std::shared_ptr<const std_msgs::msg::Float64> msg)
  {
    // ★ Step 5: 序列化 ROS2 消息 → CDR 格式
    rclcpp::Serialization<std_msgs::msg::Float64> serializer;
    rclcpp::SerializedMessage serialized_msg;
    serializer.serialize_message(msg.get(), &serialized_msg);

    // ★ Step 6: 获取序列化后的 buffer (rcutils_uint8_array_t)
    const auto & rcl_ser = serialized_msg.get_rcl_serialized_message();

    // ★ Step 7: 构造 bag 消息 — 拷贝序列化数据
    auto bag_msg = std::make_shared<rosbag2_storage::SerializedBagMessage>();

    // 分配并拷贝数据到 serialized_data (类型: shared_ptr<rcutils_uint8_array_t>)
    auto ser_data = std::make_shared<rcutils_uint8_array_t>();
    ser_data->buffer_length = rcl_ser.buffer_length;
    ser_data->buffer_capacity = rcl_ser.buffer_capacity;
    ser_data->buffer = static_cast<uint8_t *>(
      std::malloc(rcl_ser.buffer_length));
    std::memcpy(ser_data->buffer, rcl_ser.buffer, rcl_ser.buffer_length);

    bag_msg->serialized_data = ser_data;
    bag_msg->time_stamp = this->now().nanoseconds();

    // ★ Step 8: 写入 bag
    writer_->write(bag_msg);

    msg_count_++;
    if (msg_count_ % 50 == 0) {
      RCLCPP_INFO(this->get_logger(),
        "已录制 %d 条消息 (当前值: %.3f)", msg_count_, msg->data);
    }
  }

  void stop_recording()
  {
    RCLCPP_INFO(this->get_logger(),
      "录制完成! 总计 %d 条消息", msg_count_);
    RCLCPP_INFO(this->get_logger(),
      "Bag 文件: rosbag2_data/sensor_recording/");
    RCLCPP_INFO(this->get_logger(),
      "检查: ros2 bag info rosbag2_data/sensor_recording");
    rclcpp::shutdown();
  }

  std::unique_ptr<rosbag2_cpp::Writer> writer_;
  rclcpp::Subscription<std_msgs::msg::Float64>::SharedPtr subscription_;
  rclcpp::TimerBase::SharedPtr stop_timer_;
  int msg_count_;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<BagRecorder>());
  rclcpp::shutdown();
  return 0;
}
