/**
 * listener.cpp — Launch Lab: 订阅者
 * 由 launch 文件自动启动
 */
#include <memory>
#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

class Listener : public rclcpp::Node
{
public:
    Listener() : Node("listener"), count_(0)
    {
        subscription_ = this->create_subscription<std_msgs::msg::String>(
            "/launch_topic", 10,
            [this](const std_msgs::msg::String::SharedPtr msg) {
                RCLCPP_INFO(get_logger(), "Listener: %s", msg->data.c_str());
            });
        RCLCPP_INFO(get_logger(), "Listener started by launch file");
    }
private:
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr subscription_;
    int count_;
};

int main(int argc, char* argv[]) {
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<Listener>());
    rclcpp::shutdown();
    return 0;
}
