/**
 * gRPC Server — 机器人状态服务
 *
 * 演示三种 gRPC 通信模式:
 *   1. Unary RPC        — GetStatus (一问一答)
 *   2. Server Streaming — StreamTelemetry (服务端持续推送)
 *   3. Client Streaming — ReportTaskProgress (客户端持续上报)
 *
 * 模拟 6 轴机械臂: 每 1s 更新关节角度/速度/扭矩, 推送遥测数据
 */

#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <chrono>
#include <mutex>
#include <random>
#include <signal.h>

#include <grpcpp/grpcpp.h>
#include "robot.grpc.pb.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerWriter;
using grpc::ServerReader;
using grpc::Status;

// ============================================================================
// 模拟机器人数据
// ============================================================================
static const char* kJointNames[6] = {
    "joint_base", "joint_shoulder", "joint_elbow",
    "joint_wrist1", "joint_wrist2", "joint_wrist3"
};

class RobotSimulator {
public:
    double battery = 85.0;         // 电池 %
    double cpu = 32.0;             // CPU %
    double mem = 41.0;             // 内存 %
    double joint_pos[6] = {0};     // 关节位置 (rad)
    int64_t start_ms;

    RobotSimulator() {
        start_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        for (int i = 0; i < 6; i++)
            joint_pos[i] = i * 0.2;  // 初始角度
    }

    void update() {
        // 模拟关节运动 (正弦波)
        static double phase[6] = {0, 0.5, 1.0, 1.5, 2.0, 2.5};
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        double t = (now - start_ms) / 1000.0;
        for (int i = 0; i < 6; i++)
            joint_pos[i] = sin(t * 0.5 + phase[i]) * 1.5;

        battery -= 0.002; if (battery < 0) battery = 100.0;
        cpu = 30.0 + sin(t * 0.3) * 15.0;
        mem = 40.0 + cos(t * 0.25) * 8.0;
    }

    robot::RobotState state() const { return robot::RobotState::RUNNING; }

    int64_t now_ms() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }
};

// ============================================================================
// 服务实现
// ============================================================================
class RobotServiceImpl final : public robot::RobotService::Service {
public:
    RobotServiceImpl() {
        std::cout << "🤖 机器人模拟器已启动 (6 轴机械臂)" << std::endl;
    }

    // --- 1. Unary RPC: 查询即时状态 ---
    Status GetStatus(ServerContext* ctx, const robot::GetStatusRequest* req,
                     robot::GetStatusResponse* rsp) override {
        sim_.update();
        rsp->set_robot_id(req->robot_id());
        rsp->set_state(sim_.state());
        rsp->set_battery(sim_.battery);
        rsp->set_timestamp_ms(sim_.now_ms());

        // 填充 6 轴关节状态
        for (int i = 0; i < 6; i++) {
            auto* j = rsp->add_joints();
            j->set_name(kJointNames[i]);
            j->set_position(sim_.joint_pos[i]);
            j->set_velocity(cos(sim_.joint_pos[i]) * 0.5);
            j->set_torque(sin(sim_.joint_pos[i]) * 2.0);
            j->set_temperature(35.0 + i * 3.0);
        }

        // 模拟当前任务
        auto* task = rsp->mutable_current_task();
        task->set_task_id("TASK-0001");
        task->set_description("装配任务 - 拧紧 M8 螺栓");
        task->set_progress(fmod(sim_.now_ms() / 100.0, 100.0));

        std::cout << "[Unary] GetStatus: robot=" << req->robot_id()
                  << " battery=" << sim_.battery << "% joints=" << 6
                  << " latency=" << (sim_.now_ms() - sim_.start_ms) << "ms"
                  << std::endl;
        return Status::OK;
    }

    // --- 2. Server Streaming: 持续推送遥测 ---
    Status StreamTelemetry(ServerContext* ctx,
                           const robot::StreamTelemetryRequest* req,
                           ServerWriter<robot::TelemetrySample>* writer) override {
        int interval = req->interval_ms() > 0 ? req->interval_ms() : 1000;
        std::cout << "[Stream] StreamTelemetry: robot=" << req->robot_id()
                  << " interval=" << interval << "ms (按 Ctrl-C 停止)"
                  << std::endl;

        int seq = 0;
        while (!ctx->IsCancelled()) {
            sim_.update();
            robot::TelemetrySample sample;
            sample.set_robot_id(req->robot_id());
            sample.set_battery(sim_.battery);
            sample.set_cpu_usage(sim_.cpu);
            sample.set_mem_usage(sim_.mem);
            sample.set_timestamp_ms(sim_.now_ms());

            for (int i = 0; i < 6; i++) {
                auto* j = sample.add_joints();
                j->set_name(kJointNames[i]);
                j->set_position(sim_.joint_pos[i]);
                j->set_velocity(cos(sim_.joint_pos[i]) * 0.5);
                j->set_torque(sin(sim_.joint_pos[i]) * 1.8);
                j->set_temperature(35.0 + i * 3.0);
            }

            if (!writer->Write(sample)) break;  // 客户端断开
            seq++;
            std::cout << "  [#" << seq << "] battery=" << sim_.battery
                      << "% cpu=" << sim_.cpu << "% mem=" << sim_.mem << "%"
                      << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(interval));
        }
        std::cout << "[Stream] 客户端断开, 共推送 " << seq << " 条" << std::endl;
        return Status::OK;
    }

    // --- 3. Client Streaming: 接收任务进度上报 ---
    Status ReportTaskProgress(ServerContext* ctx,
                              ServerReader<robot::TaskReport>* reader,
                              robot::TaskSummary* summary) override {
        robot::TaskReport report;
        int count = 0;
        std::string task_id;
        double max_progress = 0;

        while (reader->Read(&report)) {
            count++;
            task_id = report.task_id();
            if (report.progress() > max_progress)
                max_progress = report.progress();
            std::cout << "[ClientStream] task=" << task_id
                      << " progress=" << report.progress() << "%"
                      << " msg=" << report.message() << std::endl;
        }

        summary->set_task_id(task_id);
        summary->set_completed(max_progress >= 100.0);
        summary->set_total_progress(max_progress);
        summary->set_report_count(count);
        std::cout << "[ClientStream] 完成: task=" << task_id
                  << " progress=" << max_progress << "% reports=" << count
                  << std::endl;
        return Status::OK;
    }

private:
    RobotSimulator sim_;
};

// ============================================================================
// Main
// ============================================================================
std::unique_ptr<Server> g_server;

void on_signal(int) {
    std::cout << "\n🛑 收到退出信号, 关闭服务器..." << std::endl;
    if (g_server) g_server->Shutdown();
}

int main(int argc, char* argv[]) {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    std::string addr = "0.0.0.0:50051";
    RobotServiceImpl service;

    ServerBuilder builder;
    builder.AddListeningPort(addr, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    g_server = builder.BuildAndStart();

    std::cout << "========================================" << std::endl;
    std::cout << "  gRPC RobotService 已启动" << std::endl;
    std::cout << "  监听: " << addr << std::endl;
    std::cout << "  RPC 模式:" << std::endl;
    std::cout << "    1. GetStatus          — Unary (一问一答)" << std::endl;
    std::cout << "    2. StreamTelemetry     — Server Streaming (服务端持续推送)" << std::endl;
    std::cout << "    3. ReportTaskProgress  — Client Streaming (客户端持续上报)" << std::endl;
    std::cout << "========================================" << std::endl;

    g_server->Wait();
    return 0;
}
