/**
 * vehicle_publisher.cpp — ROS2 自定义消息发布者
 *
 * 对应 DDS: vehicle/publisher.c
 *
 * DDS → ROS2 对比:
 *   dds_create_participant()     → rclcpp::Node
 *   dds_create_topic(desc)       → create_publisher<VehicleState>()
 *   dds_write(wr, &sample)      → publisher_->publish(msg)
 *   sleep(1)                     → create_wall_timer(500ms, ...)
 */

#include <chrono>
#include <memory>

#include "rclcpp/rclcpp.hpp"
#include "custom_msg/msg/vehicle_state.hpp"

using namespace std::chrono_literals;

class VehiclePublisher : public rclcpp::Node
{
public:
    VehiclePublisher()
    : Node("vehicle_publisher"), count_(0)
    {
        /* QoS: 对应 DDS Reliable + KeepLast(10) */
        auto qos = rclcpp::QoS(rclcpp::KeepLast(10)).reliable();

        publisher_ = this->create_publisher<custom_msg::msg::VehicleState>(
            "/vehicle/state", qos);

        /* Timer 替代 DDS 的 sleep(1) + 循环 */
        timer_ = this->create_wall_timer(
            500ms, std::bind(&VehiclePublisher::timer_callback, this));

        RCLCPP_INFO(this->get_logger(),
            "Vehicle Publisher 已启动 (Topic: /vehicle/state, 2Hz)");
        RCLCPP_INFO(this->get_logger(),
            "QoS: Reliable + KeepLast(10) ← 对应 DDS QoS");
    }

private:
    void timer_callback()
    {
        auto msg = custom_msg::msg::VehicleState();

        /* 模拟数据 (与 DDS publisher.c 相同的逻辑) */
        msg.vehicle_id = "ros2-car-" + std::to_string(count_ % 100);
        msg.speed = 30.0 + (count_ % 10) * 5.0;
        msg.latitude = 22.53 + count_ * 0.001;
        msg.longitude = 113.93 + count_ * 0.001;
        msg.battery_soc = 85.0 - count_ * 0.3;

        /* 时间戳 */
        msg.timestamp = this->now().nanoseconds();

        publisher_->publish(msg);
        count_++;

        RCLCPP_INFO(this->get_logger(),
            "#%d %s speed=%.1f SOC=%.1f%%",
            count_, msg.vehicle_id.c_str(), msg.speed, msg.battery_soc);
    }

    rclcpp::Publisher<custom_msg::msg::VehicleState>::SharedPtr publisher_;
    rclcpp::TimerBase::SharedPtr timer_;
    int count_;
};

int main(int argc, char* argv[])
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<VehiclePublisher>());
    rclcpp::shutdown();
    return 0;
}
