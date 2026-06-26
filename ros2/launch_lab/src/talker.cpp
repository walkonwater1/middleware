/**
 * talker.cpp — Launch Lab: 发布者
 * 由 launch 文件自动启动, 无需手动 ros2 run
 */
#include <chrono>
#include <memory>
#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

using namespace std::chrono_literals;

class Talker : public rclcpp::Node
{
public:
    Talker() : Node("talker"), count_(0)
    {
        publisher_ = this->create_publisher<std_msgs::msg::String>("/launch_topic", 10);
        timer_ = this->create_wall_timer(1s, [this]() {
            auto msg = std_msgs::msg::String();
            msg.data = "Launched! Msg #" + std::to_string(++count_);
            publisher_->publish(msg);
            RCLCPP_INFO(get_logger(), "Talker: %s", msg.data.c_str());
        });
        RCLCPP_INFO(get_logger(), "Talker started by launch file");
    }
private:
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_;
    rclcpp::TimerBase::SharedPtr timer_;
    int count_;
};

int main(int argc, char* argv[]) {
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<Talker>());
    rclcpp::shutdown();
    return 0;
}
