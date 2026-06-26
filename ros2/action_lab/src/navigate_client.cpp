/**
 * navigate_client.cpp — ROS2 Action Client
 *
 * 发送导航目标 → 接收进度反馈 → 获取结果
 *
 * 用法:
 *   ros2 run action_lab navigate_client
 */

#include <memory>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "action_lab/action/navigate_to.hpp"

using NavigateTo = action_lab::action::NavigateTo;

class NavigateClient : public rclcpp::Node
{
public:
    NavigateClient()
    : Node("navigate_client")
    {
        client_ = rclcpp_action::create_client<NavigateTo>(this, "/navigate");

        /* 发送测试目标 */
        timer_ = this->create_wall_timer(
            std::chrono::seconds(8),
            std::bind(&NavigateClient::send_goal, this));

        RCLCPP_INFO(this->get_logger(),
            "Navigate Client 已启动, 等待 /navigate Action...");
    }

private:
    void send_goal()
    {
        if (!client_->wait_for_action_server(std::chrono::seconds(3))) {
            RCLCPP_WARN(this->get_logger(), "Action Server 未上线!");
            return;
        }

        auto goal = NavigateTo::Goal();
        goal.target_x = 10.0 + (rand() % 50);
        goal.target_y = 8.0 + (rand() % 40);

        RCLCPP_INFO(this->get_logger(),
            "发送导航目标: (%.1f, %.1f)", goal.target_x, goal.target_y);

        auto send_goal_options = rclcpp_action::Client<NavigateTo>::SendGoalOptions();

        /* 反馈回调: 实时进度更新 */
        send_goal_options.feedback_callback =
            [this](auto, const std::shared_ptr<const NavigateTo::Feedback> feedback) {
                RCLCPP_INFO(this->get_logger(),
                    "← 反馈: %.0f%% (%.2f, %.2f) [%s]",
                    feedback->progress,
                    feedback->current_x, feedback->current_y,
                    feedback->status_text.c_str());
            };

        /* 结果回调: 任务完成 */
        send_goal_options.result_callback =
            [this](const rclcpp_action::ClientGoalHandle<NavigateTo>::WrappedResult& result) {
                if (result.code == rclcpp_action::ResultCode::SUCCEEDED) {
                    RCLCPP_INFO(this->get_logger(),
                        "✅ 导航成功! 距离=%.2f", result.result->distance);
                } else {
                    RCLCPP_WARN(this->get_logger(),
                        "❌ 导航失败/取消");
                }
            };

        client_->async_send_goal(goal, send_goal_options);
    }

    rclcpp_action::Client<NavigateTo>::SharedPtr client_;
    rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char* argv[])
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<NavigateClient>());
    rclcpp::shutdown();
    return 0;
}
