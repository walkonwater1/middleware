/**
 * qos_publisher.cpp — ROS2 QoS 实验发布者
 *
 * 对应 DDS: vehicle/qos_lab.c
 *
 * 对比 Reliable vs BestEffort 在 ROS2 中的表现
 *
 * 用法:
 *   ros2 run qos_lab qos_publisher --ros-args -p qos_mode:=reliable
 *   ros2 run qos_lab qos_publisher --ros-args -p qos_mode:=besteffort
 *   ros2 run qos_lab qos_publisher --ros-args -p qos_mode:=transient
 */

#include <chrono>
#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

using namespace std::chrono_literals;

class QoSPublisher : public rclcpp::Node
{
public:
    QoSPublisher()
    : Node("qos_publisher"), count_(0)
    {
        /* 从参数读取 QoS 模式 */
        this->declare_parameter("qos_mode", "reliable");
        std::string mode = this->get_parameter("qos_mode").as_string();

        auto qos = rclcpp::QoS(rclcpp::KeepLast(10));

        if (mode == "reliable") {
            qos.reliable();
            RCLCPP_INFO(this->get_logger(),
                "QoS: RELIABLE + KeepLast(10) ← DDS Reliable");
        } else if (mode == "besteffort") {
            qos.best_effort();
            RCLCPP_INFO(this->get_logger(),
                "QoS: BEST_EFFORT + KeepLast(10) ← DDS BestEffort");
        } else if (mode == "transient") {
            qos.reliable().transient_local();
            RCLCPP_INFO(this->get_logger(),
                "QoS: RELIABLE + TRANSIENT_LOCAL ← Late Joiner 可拿历史");
        }

        publisher_ = this->create_publisher<std_msgs::msg::String>(
            "/qos_lab", qos);

        /* 10Hz 发布 */
        timer_ = this->create_wall_timer(
            100ms, std::bind(&QoSPublisher::publish, this));
    }

private:
    void publish()
    {
        auto msg = std_msgs::msg::String();
        count_++;
        msg.data = "QoS test #" + std::to_string(count_)
                 + " speed=" + std::to_string(count_ * 10);

        publisher_->publish(msg);

        if (count_ % 10 == 0) {
            RCLCPP_INFO(this->get_logger(),
                "已发布 %d 条", count_);
        }

        if (count_ >= 50) {
            RCLCPP_INFO(this->get_logger(), "发布完成, 退出");
            rclcpp::shutdown();
        }
    }

    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_;
    rclcpp::TimerBase::SharedPtr timer_;
    int count_;
};

int main(int argc, char* argv[])
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<QoSPublisher>());
    rclcpp::shutdown();
    return 0;
}
