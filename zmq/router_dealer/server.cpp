/**
 * ZeroMQ ROUTER/DEALER 模式 — 服务端 (C++)
 *
 * ROUTER 是异步请求-响应模式的服务端:
 *   - 自动为每条接收的消息添加客户端 identity 帧
 *   - 可同时服务多个客户端, 无 REQ/REP 的严格锁步限制
 *   - 回复时需手动将 identity 帧原样返回
 *
 * 场景: 车辆诊断服务升级版 — 支持多客户端并发诊断, 无顺序限制
 *
 * 与 REQ/REP 对比:
 *   REQ/REP    : send→recv→send→recv (严格交替, 单客户端阻塞)
 *   ROUTER/DEALER: 异步自由收发, 多客户端并发, 无锁步
 */

#include <zmq.hpp>
#include <iostream>
#include <iomanip>
#include <string>
#include <map>
#include <chrono>
#include <thread>
#include <atomic>
#include <csignal>
#include <sstream>
#include <vector>

/* ============================================================
 * 配置
 * ============================================================ */
constexpr const char *ENDPOINT = "tcp://*:5558";

/* ============================================================
 * 信号
 * ============================================================ */
static std::atomic<bool> running{true};
void on_signal(int) { running = false; }

/* ============================================================
 * 模拟车辆诊断数据库
 * ============================================================ */
struct DiagRecord {
    std::string status;
    double      value;
    std::string unit;
};

static const std::map<std::string, std::vector<DiagRecord>> VEHICLE_DB = {
    {"ENGINE",   {{"OK",85.2,"°C"}, {"OK",2200,"rpm"}, {"OK",98.5,"kPa"}}},
    {"BRAKE",    {{"OK",8.5,"mm"},  {"WARN",65.0,"%"}}},
    {"BATTERY",  {{"OK",12.6,"V"},  {"OK",92.0,"%"},  {"OK",35.0,"°C"}}},
    {"STEERING", {{"OK",0.5,"°"},   {"WARN",-2.1,"°"}}},
};

/* ============================================================
 * 简易 JSON 工具
 * ============================================================ */
static std::string json_get_string(const std::string &json, const std::string &key)
{
    std::string search = "\"" + key + "\":\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos += search.length();
    auto end = json.find('"', pos);
    if (end == std::string::npos) return "";
    return json.substr(pos, end - pos);
}

static std::string build_response(const std::string &component)
{
    auto it = VEHICLE_DB.find(component);
    if (it == VEHICLE_DB.end()) {
        return R"({"status":"ERR","message":"unknown_component"})";
    }

    std::ostringstream oss;
    oss << R"({"status":"OK","component":")" << component
        << R"(","readings":[)";
    for (size_t i = 0; i < it->second.size(); i++) {
        auto &r = it->second[i];
        oss << R"({"status":")" << r.status
            << R"(","value":)" << r.value
            << R"(,"unit":")" << r.unit << R"("})";
        if (i + 1 < it->second.size()) oss << ",";
    }
    oss << "]}";
    return oss.str();
}

/* ============================================================
 * 客户端连接跟踪
 * ============================================================ */
struct ClientInfo {
    std::string identity;
    uint64_t    requests_handled = 0;
    uint64_t    last_seen_us = 0;
};

/* ============================================================
 * 主逻辑
 * ============================================================ */
int main()
{
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    zmq::context_t context(1);
    zmq::socket_t  socket(context, ZMQ_ROUTER);  /* ROUTER = 路由端 */

    socket.bind(ENDPOINT);
    std::cout << "[ROUTER] 异步诊断服务已启动, 绑定 " << ENDPOINT << std::endl;
    std::cout << "[ROUTER] 支持多客户端并发诊断 (Ctrl-C 退出)..." << std::endl;
    std::cout << "可诊断组件:";
    for (auto &kv : VEHICLE_DB) std::cout << " " << kv.first;
    std::cout << std::endl << std::endl;

    std::map<std::string, ClientInfo> clients;
    uint64_t total_requests = 0;

    try {
        while (running) {
            /* ROUTER recv 返回多帧: [identity][empty][data]
             * identity 帧由 ZMQ 自动添加 */
            zmq::message_t identity_msg;
            auto recv_id = socket.recv(identity_msg);
            (void)recv_id;
            std::string identity(static_cast<char *>(identity_msg.data()),
                                identity_msg.size());

            /* 接收空帧 (分隔符, 如果 client 是 DEALER 则可能有) */
            zmq::message_t request_msg;
            auto recv_req = socket.recv(request_msg);
            (void)recv_req;
            std::string request(static_cast<char *>(request_msg.data()),
                               request_msg.size());

            /* 跳过可能的空帧 (DEALER 可能发送空帧作为分隔) */
            if (request.empty()) {
                auto recv_req2 = socket.recv(request_msg);
                request = std::string(static_cast<char *>(request_msg.data()),
                                     request_msg.size());
            }

            /* 更新客户端记录 */
            auto &client = clients[identity];
            client.identity = identity;
            client.requests_handled++;
            total_requests++;

            using namespace std::chrono;
            client.last_seen_us = duration_cast<microseconds>(
                system_clock::now().time_since_epoch()).count();

            /* 解析请求 */
            std::string cmd = json_get_string(request, "command");
            std::string component = json_get_string(request, "component");

            std::cout << "[ROUTER] 客户端 " << identity.substr(0, 8) << "..."
                      << " 请求 #" << client.requests_handled
                      << " | 命令: " << cmd
                      << " | 组件: " << component
                      << std::endl;

            /* 处理并生成响应 */
            std::string response;
            if (cmd == "diag_query") {
                response = build_response(component);
            } else if (cmd == "ping") {
                auto ts = duration_cast<seconds>(
                    system_clock::now().time_since_epoch()).count();
                response = R"({"status":"OK","pong":true,"ts":)"
                           + std::to_string(ts) + "}";
            } else if (cmd == "list_components") {
                std::ostringstream oss;
                oss << R"({"status":"OK","components":[)";
                bool first = true;
                for (auto &kv : VEHICLE_DB) {
                    if (!first) oss << ",";
                    oss << "\"" << kv.first << "\"";
                    first = false;
                }
                oss << "]}";
                response = oss.str();
            } else {
                response = R"({"status":"ERR","message":"unknown_command"})";
            }

            /* ROUTER 回复: 必须先发送 identity 帧 */
            zmq::message_t id_reply(identity.size());
            memcpy(id_reply.data(), identity.data(), identity.size());
            socket.send(id_reply, zmq::send_flags::sndmore);

            zmq::message_t reply_msg(response.size());
            memcpy(reply_msg.data(), response.data(), response.size());
            socket.send(reply_msg, zmq::send_flags::none);

            std::cout << "[ROUTER] → 响应: " << response.substr(0, 80);
            if (response.size() > 80) std::cout << "...";
            std::cout << std::endl;

            /* 每秒打印统计 */
            static auto last_print = std::chrono::steady_clock::now();
            static uint64_t last_count = 0;
            auto now = std::chrono::steady_clock::now();
            double secs = duration<double>(now - last_print).count();
            if (secs >= 3.0) {
                double rate = (total_requests - last_count) / secs;
                std::cout << "────────────────────────────────────────────" << std::endl;
                std::cout << "[STATS] 活跃客户端: " << clients.size()
                          << " | 总请求: " << total_requests
                          << " | 速率: " << std::fixed << std::setprecision(1)
                          << rate << " req/s" << std::endl;
                for (auto &kv : clients) {
                    std::cout << "  " << kv.second.identity.substr(0, 12)
                              << " | 请求数: " << kv.second.requests_handled
                              << std::endl;
                }
                std::cout << "────────────────────────────────────────────" << std::endl;
                last_print = now;
                last_count = total_requests;
            }
        }
    }
    catch (const zmq::error_t &e) {
        std::cerr << "[ERR] ZMQ 异常: " << e.what() << std::endl;
    }

    socket.close();
    context.shutdown();
    std::cout << "\n[ROUTER] 停止, 共处理 " << total_requests << " 个请求"
              << ", 服务过 " << clients.size() << " 个客户端" << std::endl;
    return 0;
}
