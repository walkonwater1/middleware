/**
 * battery_client.cpp — ROS2 Service Client
 *
 * 主动向 Server 发起请求, 等待响应 (同步 RPC 模式)
 *
 * 用法:
 *   ros2 run service_lab battery_client
 */

#include <chrono>
#include <memory>

#include "rclcpp/rclcpp.hpp"
#include "service_lab/srv/battery_query.hpp"

using namespace std::chrono_literals;

class BatteryClient : public rclcpp::Node
{
public:
    BatteryClient()
    : Node("battery_client"), count_(0)
    {
        client_ = this->create_client<service_lab::srv::BatteryQuery>(
            "/battery/query");

        /* 等待 Service 上线 */
        RCLCPP_INFO(this->get_logger(), "等待 /battery/query 上线...");
        while (!client_->wait_for_service(1s)) {
            if (!rclcpp::ok()) return;
            RCLCPP_INFO(this->get_logger(), "等待中...");
        }
        RCLCPP_INFO(this->get_logger(), "Service 已在线!");

        /* 每 3s 查询一次 */
        timer_ = this->create_wall_timer(
            3s, std::bind(&BatteryClient::query, this));
    }

private:
    void query()
    {
        auto request = std::make_shared<service_lab::srv::BatteryQuery::Request>();

        auto future = client_->async_send_request(request,
            std::bind(&BatteryClient::response_callback, this,
                       std::placeholders::_1));
    }

    void response_callback(
        rclcpp::Client<service_lab::srv::BatteryQuery>::SharedFuture future)
    {
        count_++;
        auto response = future.get();

        RCLCPP_INFO(this->get_logger(),
            "#%d 响应: batt=%.1f%% cpu=%.1f°C status=%s [%s]",
            count_,
            response->battery_level,
            response->cpu_temp,
            response->status.c_str(),
            response->ok ? "OK" : "WARN");
    }

    rclcpp::Client<service_lab::srv::BatteryQuery>::SharedPtr client_;
    rclcpp::TimerBase::SharedPtr timer_;
    int count_;
};

int main(int argc, char* argv[])
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<BatteryClient>());
    rclcpp::shutdown();
    return 0;
}
