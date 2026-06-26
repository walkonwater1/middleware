/**
 * param_demo.cpp — ROS2 Parameter 动态参数
 *
 * 演示:
 *   1. 声明参数 (带默认值)
 *   2. 运行时读取参数
 *   3. 参数变更回调 (无需重启)
 *   4. CLI 修改参数
 *
 * 用法:
 *   ros2 run param_lab param_demo
 *
 *   # 另一个终端操作参数:
 *   ros2 param list
 *   ros2 param get /param_node speed
 *   ros2 param set /param_node speed 80.0
 *   ros2 param describe /param_node speed
 */

#include <chrono>
#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

using namespace std::chrono_literals;

class ParamDemo : public rclcpp::Node
{
public:
    ParamDemo() : Node("param_node")
    {
        /* 声明参数 */
        this->declare_parameter("speed", 30.0);
        this->declare_parameter("robot_name", std::string("robot-001"));
        this->declare_parameter("enable_debug", false);

        /* ★ 参数变更回调: 无需重启节点 */
        param_callback_handle_ = this->add_on_set_parameters_callback(
            [this](const std::vector<rclcpp::Parameter>& params) {
                rcl_interfaces::msg::SetParametersResult result;
                result.successful = true;
                for (auto& p : params) {
                    RCLCPP_INFO(get_logger(),
                        "⚡ 参数变更: %s = %s",
                        p.get_name().c_str(),
                        p.value_to_string().c_str());
                }
                return result;
            });

        /* 定时打印当前参数 */
        timer_ = this->create_wall_timer(3s, [this]() {
            RCLCPP_INFO(get_logger(),
                "当前参数: speed=%.1f robot=%s debug=%s",
                this->get_parameter("speed").as_double(),
                this->get_parameter("robot_name").as_string().c_str(),
                this->get_parameter("enable_debug").as_bool() ? "ON" : "OFF");
        });

        RCLCPP_INFO(get_logger(), "Parameter Demo 已启动");
        RCLCPP_INFO(get_logger(), "试试: ros2 param set /param_node speed 80.0");
    }

private:
    rclcpp::TimerBase::SharedPtr timer_;
    OnSetParametersCallbackHandle::SharedPtr param_callback_handle_;
};

int main(int argc, char* argv[])
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<ParamDemo>());
    rclcpp::shutdown();
    return 0;
}
