/**
 * demo_service.cpp — GDBus C++17 机器人服务端入口
 *
 * 模拟机器人核心系统：
 *   - 状态机: Uninitialized → Setup → SelfCheck → Idle ↔ Working
 *   - 任务调度: 支持 Navigation/Motion/VoiceInteraction 等任务
 *   - 健康指标: 电量、CPU温度、表情状态
 *
 * 工作流 (修改接口只需第1步):
 *   1. 编辑 interfaces/com.example.Robot.xml
 *   2. cmake .. && make  (自动生成 C + C++ 代码)
 *   3. 本站只写业务逻辑
 */

#include "gdbus_cxx.hpp"
#include "robot_bindings.hpp"   // ★ 自动生成, 来自 gen_cpp_bindings.py

#include <atomic>
#include <clocale>
#include <csignal>
#include <cstdio>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

namespace {

gdbus_cxx::MainLoop* g_loop = nullptr;

void on_signal(int sig) {
    g_print("\n收到信号 %d, 退出...\n", sig);
    if (g_loop) g_loop->quit();
}

// ==========================================================================
// 模拟机器人内部状态 (对应 RobotStateManager + TaskOrchestrator)
// ==========================================================================

// 任务状态 (对应 TaskState in task_types.h)
enum class SimTaskStatus {
    Starting, Running, Completed, Failed
};

struct SimTask {
    std::string id;
    std::string type;
    std::string name;
    SimTaskStatus status = SimTaskStatus::Starting;
    double progress = 0.0;  // 0-100
};

// 机器人状态 (对应 RobotState in robot_state.h)
enum class SimRobotState {
    Uninitialized, Setup, SelfCheck, Idle, Working,
    LowPower, ManualControl, Fault, EmergencyStop, Terminated
};

const char* state_name(SimRobotState s) {
    switch (s) {
    case SimRobotState::Uninitialized: return "Uninitialized";
    case SimRobotState::Setup:         return "Setup";
    case SimRobotState::SelfCheck:     return "SelfCheck";
    case SimRobotState::Idle:          return "Idle";
    case SimRobotState::Working:       return "Working";
    case SimRobotState::LowPower:      return "LowPower";
    case SimRobotState::ManualControl: return "ManualControl";
    case SimRobotState::Fault:         return "Fault";
    case SimRobotState::EmergencyStop: return "EmergencyStop";
    case SimRobotState::Terminated:    return "Terminated";
    }
    return "Unknown";
}

// 表情 (对应 Emotion enum)
std::vector<std::string> kEmotions = {
    "Natural", "Joy", "Anger", "Anxiety", "Thought", "Grief", "Fear", "Fright"
};

class RobotSimulator {
public:
    RobotSimulator()
        : robot_id_("ROBOT-001")
        , state_(SimRobotState::Uninitialized)
        , battery_(87.0)
        , cpu_temp_(42.0)
        , emotion_("Natural")
        , task_counter_(0)
    {}

    const std::string& robot_id() const { return robot_id_; }
    SimRobotState state() const { return state_.load(); }
    double battery() const { return battery_.load(); }
    double cpu_temp() const { return cpu_temp_.load(); }
    const std::string& emotion() const { return emotion_; }

    // 状态转移 (对应 kValidTransitions)
    bool transition_to(SimRobotState to) {
        SimRobotState from = state_.load();
        if (!is_valid_transition(from, to)) {
            g_print("[SIM] 非法转移: %s → %s\n", state_name(from), state_name(to));
            return false;
        }
        state_.store(to);
        g_print("[SIM] 状态变更: %s → %s\n", state_name(from), state_name(to));
        return true;
    }

    // 初始化流程: Uninitialized → Setup → SelfCheck → Idle
    void initialize() {
        transition_to(SimRobotState::Setup);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        transition_to(SimRobotState::SelfCheck);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        transition_to(SimRobotState::Idle);
    }

    // 任务下发 (对应 TaskOrchestrator::StartTask)
    std::string add_task(const std::string& type, const std::string& name) {
        auto id = "TASK-" + std::to_string(++task_counter_);
        SimTask task{id, type, name, SimTaskStatus::Starting, 0.0};

        // Idle → Working (对应 active_count 0→1 时 RobotStateManager 切换)
        if (state_.load() == SimRobotState::Idle) {
            transition_to(SimRobotState::Working);
        }

        std::lock_guard<std::recursive_mutex> lk(tasks_mutex_);
        tasks_[id] = task;
        return id;
    }

    bool cancel_task(const std::string& task_id) {
        std::lock_guard<std::recursive_mutex> lk(tasks_mutex_);
        auto it = tasks_.find(task_id);
        if (it == tasks_.end()) return false;
        it->second.status = SimTaskStatus::Failed;
        it->second.progress = 0.0;
        return true;
    }

    // 获取活跃任务数 (对应 GetActiveTaskCount)
    int active_task_count() {
        std::lock_guard<std::recursive_mutex> lk(tasks_mutex_);
        int count = 0;
        for (auto& [id, t] : tasks_) {
            if (t.status == SimTaskStatus::Starting ||
                t.status == SimTaskStatus::Running) count++;
        }
        return count;
    }

    // 更新任务进度 (模拟执行)
    void update_tasks(double elapsed_sec, std::function<void(const SimTask&)> on_progress) {
        std::lock_guard<std::recursive_mutex> lk(tasks_mutex_);
        for (auto& [id, t] : tasks_) {
            if (t.status == SimTaskStatus::Starting) {
                t.status = SimTaskStatus::Running;
                t.progress = 5.0;
            } else if (t.status == SimTaskStatus::Running) {
                t.progress += elapsed_sec * 20.0;  // ~5秒完成
                if (t.progress >= 100.0) {
                    t.progress = 100.0;
                    t.status = SimTaskStatus::Completed;
                }
            }
            if (on_progress) on_progress(t);
        }

        // 所有任务完成后 Working → Idle (对应 active_count 1→0)
        if (state_.load() == SimRobotState::Working && active_task_count() == 0) {
            // 不能在锁内调用 transition_to，先释放锁
            // (简化处理: 放在外部定时器检查)
        }
    }

    void cleanup_completed() {
        std::lock_guard<std::recursive_mutex> lk(tasks_mutex_);
        for (auto it = tasks_.begin(); it != tasks_.end(); ) {
            if (it->second.status == SimTaskStatus::Completed ||
                it->second.status == SimTaskStatus::Failed) {
                it = tasks_.erase(it);
            } else {
                ++it;
            }
        }
        // Working → Idle
        if (state_.load() == SimRobotState::Working && active_task_count() == 0) {
            transition_to(SimRobotState::Idle);
        }
    }

    // 健康指标模拟
    void update_health(double elapsed_sec) {
        thread_local std::mt19937 gen(std::random_device{}());
        thread_local std::uniform_real_distribution<double> temp_dist(-0.5, 0.5);
        thread_local std::uniform_real_distribution<double> battery_dist(-0.1, 0.0);

        // CPU 温度波动 (Working 时更高)
        double base = (state_.load() == SimRobotState::Working) ? 58.0 : 42.0;
        double target = base + temp_dist(gen);
        cpu_temp_.store(cpu_temp_.load() * 0.7 + target * 0.3);

        // 电量缓慢下降
        battery_.store(battery_.load() + battery_dist(gen));
        if (battery_.load() < 0.0) battery_.store(100.0);  // 模拟充电
    }

    // 表情根据状态自动切换
    void update_emotion() {
        switch (state_.load()) {
        case SimRobotState::Idle:    emotion_ = "Natural"; break;
        case SimRobotState::Working: emotion_ = "Thought"; break;
        case SimRobotState::Fault:   emotion_ = "Anxiety"; break;
        case SimRobotState::LowPower: emotion_ = "Grief"; break;
        default: emotion_ = "Natural"; break;
        }
    }

private:
    bool is_valid_transition(SimRobotState from, SimRobotState to) {
        // 对应 kValidTransitions (简化版)
        using S = SimRobotState;
        switch (from) {
        case S::Uninitialized: return to == S::Setup || to == S::Fault || to == S::EmergencyStop || to == S::Terminated;
        case S::Setup:         return to == S::SelfCheck || to == S::Fault || to == S::EmergencyStop || to == S::Terminated;
        case S::SelfCheck:     return to == S::Idle || to == S::Fault || to == S::EmergencyStop || to == S::Terminated;
        case S::Idle:          return to == S::Working || to == S::LowPower || to == S::ManualControl || to == S::Fault || to == S::EmergencyStop || to == S::Terminated;
        case S::Working:       return to == S::Idle || to == S::LowPower || to == S::ManualControl || to == S::Fault || to == S::EmergencyStop || to == S::Terminated;
        case S::LowPower:      return to == S::Idle || to == S::Fault || to == S::EmergencyStop || to == S::Terminated;
        case S::ManualControl: return to == S::Idle || to == S::Working || to == S::Fault || to == S::EmergencyStop || to == S::Terminated;
        case S::Fault:         return to == S::Idle || to == S::EmergencyStop || to == S::Terminated;
        case S::EmergencyStop: return to == S::Idle || to == S::Terminated;
        case S::Terminated:    return false;
        }
        return false;
    }

    std::string robot_id_;
    std::atomic<SimRobotState> state_;
    std::atomic<double> battery_;
    std::atomic<double> cpu_temp_;
    std::string emotion_;
    int task_counter_;
    std::map<std::string, SimTask> tasks_;
    std::recursive_mutex tasks_mutex_;
};

} // namespace

int main() {
    setlocale(LC_ALL, "");
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    gdbus_cxx::MainLoop loop;
    g_loop = &loop;

    RobotSimulator robot;

    g_print("=== 机器人 GDBus 服务端 ===\n");
    g_print("机器人 ID: %s\n", robot.robot_id().c_str());
    g_print("状态机:    Uninitialized → Setup → SelfCheck → Idle ↔ Working\n");
    g_print("接口定义:  interfaces/com.example.Robot.xml\n\n");

    // 初始化机器人 (对应 Manager::Init → RobotStateManager::Initialize)
    g_print("[INIT] 开始初始化...\n");
    robot.initialize();
    g_print("[INIT] 初始化完成, 当前状态: %s\n\n", state_name(robot.state()));

    // ★ 自动生成的骨架类
    DemoRobotServer skel;

    // ---- GetRobotStatus handler ----
    skel.set_get_robot_status_handler([&]() -> DemoRobotServer::GetRobotStatusResult {
        return {
            robot.robot_id(),
            state_name(robot.state()),
            robot.battery(),
            robot.cpu_temp()
        };
    });

    // ---- ExecuteTask handler (对应 TaskOrchestrator::StartTask) ----
    skel.set_execute_task_handler([&](const std::string& task_type,
                                       const std::string& task_name)
        -> DemoRobotServer::ExecuteTaskResult {
        if (robot.state() == SimRobotState::Fault ||
            robot.state() == SimRobotState::EmergencyStop ||
            robot.state() == SimRobotState::Terminated) {
            g_print("[SERVICE] 拒绝任务: 机器人处于 %s 状态\n",
                    state_name(robot.state()));
            return {"", false};
        }
        auto task_id = robot.add_task(task_type, task_name);
        g_print("[SERVICE] 下发任务: [%s] %s/%s\n",
                task_id.c_str(), task_type.c_str(), task_name.c_str());
        return {task_id, true};
    });

    // ---- CancelTask handler ----
    skel.set_cancel_task_handler([&](const std::string& task_id)
        -> DemoRobotServer::CancelTaskResult {
        bool ok = robot.cancel_task(task_id);
        g_print("[SERVICE] 取消任务 %s: %s\n", task_id.c_str(), ok ? "成功" : "失败");
        return {ok};
    });

    // ---- EmergencyStop handler ----
    skel.set_emergency_stop_handler([&]() -> DemoRobotServer::EmergencyStopResult {
        g_print("[SERVICE] ⚠ 紧急停止!\n");
        robot.transition_to(SimRobotState::EmergencyStop);
        return {true};
    });

    // 获取总线名
    gdbus_cxx::BusName bus;
    bus.own("com.example.RobotService",
        [&](GDBusConnection* conn) {
            skel.export_on_bus(conn, "/com/example/Robot");

            // 设置初始属性值
            skel.set_state(state_name(robot.state()));
            skel.set_battery_level(robot.battery());
            skel.set_cpu_temp(robot.cpu_temp());
            skel.set_emotion(robot.emotion());

            // 主定时器: 1s 周期 (对应 ROS2 1s heartbeat timer)
            // 在 timer 创建时捕获初始状态，确保能检测到 handler 中发生的状态变更
            auto last_seen_state = robot.state();
            gdbus_cxx::timeout_add_seconds(1, [&, last_seen_state]() mutable -> gboolean {
                static int tick = 0;
                tick++;

                // 更新任务进度
                robot.update_tasks(1.0, [&](const SimTask& t) {
                    const char* status_str = "Unknown";
                    switch (t.status) {
                    case SimTaskStatus::Starting: status_str = "Starting"; break;
                    case SimTaskStatus::Running:  status_str = "Running"; break;
                    case SimTaskStatus::Completed: status_str = "Completed"; break;
                    case SimTaskStatus::Failed:    status_str = "Failed"; break;
                    }
                    g_print("[TASK] %s: %.0f%% (%s)\n",
                            t.id.c_str(), t.progress, status_str);

                    // 发射任务进度信号
                    skel.emit_task_progress(t.id, t.progress, status_str);
                });
                robot.cleanup_completed();

                // 属性始终同步 (状态可能在 handler 中被修改)
                auto current_state = robot.state();
                skel.set_state(state_name(current_state));
                robot.update_emotion();
                skel.set_emotion(robot.emotion());

                // 状态变更信号 (用持久化变量追踪)
                if (last_seen_state != current_state) {
                    skel.emit_state_changed(state_name(last_seen_state),
                                            state_name(current_state));
                    last_seen_state = current_state;
                }

                // 更新健康指标
                robot.update_health(1.0);
                skel.set_battery_level(robot.battery());
                skel.set_cpu_temp(robot.cpu_temp());

                // 低电量告警
                if (robot.battery() < 15.0 && current_state != SimRobotState::LowPower) {
                    robot.transition_to(SimRobotState::LowPower);
                    skel.emit_battery_low(robot.battery());
                }

                // 定期打印状态
                if (tick % 5 == 0) {
                    g_print("[STATUS] 状态=%s 电量=%.1f%% CPU=%.1f°C 表情=%s 活跃任务=%d\n",
                            state_name(current_state),
                            robot.battery(), robot.cpu_temp(),
                            robot.emotion().c_str(),
                            robot.active_task_count());
                }

                return G_SOURCE_CONTINUE;
            });

            g_print("[SERVICE] 已启动, 按 Ctrl+C 退出\n\n");
        },
        [&]() {
            g_print("[SERVICE] 总线名丢失!\n");
            loop.quit();
        });

    loop.run();
    g_print("服务已停止.\n");
    return 0;
}
