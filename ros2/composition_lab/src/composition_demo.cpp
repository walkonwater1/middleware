/**
 * composition_demo.cpp — ROS2 Node Composition (进程内通信)
 *
 * 两个 Node 在同一个进程中, 使用 IntraProcess 通信:
 *   - PubNode: 发布数据
 *   - SubNode: 订阅数据
 *   - 数据通过共享内存传递, 无序列化/反序列化开销
 *
 * DDS 对比:
 *   - DDS: 即使同进程, 数据也走 UDP loopback
 *   - ROS2 Composition: 同进程内走共享内存, 零拷贝
 *
 * 用法:
 *   ros2 run composition_lab composition_demo
 */

#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

using namespace std::chrono_literals;

/* Publisher Node */
class PubNode : public rclcpp::Node
{
public:
    PubNode() : Node("pub_node"), count_(0)
    {
        auto qos = rclcpp::QoS(rclcpp::KeepLast(10));
        /* ★ IntraProcess 必须在 QoS 中显式启用 */
        publisher_ = this->create_publisher<std_msgs::msg::String>("/intra_topic", qos);

        timer_ = this->create_wall_timer(500ms, [this]() {
            auto msg = std_msgs::msg::String();
            msg.data = "IntraProcess #" + std::to_string(++count_);
            publisher_->publish(msg);
            RCLCPP_INFO(get_logger(), "Pub: %s", msg.data.c_str());
        });
    }

private:
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_;
    rclcpp::TimerBase::SharedPtr timer_;
    int count_;
};

/* Subscriber Node */
class SubNode : public rclcpp::Node
{
public:
    SubNode() : Node("sub_node"), count_(0)
    {
        auto qos = rclcpp::QoS(rclcpp::KeepLast(10));
        subscription_ = this->create_subscription<std_msgs::msg::String>(
            "/intra_topic", qos,
            [this](const std_msgs::msg::String::SharedPtr msg) {
                RCLCPP_INFO(get_logger(), "Sub (#%d): %s", ++count_, msg->data.c_str());
            });
    }

private:
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr subscription_;
    int count_;
};

int main(int argc, char* argv[])
{
    rclcpp::init(argc, argv);

    /* ★ 使用 MultiThreadedExecutor 让两个 Node 并发 */
    rclcpp::executors::MultiThreadedExecutor exec;

    auto pub = std::make_shared<PubNode>();
    auto sub = std::make_shared<SubNode>();

    exec.add_node(pub);
    exec.add_node(sub);

    RCLCPP_INFO(pub->get_logger(),
        "Node Composition Demo — 2 Nodes in 1 Process");
    RCLCPP_INFO(pub->get_logger(),
        "★ PubNode + SubNode 同进程, 共享内存通信");
    RCLCPP_INFO(pub->get_logger(),
        "★ 对比 DDS: DDS 即使同进程也走 UDP loopback");

    exec.spin();
    rclcpp::shutdown();
    return 0;
}
