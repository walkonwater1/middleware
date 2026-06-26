/**
 * static_broadcaster.cpp — 静态坐标变换广播器
 *
 * 演示要点:
 *   1. 静态坐标变换 (StaticTransformBroadcaster) — map→odom 固定关系
 *   2. TransientLocal QoS 自动生效 — Late Joiner 也能收到历史变换
 *   3. 只需在构造函数中发送一次即可 (不需 timer 持续发布)
 *
 * 用法:
 *   ros2 run tf2_lab static_broadcaster
 *   ros2 topic echo /tf_static          # 查看 latch 的静态变换
 *   ros2 run tf2_tools tf2_echo map odom  # 验证变换
 *
 * DDS 对应:
 *   /tf_static topic 使用 TransientLocal Durability — 对标 DDS qos_lab
 *   中的 TransientLocal: 后加入的 Reader 仍能拿到历史样本
 */

#include <chrono>
#include <functional>
#include <memory>

#include "rclcpp/rclcpp.hpp"
#include "tf2_ros/static_transform_broadcaster.h"
#include "geometry_msgs/msg/transform_stamped.hpp"
#include "tf2/LinearMath/Quaternion.h"

using namespace std::chrono_literals;

class StaticBroadcaster : public rclcpp::Node
{
public:
  StaticBroadcaster()
  : Node("static_broadcaster_node")
  {
    // ★ StaticTransformBroadcaster — 发布到 /tf_static (TransientLocal)
    tf_static_broadcaster_ =
      std::make_shared<tf2_ros::StaticTransformBroadcaster>(this);

    // 构造 map → odom 变换 (世界原点 → 里程计原点)
    geometry_msgs::msg::TransformStamped t;
    t.header.stamp = this->now();
    t.header.frame_id = "map";        // 父帧: 世界坐标系
    t.child_frame_id = "odom";        // 子帧: 里程计坐标系
    t.transform.translation.x = 0.0;  // odom 与 map 同原点
    t.transform.translation.y = 0.0;
    t.transform.translation.z = 0.0;
    t.transform.rotation.x = 0.0;     // 无旋转
    t.transform.rotation.y = 0.0;
    t.transform.rotation.z = 0.0;
    t.transform.rotation.w = 1.0;

    // ★ 发送一次即可 — TransientLocal 保证后续订阅者也能收到
    tf_static_broadcaster_->sendTransform(t);

    RCLCPP_INFO(this->get_logger(),
      "已发布静态变换: map → odom (同原点, 无偏移)");
    RCLCPP_INFO(this->get_logger(),
      "提示: ros2 topic echo /tf_static 可查看 latched 变换");
    RCLCPP_INFO(this->get_logger(),
      "提示: ros2 run tf2_tools tf2_echo map odom 可验证");
  }

private:
  std::shared_ptr<tf2_ros::StaticTransformBroadcaster> tf_static_broadcaster_;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<StaticBroadcaster>();
  rclcpp::spin(node);  // 持续运行, 保持 /tf_static 在 Discovery 中可见
  rclcpp::shutdown();
  return 0;
}
