/**
 * transform_listener.cpp — TF2 变换监听器
 *
 * 演示要点:
 *   1. tf2_ros::Buffer 缓存所有 /tf + /tf_static 数据
 *   2. tf2_ros::TransformListener 自动订阅 tf topic
 *   3. canTransform 检查变换可用性 (避免等待超时)
 *   4. lookupTransform 正向查询 (odom → base_link)
 *   5. lookupTransform 反向查询 (base_link → map, 穿透中间帧)
 *   6. 完整变换链: laser_frame → base_link → odom → map
 *
 * 用法:
 *   # 先启动广播器 (另一个终端):
 *   ros2 launch tf2_lab tf2_demo.launch.py
 *   # 或单独运行:
 *   ros2 run tf2_lab transform_listener
 *
 * 验证:
 *   ros2 run tf2_tools tf2_echo map laser_frame   # CLI 监听变换
 *   ros2 run tf2_tools view_frames                 # 生成 PDF 变换树
 *
 * 工程意义:
 *   感知模块需要把传感器数据从 sensor_frame 转换到 map 做融合:
 *   point_cloud_in_map = T_map_base * T_base_laser * point_cloud
 *   tf2::Buffer + lookupTransform 是实现这一切的基础
 */

#include <chrono>
#include <functional>
#include <memory>

#include "rclcpp/rclcpp.hpp"
#include "tf2_ros/transform_listener.h"
#include "tf2_ros/buffer.h"
#include "tf2_geometry_msgs/tf2_geometry_msgs.hpp"
#include "geometry_msgs/msg/transform_stamped.hpp"

using namespace std::chrono_literals;

class TransformListener : public rclcpp::Node
{
public:
  TransformListener()
  : Node("transform_listener_node")
  {
    // ★ Buffer — 缓存所有收到的 tf 数据 (内部维护帧间关系图)
    tf_buffer_ = std::make_unique<tf2_ros::Buffer>(this->get_clock());

    // ★ TransformListener — 自动订阅 /tf 和 /tf_static topic
    tf_listener_ = std::make_shared<tf2_ros::TransformListener>(*tf_buffer_);

    // 500ms 定时查询变换链 — 给 Buffer 时间填充数据
    timer_ = this->create_wall_timer(
      500ms, std::bind(&TransformListener::lookup_transforms, this));

    RCLCPP_INFO(this->get_logger(),
      "TF2 监听器已启动, 每 500ms 查询变换链");
    RCLCPP_INFO(this->get_logger(),
      "等待 map/odom/base_link/laser_frame 帧...");
  }

private:
  void lookup_transforms()
  {
    // ★ Step 1: 检查必需的帧是否已在 tf tree 中可用
    if (!tf_buffer_->canTransform("map", "base_link", tf2::TimePointZero)) {
      RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 3000,
        "等待变换链... 请确认 static_broadcaster 和 robot_broadcaster 已启动");
      return;
    }

    try {
      // ★ Step 2: 正向查询 — odom → base_link (机器人当前位置)
      auto odom_to_base = tf_buffer_->lookupTransform(
        "odom", "base_link", tf2::TimePointZero);

      double x = odom_to_base.transform.translation.x;
      double y = odom_to_base.transform.translation.y;

      RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
        "odom → base_link: x=%.3f y=%.3f", x, y);

      // ★ Step 3: 反向查询 — map → base_link (穿透 odom 中间帧)
      auto map_to_base = tf_buffer_->lookupTransform(
        "map", "base_link", tf2::TimePointZero);

      // ★ Step 4: 全变换链查询 — map → laser_frame (穿透 base_link)
      auto map_to_laser = tf_buffer_->lookupTransform(
        "map", "laser_frame", tf2::TimePointZero);

      double laser_x = map_to_laser.transform.translation.x;
      double laser_y = map_to_laser.transform.translation.y;
      double laser_z = map_to_laser.transform.translation.z;

      // ★ 打印完整变换链
      RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
        "laser_frame → map: x=%.3f y=%.3f z=%.3f  "
        "[链: laser→base_link→odom→map]",
        laser_x, laser_y, laser_z);

      // ★ Step 5: 逆变换 — base_link → map (反向穿透链)
      auto base_to_map = tf_buffer_->lookupTransform(
        "base_link", "map", tf2::TimePointZero);

      RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 3000,
        "base_link → map (逆变换): x=%.3f y=%.3f",
        base_to_map.transform.translation.x,
        base_to_map.transform.translation.y);

    } catch (const tf2::TransformException & ex) {
      RCLCPP_WARN(this->get_logger(), "变换查询失败: %s", ex.what());
    }
  }

  std::unique_ptr<tf2_ros::Buffer> tf_buffer_;
  std::shared_ptr<tf2_ros::TransformListener> tf_listener_;
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<TransformListener>());
  rclcpp::shutdown();
  return 0;
}
