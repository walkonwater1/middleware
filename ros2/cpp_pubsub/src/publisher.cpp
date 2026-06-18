/**
 * ROS2 C++ 发布者 Demo
 *
 * 发布主题: /chatter  (类型: std_msgs::msg::String)
 * 包含消息计数和自定义帧头
 */

#include <chrono>
#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

using namespace std::chrono_literals;

class Talker : public rclcpp::Node
{
public:
    Talker()
    : Node("talker"), count_(0)
    {
        // QoS: Reliable + KeepLast(10)
        auto qos = rclcpp::QoS(rclcpp::KeepLast(10)).reliable();

        publisher_ = this->create_publisher<std_msgs::msg::String>("/chatter", qos);

        // 2Hz 发布
        timer_ = this->create_wall_timer(
            500ms, std::bind(&Talker::timer_callback, this));

        RCLCPP_INFO(this->get_logger(), "Talker 已启动 (2Hz)");
    }

private:
    void timer_callback()
    {
        auto message = std_msgs::msg::String();

        message.data = "Hello, ROS2! Message #" + std::to_string(++count_)
                     + " | timestamp: " + std::to_string(this->now().nanoseconds());

        RCLCPP_INFO(this->get_logger(), "发布: '%s'", message.data.c_str());
        publisher_->publish(message);
    }

    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_;
    rclcpp::TimerBase::SharedPtr timer_;
    size_t count_;
};

int main(int argc, char * argv[])
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<Talker>());
    rclcpp::shutdown();
    return 0;
}
