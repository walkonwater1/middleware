/**
 * vehicle_subscriber.cpp — ROS2 自定义消息订阅者
 *
 * 对应 DDS: vehicle/subscriber.c
 *
 * DDS → ROS2 对比:
 *   dds_create_reader(pp, tp, qos) → create_subscription()
 *   dds_read() 轮询                 → 回调函数 (自动触发, 对应 Listener)
 *   void* samples[] + cast          → SharedPtr 智能指针, 直接使用
 */

#include <memory>

#include "rclcpp/rclcpp.hpp"
#include "topic_lab/msg/vehicle_state.hpp"

using std::placeholders::_1;

class VehicleSubscriber : public rclcpp::Node
{
public:
    VehicleSubscriber()
    : Node("vehicle_subscriber"), count_(0)
    {
        auto qos = rclcpp::QoS(rclcpp::KeepLast(10)).reliable();

        subscription_ = this->create_subscription<topic_lab::msg::VehicleState>(
            "/vehicle/state", qos,
            std::bind(&VehicleSubscriber::topic_callback, this, _1));

        RCLCPP_INFO(this->get_logger(),
            "Vehicle Subscriber 已启动");
        RCLCPP_INFO(this->get_logger(),
            "等待 /vehicle/state 数据... (对应 DDS dds_read → ROS2 回调)");
    }

private:
    void topic_callback(const topic_lab::msg::VehicleState::SharedPtr msg)
    {
        count_++;

        /* 计算端到端延迟 (对应 DDS latency 计算) */
        auto now_ns = this->now().nanoseconds();
        double latency_ms = (now_ns - msg->timestamp) / 1'000'000.0;

        RCLCPP_INFO(this->get_logger(),
            "#%d ← %s | speed=%.1f | pos=(%.4f,%.4f) | SOC=%.1f%% | lat=%.2fms",
            count_, msg->vehicle_id.c_str(), msg->speed,
            msg->latitude, msg->longitude, msg->battery_soc, latency_ms);

        /* 低电量告警 (对应 DDS 告警检测) */
        if (msg->battery_soc < 20.0) {
            RCLCPP_WARN(this->get_logger(),
                "⚠ 电量过低: %.1f%%", msg->battery_soc);
        }
    }

    rclcpp::Subscription<topic_lab::msg::VehicleState>::SharedPtr subscription_;
    int count_;
};

int main(int argc, char* argv[])
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<VehicleSubscriber>());
    rclcpp::shutdown();
    return 0;
}
