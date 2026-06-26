/**
 * qos_subscriber.cpp — ROS2 QoS 实验订阅者
 *
 * 对应 DDS: vehicle/qos_lab.c (subscriber 端)
 *
 * 用法:
 *   ros2 run qos_lab qos_subscriber --ros-args -p qos_mode:=reliable
 */

#include <chrono>
#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

using std::placeholders::_1;

class QoSSubscriber : public rclcpp::Node
{
public:
    QoSSubscriber()
    : Node("qos_subscriber"), count_(0), missed_(0), last_seq_(0)
    {
        this->declare_parameter("qos_mode", "reliable");
        std::string mode = this->get_parameter("qos_mode").as_string();

        auto qos = rclcpp::QoS(rclcpp::KeepLast(10));

        if (mode == "reliable") {
            qos.reliable();
            RCLCPP_INFO(this->get_logger(), "QoS: RELIABLE");
        } else if (mode == "besteffort") {
            qos.best_effort();
            RCLCPP_INFO(this->get_logger(), "QoS: BEST_EFFORT (可能丢包)");
        } else if (mode == "transient") {
            qos.reliable().transient_local();
            RCLCPP_INFO(this->get_logger(),
                "QoS: TRANSIENT_LOCAL (★ Late Joiner 可收到历史)");
        }

        subscription_ = this->create_subscription<std_msgs::msg::String>(
            "/qos_lab", qos,
            std::bind(&QoSSubscriber::callback, this, _1));

        RCLCPP_INFO(this->get_logger(), "等待 /qos_lab 数据...");
    }

private:
    void callback(const std_msgs::msg::String::SharedPtr msg)
    {
        count_++;

        /* 解析序列号检测丢帧 (对应 DDS qos_lab 的 missed 检测) */
        auto pos = msg->data.find("speed=");
        if (pos != std::string::npos) {
            int seq = std::stoi(msg->data.substr(pos + 6)) / 10;
            if (last_seq_ > 0 && seq != last_seq_ + 1)
                missed_ += (seq - last_seq_ - 1);
            last_seq_ = seq;
        }

        RCLCPP_INFO(this->get_logger(),
            "#%d ← %s %s",
            count_, msg->data.c_str(),
            missed_ > 0 ? "← 有丢帧!" : "✓");
    }

    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr subscription_;
    int count_;
    int missed_;
    int last_seq_;
};

int main(int argc, char* argv[])
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<QoSSubscriber>());
    rclcpp::shutdown();
    return 0;
}
