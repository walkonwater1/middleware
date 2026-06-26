/**
 * navigate_server.cpp — ROS2 Action Server (导航模拟)
 *
 * Action = 5 Topic 实现异步任务:
 *   /navigate/_action/send_goal     ← Client 发目标
 *   /navigate/_action/get_result    → Server 返回结果
 *   /navigate/_action/feedback       → Server 发进度
 *   /navigate/_action/cancel_goal   ← Client 取消
 *   /navigate/_action/status         → Server 发状态
 *
 * 用法:
 *   ros2 run action_lab navigate_server
 */

#include <memory>
#include <cmath>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "action_lab/action/navigate_to.hpp"

using NavigateTo = action_lab::action::NavigateTo;
using GoalHandle = rclcpp_action::ServerGoalHandle<NavigateTo>;

class NavigateServer : public rclcpp::Node
{
public:
    NavigateServer()
    : Node("navigate_server")
    {
        action_server_ = rclcpp_action::create_server<NavigateTo>(
            this, "/navigate",
            std::bind(&NavigateServer::handle_goal, this,
                       std::placeholders::_1, std::placeholders::_2),
            std::bind(&NavigateServer::handle_cancel, this,
                       std::placeholders::_1),
            std::bind(&NavigateServer::handle_accepted, this,
                       std::placeholders::_1));

        RCLCPP_INFO(this->get_logger(),
            "Navigate Server 已启动 (Action: /navigate)");
        RCLCPP_INFO(this->get_logger(),
            "★ Action = Goal+Result+Feedback+Cancel+Status (5 Topics)");
    }

private:
    rclcpp_action::GoalResponse handle_goal(
        const rclcpp_action::GoalUUID& uuid,
        std::shared_ptr<const NavigateTo::Goal> goal)
    {
        (void)uuid;
        RCLCPP_INFO(this->get_logger(),
            "收到导航目标: (%.1f, %.1f)", goal->target_x, goal->target_y);
        return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
    }

    rclcpp_action::CancelResponse handle_cancel(
        const std::shared_ptr<GoalHandle> goal_handle)
    {
        (void)goal_handle;
        RCLCPP_INFO(this->get_logger(), "收到取消请求");
        return rclcpp_action::CancelResponse::ACCEPT;
    }

    void handle_accepted(const std::shared_ptr<GoalHandle> goal_handle)
    {
        /* 在新线程中执行 (模拟长时间任务) */
        std::thread{std::bind(&NavigateServer::execute, this, goal_handle)}.detach();
    }

    void execute(const std::shared_ptr<GoalHandle> goal_handle)
    {
        auto goal = goal_handle->get_goal();
        double tx = goal->target_x;
        double ty = goal->target_y;

        RCLCPP_INFO(this->get_logger(),
            "开始导航: (0,0) → (%.1f, %.1f)", tx, ty);

        double dist = std::sqrt(tx * tx + ty * ty);
        auto feedback = std::make_shared<NavigateTo::Feedback>();
        auto result = std::make_shared<NavigateTo::Result>();

        /* 模拟导航过程 (5 秒, 每 200ms 更新进度) */
        for (int step = 0; step < 25; step++) {
            if (goal_handle->is_canceling()) {
                result->success = false;
                result->distance = dist * feedback->progress / 100.0;
                goal_handle->canceled(result);
                RCLCPP_WARN(this->get_logger(), "导航已取消!");
                return;
            }

            double pct = (step + 1) * 4.0;  /* 0→100% */
            feedback->progress = pct;
            feedback->current_x = tx * pct / 100.0;
            feedback->current_y = ty * pct / 100.0;
            feedback->status_text = "Moving... (" + std::to_string((int)pct) + "%)";

            goal_handle->publish_feedback(feedback);

            RCLCPP_INFO(this->get_logger(),
                "进度: %.0f%% (%.2f, %.2f)",
                pct, feedback->current_x, feedback->current_y);

            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        result->success = true;
        result->distance = dist;
        goal_handle->succeed(result);

        RCLCPP_INFO(this->get_logger(),
            "导航完成! 距离=%.2f", dist);
    }

    rclcpp_action::Server<NavigateTo>::SharedPtr action_server_;
};

int main(int argc, char* argv[])
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<NavigateServer>());
    rclcpp::shutdown();
    return 0;
}
