/**
 * robot_broadcaster.cpp — 机器人坐标变换广播器
 *
 * 演示要点:
 *   1. 动态坐标变换 (TransformBroadcaster) — odom→base_link 持续变化
 *   2. 静态坐标变换 — base_link→laser_frame 传感器固定安装偏移
 *   3. 模拟机器人以半径 1.0m 做圆周运动
 *   4. TransformBroadcaster 发布到 /tf (Reliable QoS)
 *
 * 用法:
 *   ros2 run tf2_lab robot_broadcaster
 *   ros2 topic echo /tf           # 查看持续更新的动态变换
 *   ros2 topic echo /tf_static    # 查看传感器安装偏移
 *
 * 工程意义:
 *   真实机器人中:
 *   - odom→base_link 来自轮式里程计/IMU 融合, 持续修正漂移
 *   - base_link→laser  是机械安装参数, 出厂标定后不变
 *   - 一切以 base_link 为中心: 传感器相对于本体安装
 *
 * DDS 对应:
 *   TransformBroadcaster 底层是 Publisher<tf2_msgs::msg::TFMessage>
 *   发布到 /tf topic (Reliable QoS), 和 DDS DataWriter 完全对应
 */

#include <chrono>
#include <cmath>
#include <functional>
#include <memory>

#include "rclcpp/rclcpp.hpp"
#include "tf2_ros/transform_broadcaster.h"
#include "tf2_ros/static_transform_broadcaster.h"
#include "geometry_msgs/msg/transform_stamped.hpp"
#include "tf2/LinearMath/Quaternion.h"

using namespace std::chrono_literals;

class RobotBroadcaster : public rclcpp::Node
{
public:
  RobotBroadcaster()
  : Node("robot_broadcaster_node"), angle_(0.0)
  {
    // ★ 动态广播器 — 发布 /tf topic
    dyn_broadcaster_ =
      std::make_shared<tf2_ros::TransformBroadcaster>(this);

    // ★ 静态广播器 — 发布 /tf_static topic (传感器安装偏移)
    static_broadcaster_ =
      std::make_shared<tf2_ros::StaticTransformBroadcaster>(this);

    // 发布激光雷达安装偏移 (一次性, TransientLocal)
    publish_laser_mount();

    // ★ 100ms 定时器: 模拟机器人运动, 持续更新 odom→base_link
    timer_ = this->create_wall_timer(
      100ms, std::bind(&RobotBroadcaster::publish_motion, this));

    RCLCPP_INFO(this->get_logger(),
      "机器人广播器已启动: 100Hz odom→base_link (圆周运动)");
    RCLCPP_INFO(this->get_logger(),
      "变换链: map → odom → base_link → laser_frame");
    RCLCPP_INFO(this->get_logger(),
      "提示: ros2 run tf2_tools tf2_echo map base_link 可查看运动");
  }

private:
  void publish_laser_mount()
  {
    // base_link → laser_frame: 激光雷达安装在机器人前方 0.2m, 高度 0.3m
    geometry_msgs::msg::TransformStamped t;
    t.header.stamp = this->now();
    t.header.frame_id = "base_link";
    t.child_frame_id = "laser_frame";
    t.transform.translation.x = 0.2;   // 前方 20cm
    t.transform.translation.y = 0.0;   // 正中
    t.transform.translation.z = 0.3;   // 离地 30cm
    t.transform.rotation.x = 0.0;
    t.transform.rotation.y = 0.0;
    t.transform.rotation.z = 0.0;
    t.transform.rotation.w = 1.0;

    static_broadcaster_->sendTransform(t);
    RCLCPP_INFO(this->get_logger(),
      "传感器安装偏移: base_link → laser_frame (前0.2m, 高0.3m)");
  }

  void publish_motion()
  {
    // ★ 圆周运动模拟: x = cos(θ), y = sin(θ), θ += 0.1rad/step
    double x = std::cos(angle_) * 1.0;   // 半径 1.0m
    double y = std::sin(angle_) * 1.0;
    double yaw = angle_ + M_PI_2;         // 朝向切线方向

    // 欧拉角 → 四元数 (绕 Z 轴旋转 yaw)
    tf2::Quaternion q;
    q.setRPY(0, 0, yaw);

    geometry_msgs::msg::TransformStamped t;
    t.header.stamp = this->now();
    t.header.frame_id = "odom";
    t.child_frame_id = "base_link";
    t.transform.translation.x = x;
    t.transform.translation.y = y;
    t.transform.translation.z = 0.0;
    t.transform.rotation.x = q.x();
    t.transform.rotation.y = q.y();
    t.transform.rotation.z = q.z();
    t.transform.rotation.w = q.w();

    // ★ 发送动态变换 — 底层是普通 Publisher, 对标 DDS DataWriter
    dyn_broadcaster_->sendTransform(t);

    angle_ += 0.1;  // 每步 0.1rad ≈ 5.7°, 100Hz → 约 1.6 rps
    if (angle_ > 2 * M_PI) {
      angle_ -= 2 * M_PI;
    }

    RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
      "odom→base_link: x=%.2f y=%.2f yaw=%.2f°",
      x, y, yaw * 180.0 / M_PI);
  }

  std::shared_ptr<tf2_ros::TransformBroadcaster> dyn_broadcaster_;
  std::shared_ptr<tf2_ros::StaticTransformBroadcaster> static_broadcaster_;
  rclcpp::TimerBase::SharedPtr timer_;
  double angle_;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<RobotBroadcaster>());
  rclcpp::shutdown();
  return 0;
}
