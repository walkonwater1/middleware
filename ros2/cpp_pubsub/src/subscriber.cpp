/**
 * ROS2 C++ 订阅者 Demo
 *
 * 订阅主题: /chatter  (类型: std_msgs::msg::String)
 * 统计接收帧数和延迟
 */

#include <memory>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

using std::placeholders::_1;

class Listener : public rclcpp::Node
{
public:
    Listener()
    : Node("listener"), count_(0)
    {
        // QoS: Reliable + KeepLast(10), 与 Talker 匹配
        auto qos = rclcpp::QoS(rclcpp::KeepLast(10)).reliable();

        subscription_ = this->create_subscription<std_msgs::msg::String>(
            "/chatter", qos,
            std::bind(&Listener::topic_callback, this, _1));

        RCLCPP_INFO(this->get_logger(), "Listener 已启动");
        RCLCPP_INFO(this->get_logger(), "等待 /chatter 数据...");
    }

private:
    void topic_callback(const std_msgs::msg::String::SharedPtr msg)
    {
        count_++;

        // 计算端到端延迟
        auto now = this->now();
        auto msg_time = rclcpp::Time(msg->data.find("timestamp:") != std::string::npos
            ? std::stoll(msg->data.substr(msg->data.find("timestamp:") + 11))
            : 0);
        auto latency_ms = (now.nanoseconds() - msg_time.nanoseconds()) / 1'000'000.0;

        RCLCPP_INFO(this->get_logger(),
            "#%zu 收到: '%s' (延迟: %.2f ms)", count_, msg->data.c_str(), latency_ms);
    }

    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr subscription_;
    size_t count_;
};

int main(int argc, char * argv[])
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<Listener>());
    rclcpp::shutdown();
    return 0;
}
