/**
 * battery_server.cpp — ROS2 Service Server
 *
 * DDS 没有原生 Service, ROS2 在底层用两个 Topic (Request/Reply) 实现:
 *   Client → RequestTopic  → Server
 *   Client ← ReplyTopic    ← Server
 *
 * 用法:
 *   ros2 run service_lab battery_server
 *   ros2 service call /battery/query service_lab/srv/BatteryQuery
 */

#include <memory>

#include "rclcpp/rclcpp.hpp"
#include "service_lab/srv/battery_query.hpp"

class BatteryServer : public rclcpp::Node
{
public:
    BatteryServer()
    : Node("battery_server"), battery_(87.0), cpu_temp_(42.0), tick_(0)
    {
        using std::placeholders::_1;
        using std::placeholders::_2;

        service_ = this->create_service<service_lab::srv::BatteryQuery>(
            "/battery/query",
            std::bind(&BatteryServer::handle_query, this, _1, _2));

        /* 模拟电池变化 */
        timer_ = this->create_wall_timer(
            std::chrono::seconds(2),
            std::bind(&BatteryServer::simulate, this));

        RCLCPP_INFO(this->get_logger(),
            "Battery Server 已启动 (Service: /battery/query)");
        RCLCPP_INFO(this->get_logger(),
            "★ ROS2 Service = Req Topic + Rep Topic, DDS 无原生支持");
    }

private:
    void handle_query(
        const std::shared_ptr<service_lab::srv::BatteryQuery::Request> request,
        std::shared_ptr<service_lab::srv::BatteryQuery::Response> response)
    {
        (void)request;  /* 本服务 Request 为空 */

        response->battery_level = battery_;
        response->cpu_temp = cpu_temp_;
        response->ok = battery_ > 10.0;

        if (response->ok)
            response->status = "NORMAL";
        else
            response->status = "LOW_BATTERY";

        RCLCPP_INFO(this->get_logger(),
            "收到查询 → 响应: batt=%.1f%% cpu=%.1f°C [%s]",
            battery_, cpu_temp_, response->status.c_str());
    }

    void simulate()
    {
        tick_++;
        battery_ -= 2.0 + (rand() % 10) / 10.0;
        if (battery_ < 0.0) battery_ = 100.0;
        cpu_temp_ = 40.0 + (rand() % 20);
    }

    rclcpp::Service<service_lab::srv::BatteryQuery>::SharedPtr service_;
    rclcpp::TimerBase::SharedPtr timer_;
    double battery_;
    double cpu_temp_;
    int tick_;
};

int main(int argc, char* argv[])
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<BatteryServer>());
    rclcpp::shutdown();
    return 0;
}
