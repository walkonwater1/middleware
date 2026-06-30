/**
 * ZeroMQ ROUTER/DEALER 模式 — 客户端 (C++)
 *
 * DEALER 是 REQ 的异步升级版:
 *   - REQ: send→recv→send→recv (严格交替, 发一收一)
 *   - DEALER: 可连续发送多个请求, 异步接收响应, 不阻塞
 *
 * 场景: 车辆诊断客户端 — 并发查询多个组件, 无需等待每个回复
 */

#include <zmq.hpp>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <sstream>
#include <random>

/* ============================================================
 * 配置
 * ============================================================ */
constexpr const char *ENDPOINT = "tcp://localhost:5558";

/* ============================================================
 * 信号
 * ============================================================ */
static std::atomic<bool> running{true};
void on_signal(int) { running = false; }

/* ============================================================
 * 查询组件列表
 * ============================================================ */
static const std::vector<std::string> COMPONENTS = {
    "ENGINE", "BRAKE", "BATTERY", "STEERING",
    "ENGINE", "BRAKE", "BATTERY",        /* 重复查询部分组件 */
};

/* ============================================================
 * 主逻辑
 * ============================================================ */
int main()
{
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    zmq::context_t context(1);
    zmq::socket_t  socket(context, ZMQ_DEALER);  /* DEALER = 经销商端 */

    /* DEALER 自动分配随机 identity */
    char identity_buf[32];
    snprintf(identity_buf, sizeof(identity_buf), "client-%04x",
             static_cast<unsigned>(rand() & 0xFFFF));
    socket.set(zmq::sockopt::routing_id, identity_buf);

    std::cout << "[DEALER:" << identity_buf << "] 正在连接诊断服务: "
              << ENDPOINT << std::endl;
    socket.connect(ENDPOINT);
    std::cout << "[DEALER:" << identity_buf << "] 异步模式 — 可连续发请求不等回复"
              << std::endl;
    std::cout << std::endl;

    /* 先 ping 一下确认连通 */
    {
        std::string ping = R"({"command":"ping"})";
        zmq::message_t ping_msg(ping.size());
        memcpy(ping_msg.data(), ping.data(), ping.size());
        socket.send(ping_msg, zmq::send_flags::none);
        std::cout << "[→] 发送: ping" << std::endl;

        zmq::message_t pong;
        auto recv_result = socket.recv(pong);
        (void)recv_result;
        std::string pong_str(static_cast<char *>(pong.data()), pong.size());
        std::cout << "[←] 响应: " << pong_str << std::endl << std::endl;

        if (pong_str.find("pong") == std::string::npos) {
            std::cerr << "[ERR] 服务未就绪!" << std::endl;
            return 1;
        }
    }

    /* 先查询组件列表 */
    {
        std::string list_cmd = R"({"command":"list_components"})";
        zmq::message_t list_msg(list_cmd.size());
        memcpy(list_msg.data(), list_cmd.data(), list_cmd.size());
        socket.send(list_msg, zmq::send_flags::none);

        zmq::message_t list_resp;
        auto recv_result = socket.recv(list_resp);
        (void)recv_result;
        std::string list_str(static_cast<char *>(list_resp.data()), list_resp.size());
        std::cout << "[←] 可用组件: " << list_str << std::endl << std::endl;
    }

    /* ============================================================
     * 异步批量查询: 连续发送所有请求, 再逐批接收响应
     *
     * 这正是 DEALER 优于 REQ 的关键:
     *   REQ 必须: send(component1) → recv → send(component2) → recv
     *   DEALER 可以: send(1) → send(2) → send(3) → ... → recv → recv → recv
     * ============================================================ */
    std::cout << "══════ 开始异步批量查询 (8个请求连续发送) ══════" << std::endl;
    uint64_t seq = 0;

    /* 批量发送 */
    for (auto &component : COMPONENTS) {
        seq++;
        std::ostringstream oss;
        oss << R"({"command":"diag_query","component":")"
            << component << R"(","seq":)" << seq << "}";
        std::string request = oss.str();

        zmq::message_t msg(request.size());
        memcpy(msg.data(), request.data(), request.size());
        socket.send(msg, zmq::send_flags::none);

        std::cout << "[→] 发送 #" << seq << ": " << component << std::endl;
    }

    std::cout << "\n全部请求已发送, 开始异步接收响应..." << std::endl << std::endl;

    /* 逐批接收 (顺序可能与发送顺序不同!) */
    int received = 0;
    auto start_time = std::chrono::steady_clock::now();

    while (received < static_cast<int>(COMPONENTS.size()) && running) {
        zmq::message_t reply_msg;
        auto recv_result = socket.recv(reply_msg, zmq::recv_flags::none);
        if (!recv_result) continue;

        received++;
        std::string reply(static_cast<char *>(reply_msg.data()),
                         reply_msg.size());

        auto now = std::chrono::steady_clock::now();
        double elapsed_ms = std::chrono::duration<double, std::milli>(
            now - start_time).count();

        std::cout << "[←] 响应 #" << received
                  << " (" << std::fixed << std::setprecision(1)
                  << elapsed_ms << "ms): "
                  << reply.substr(0, 100);
        if (reply.size() > 100) std::cout << "...";
        std::cout << std::endl;
    }

    auto end_time = std::chrono::steady_clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(
        end_time - start_time).count();

    std::cout << "\n══════ 异步查询完成 ══════" << std::endl;
    std::cout << "  发送: " << COMPONENTS.size() << " 个请求" << std::endl;
    std::cout << "  接收: " << received << " 个响应" << std::endl;
    std::cout << "  总耗时: " << std::fixed << std::setprecision(1)
              << total_ms << " ms" << std::endl;

    socket.close();
    context.shutdown();
    return 0;
}
