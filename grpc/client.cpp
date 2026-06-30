/**
 * gRPC Client — 机器人状态服务客户端
 *
 * 演示三种调用模式:
 *   1. GetStatus           — 同步 Unary RPC
 *   2. StreamTelemetry      — 接收 Server Streaming (打印 5 条后退出)
 *   3. ReportTaskProgress   — 发送 Client Streaming (模拟 5 次进度上报)
 */

#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <chrono>

#include <grpcpp/grpcpp.h>
#include "robot.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientWriter;
using grpc::Status;

const char* state_name(robot::RobotState s) {
    switch (s) {
        case robot::RobotState::IDLE:    return "IDLE";
        case robot::RobotState::RUNNING: return "RUNNING";
        case robot::RobotState::CHARGING:return "CHARGING";
        case robot::RobotState::FAULT:   return "FAULT";
        case robot::RobotState::EMERGENCY_STOP: return "EMERGENCY_STOP";
        default: return "UNKNOWN";
    }
}

// ============================================================================
// 1. Unary RPC — 查询即时状态
// ============================================================================
void demo_unary(robot::RobotService::Stub& stub) {
    std::cout << "\n=== 1. Unary RPC: GetStatus ===" << std::endl;

    ClientContext ctx;
    robot::GetStatusRequest req;
    robot::GetStatusResponse rsp;

    req.set_robot_id("robot-arm-01");
    Status status = stub.GetStatus(&ctx, req, &rsp);

    if (status.ok()) {
        std::cout << "  Robot:   " << rsp.robot_id() << std::endl;
        std::cout << "  State:   " << state_name(rsp.state()) << std::endl;
        std::cout << "  Battery: " << rsp.battery() << "%" << std::endl;
        std::cout << "  Joints:  " << rsp.joints_size() << " 轴" << std::endl;
        for (int i = 0; i < rsp.joints_size(); i++) {
            auto& j = rsp.joints(i);
            std::cout << "    [" << j.name() << "] pos=" << j.position()
                      << " vel=" << j.velocity()
                      << " torque=" << j.torque()
                      << " temp=" << j.temperature() << "°C" << std::endl;
        }
        if (rsp.has_current_task()) {
            std::cout << "  Task:    " << rsp.current_task().description()
                      << " (" << rsp.current_task().progress() << "%)"
                      << std::endl;
        }
        std::cout << "  Time:    " << rsp.timestamp_ms() << "ms" << std::endl;
    } else {
        std::cerr << "  RPC 失败: " << status.error_message() << std::endl;
    }
}

// ============================================================================
// 2. Server Streaming — 接收遥测推送
// ============================================================================
void demo_server_streaming(robot::RobotService::Stub& stub) {
    std::cout << "\n=== 2. Server Streaming: StreamTelemetry ===" << std::endl;

    ClientContext ctx;
    robot::StreamTelemetryRequest req;
    req.set_robot_id("robot-arm-01");
    req.set_interval_ms(500);  // 每 500ms 推送一条

    std::unique_ptr<ClientReader<robot::TelemetrySample>> reader(
        stub.StreamTelemetry(&ctx, req));

    robot::TelemetrySample sample;
    int count = 0;
    while (reader->Read(&sample) && count < 5) {
        count++;
        std::cout << "  [#" << count << "] battery=" << sample.battery()
                  << "% cpu=" << sample.cpu_usage()
                  << "% mem=" << sample.mem_usage()
                  << "% joints=" << sample.joints_size()
                  << " ts=" << sample.timestamp_ms() << std::endl;
    }

    // 主动取消流
    ctx.TryCancel();
    Status status = reader->Finish();
    std::cout << "  收到 " << count << " 条后主动断开" << std::endl;
}

// ============================================================================
// 3. Client Streaming — 上报任务进度
// ============================================================================
void demo_client_streaming(robot::RobotService::Stub& stub) {
    std::cout << "\n=== 3. Client Streaming: ReportTaskProgress ===" << std::endl;

    ClientContext ctx;
    robot::TaskSummary summary;
    std::unique_ptr<ClientWriter<robot::TaskReport>> writer(
        stub.ReportTaskProgress(&ctx, &summary));

    // 模拟 5 次进度上报
    for (int i = 1; i <= 5; i++) {
        robot::TaskReport report;
        report.set_task_id("TASK-0042");
        report.set_progress(i * 20.0);
        report.set_message(i == 5 ? "全部完成!" : "正在执行...");
        std::cout << "  → progress=" << report.progress()
                  << "% msg=" << report.message() << std::endl;
        writer->Write(report);
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
    }

    writer->WritesDone();
    Status status = writer->Finish();

    if (status.ok()) {
        std::cout << "  服务端返回: task=" << summary.task_id()
                  << " completed=" << (summary.completed() ? "true" : "false")
                  << " progress=" << summary.total_progress() << "%"
                  << " reports=" << summary.report_count() << std::endl;
    }
}

// ============================================================================
// Main
// ============================================================================
int main(int argc, char* argv[]) {
    std::string addr = "localhost:50051";
    if (argc > 1) addr = argv[1];

    auto channel = grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());
    auto stub = robot::RobotService::NewStub(channel);

    std::cout << "========================================" << std::endl;
    std::cout << "  gRPC RobotService 客户端" << std::endl;
    std::cout << "  连接: " << addr << std::endl;
    std::cout << "========================================" << std::endl;

    // 1. Unary RPC
    demo_unary(*stub);

    // 2. Server Streaming
    demo_server_streaming(*stub);

    // 3. Client Streaming
    demo_client_streaming(*stub);

    std::cout << "\n✅ 三种 RPC 模式演示完成" << std::endl;
    return 0;
}
