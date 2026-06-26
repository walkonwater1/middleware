/**
 * lifecycle_demo.cpp — ROS2 Lifecycle (Managed) Node
 *
 * ★ 对标 robot_dds_service.c 的状态机!
 *
 * Lifecycle 状态迁移:
 *   Unconfigured → Configuring → Inactive → Activating → Active
 *                                                  ↓
 *                                            Deactivating
 *                                                  ↓
 *                               CleaningUp ← Inactive
 *                                    ↓
 *                              ShuttingDown → Finalized
 *
 * 工程意义:
 *   - 生产级节点必须有明确的启动/停止生命周期
 *   - 避免节点在未配置状态下发送垃圾数据
 *   - Navigator/Controller 等节点的标准模式
 *
 * 用法:
 *   ros2 run lifecycle_lab lifecycle_demo
 *
 *   另一个终端:
 *   ros2 lifecycle list /lifecycle_node
 *   ros2 lifecycle set /lifecycle_node configure
 *   ros2 lifecycle set /lifecycle_node activate
 *   ros2 lifecycle set /lifecycle_node deactivate
 *   ros2 lifecycle set /lifecycle_node shutdown
 */

#include <chrono>
#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_lifecycle/lifecycle_node.hpp"
#include "std_msgs/msg/string.hpp"

using namespace std::chrono_literals;
using LifecycleNode = rclcpp_lifecycle::LifecycleNode;
using CallbackReturn = rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

class LifecycleDemo : public LifecycleNode
{
public:
    LifecycleDemo()
    : LifecycleNode("lifecycle_node")
    {}

    /* ★ on_configure: 加载参数/分配资源 */
    CallbackReturn on_configure(const rclcpp_lifecycle::State&)
    {
        RCLCPP_INFO(get_logger(), "on_configure() — 加载配置...");
        /* 创建 Publisher/Timer 但还不激活 */
        publisher_ = this->create_publisher<std_msgs::msg::String>("/lifecycle_topic", 10);
        timer_ = this->create_wall_timer(1s, [this]() {
            auto msg = std_msgs::msg::String();
            msg.data = "Lifecycle node active! Tick=" + std::to_string(tick_++);
            publisher_->publish(msg);
            RCLCPP_INFO(get_logger(), "发布: %s", msg.data.c_str());
        });
        /* 取消 timer — 在 Active 之前不发布任何数据 */
        timer_->cancel();
        return CallbackReturn::SUCCESS;
    }

    /* ★ on_activate: 开始工作 */
    CallbackReturn on_activate(const rclcpp_lifecycle::State&)
    {
        RCLCPP_INFO(get_logger(), "on_activate() — 开始工作!");
        timer_->reset();
        return CallbackReturn::SUCCESS;
    }

    /* ★ on_deactivate: 暂停工作 */
    CallbackReturn on_deactivate(const rclcpp_lifecycle::State&)
    {
        RCLCPP_INFO(get_logger(), "on_deactivate() — 暂停工作");
        timer_->cancel();
        return CallbackReturn::SUCCESS;
    }

    /* ★ on_cleanup: 清理资源 */
    CallbackReturn on_cleanup(const rclcpp_lifecycle::State&)
    {
        RCLCPP_INFO(get_logger(), "on_cleanup() — 清理资源");
        publisher_.reset();
        timer_.reset();
        return CallbackReturn::SUCCESS;
    }

    /* ★ on_shutdown: 最终关闭 */
    CallbackReturn on_shutdown(const rclcpp_lifecycle::State&)
    {
        RCLCPP_INFO(get_logger(), "on_shutdown() — 关闭");
        publisher_.reset();
        timer_.reset();
        return CallbackReturn::SUCCESS;
    }

private:
    rclcpp_lifecycle::LifecyclePublisher<std_msgs::msg::String>::SharedPtr publisher_;
    rclcpp::TimerBase::SharedPtr timer_;
    int tick_ = 0;
};

int main(int argc, char* argv[])
{
    rclcpp::init(argc, argv);

    auto node = std::make_shared<LifecycleDemo>();
    RCLCPP_INFO(node->get_logger(),
        "Lifecycle Node 启动, 等待 configure...");
    RCLCPP_INFO(node->get_logger(),
        "★ ros2 lifecycle set /lifecycle_node configure");
    RCLCPP_INFO(node->get_logger(),
        "★ 对标 robot_dds 状态机: Uninitialized→Setup→SelfCheck→Idle");

    rclcpp::spin(node->get_node_base_interface());
    rclcpp::shutdown();
    return 0;
}
