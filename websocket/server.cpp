/**
 * WebSocket Server — 机器人遥测推送
 *
 * 演示 WebSocket 核心特性:
 *   1. 连接管理 — 多客户端并发连接
 *   2. 双向消息 — 文本/二进制帧
 *   3. 服务端推送 — 定时广播遥测数据
 *   4. Ping/Pong — 心跳保活
 *
 * 协议: ws://localhost:9002
 */

#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <set>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>
#include <signal.h>
#include <cmath>

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

using websocketpp::connection_hdl;
typedef websocketpp::server<websocketpp::config::asio> WsServer;

// ============================================================================
// 机器人模拟器
// ============================================================================
class RobotSimulator {
public:
    double battery = 85.0;
    double joint_pos[6] = {0};
    int64_t start_ms;

    RobotSimulator() {
        start_ms = now_ms();
        for (int i = 0; i < 6; i++) joint_pos[i] = i * 0.2;
    }

    void update() {
        double t = (now_ms() - start_ms) / 1000.0;
        for (int i = 0; i < 6; i++)
            joint_pos[i] = sin(t * 0.5 + i * 0.5) * 1.5;
        battery -= 0.002; if (battery < 0) battery = 100.0;
    }

    std::string to_json() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3);
        oss << "{";
        oss << "\"timestamp\":" << now_ms() << ",";
        oss << "\"battery\":" << battery << ",";
        oss << "\"cpu\":" << (30.0 + sin((now_ms()-start_ms)/1000.0*0.3)*15.0) << ",";
        oss << "\"mem\":" << (40.0 + cos((now_ms()-start_ms)/1000.0*0.25)*8.0) << ",";
        oss << "\"joints\":[";
        for (int i = 0; i < 6; i++) {
            if (i > 0) oss << ",";
            oss << "{\"name\":\"" << kj(i) << "\","
                << "\"position\":" << joint_pos[i] << ","
                << "\"velocity\":" << cos(joint_pos[i])*0.5 << ","
                << "\"torque\":" << sin(joint_pos[i])*1.8 << ","
                << "\"temperature\":" << (35.0 + i*3.0)
                << "}";
        }
        oss << "]}";
        return oss.str();
    }

private:
    static const char* kj(int i) {
        static const char* n[6] = {"base","shoulder","elbow","wrist1","wrist2","wrist3"};
        return n[i];
    }
    int64_t now_ms() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }
};

// ============================================================================
// WebSocket Server
// ============================================================================
class RobotWsServer {
public:
    RobotWsServer() {
        // 初始化 WebSocket server
        server_.init_asio();
        server_.set_reuse_addr(true);

        // 连接/断开回调
        server_.set_open_handler([this](connection_hdl hdl) {
            on_open(hdl);
        });
        server_.set_close_handler([this](connection_hdl hdl) {
            on_close(hdl);
        });

        // 消息回调
        server_.set_message_handler([this](connection_hdl hdl, WsServer::message_ptr msg) {
            on_message(hdl, msg);
        });
    }

    void run(uint16_t port) {
        server_.listen(port);
        server_.start_accept();
        std::cout << "========================================" << std::endl;
        std::cout << "  WebSocket 机器人遥测服务" << std::endl;
        std::cout << "  监听: ws://localhost:" << port << std::endl;
        std::cout << "  特性:" << std::endl;
        std::cout << "    • 多客户端并发连接" << std::endl;
        std::cout << "    • 1s 广播遥测 JSON" << std::endl;
        std::cout << "    • 命令处理 (cmd/echo)" << std::endl;
        std::cout << "    • Ping/Pong 心跳保活" << std::endl;
        std::cout << "========================================" << std::endl;

        // 启动遥测广播线程
        telemetry_thread_ = std::thread(&RobotWsServer::broadcast_loop, this);

        server_.run();
    }

    void stop() {
        server_.stop_listening();
        {
            std::lock_guard<std::mutex> lock(conn_mutex_);
            for (auto& hdl : connections_) {
                try { server_.close(hdl, websocketpp::close::status::going_away, "server shutdown"); }
                catch (...) {}
            }
        }
        if (telemetry_thread_.joinable()) telemetry_thread_.join();
    }

private:
    WsServer server_;
    std::set<connection_hdl, std::owner_less<connection_hdl>> connections_;
    std::mutex conn_mutex_;
    std::thread telemetry_thread_;
    RobotSimulator sim_;
    bool running_ = true;

    // ---- 连接/断开 ----
    void on_open(connection_hdl hdl) {
        std::lock_guard<std::mutex> lock(conn_mutex_);
        connections_.insert(hdl);
        auto conn = server_.get_con_from_hdl(hdl);
        std::cout << "[连接] 客户端 " << conn->get_remote_endpoint()
                  << " (共 " << connections_.size() << " 个连接)" << std::endl;

        // 发送欢迎消息
        server_.send(hdl, R"({"type":"welcome","msg":"已连接到机器人遥测服务"})",
                     websocketpp::frame::opcode::text);
    }

    void on_close(connection_hdl hdl) {
        std::lock_guard<std::mutex> lock(conn_mutex_);
        connections_.erase(hdl);
        auto conn = server_.get_con_from_hdl(hdl);
        std::cout << "[断开] 客户端 " << conn->get_remote_endpoint()
                  << " (剩余 " << connections_.size() << " 个连接)" << std::endl;
    }

    // ---- 消息处理 ----
    void on_message(connection_hdl hdl, WsServer::message_ptr msg) {
        std::string payload = msg->get_payload();
        auto conn = server_.get_con_from_hdl(hdl);

        std::cout << "[消息] " << conn->get_remote_endpoint()
                  << " → " << payload << std::endl;

        // 命令分发
        if (payload == "status") {
            // 返回即时机状态
            sim_.update();
            std::string json = sim_.to_json();
            json.insert(1, "\"type\":\"status\",");
            server_.send(hdl, json, websocketpp::frame::opcode::text);

        } else if (payload == "echo") {
            server_.send(hdl, R"({"type":"echo","msg":"WebSocket 双向通信正常"})",
                         websocketpp::frame::opcode::text);

        } else if (payload.substr(0, 4) == "cmd:") {
            std::string cmd = payload.substr(4);
            std::string rsp = "{\"type\":\"cmd_ack\",\"cmd\":\"" + cmd + "\",\"result\":\"ok\"}";
            server_.send(hdl, rsp, websocketpp::frame::opcode::text);

        } else {
            server_.send(hdl, R"({"type":"help","commands":["status","echo","cmd:<your-command>"]})",
                         websocketpp::frame::opcode::text);
        }
    }

    // ---- 定时广播 ----
    void broadcast_loop() {
        while (running_) {
            std::this_thread::sleep_for(std::chrono::seconds(1));

            sim_.update();
            std::string json = sim_.to_json();
            json.insert(1, "\"type\":\"telemetry\",");
            json.pop_back();  // remove trailing }
            json += ",\"connections\":" + std::to_string(connections_.size()) + "}";

            std::lock_guard<std::mutex> lock(conn_mutex_);
            for (auto& hdl : connections_) {
                try {
                    server_.send(hdl, json, websocketpp::frame::opcode::text);
                } catch (...) {
                    // 客户端已断开, 下次 broadcast 自动清理
                }
            }

            if (!connections_.empty()) {
                std::cout << "[广播] 遥测推送 → " << connections_.size()
                          << " 个客户端, battery=" << sim_.battery << "%"
                          << std::endl;
            }
        }
    }
};

// ============================================================================
// Main
// ============================================================================
RobotWsServer* g_server = nullptr;

void on_signal(int) {
    std::cout << "\n🛑 收到退出信号..." << std::endl;
    if (g_server) g_server->stop();
}

int main(int argc, char* argv[]) {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    uint16_t port = 9002;
    if (argc > 1) port = static_cast<uint16_t>(std::stoi(argv[1]));

    RobotWsServer srv;
    g_server = &srv;
    srv.run(port);

    return 0;
}
