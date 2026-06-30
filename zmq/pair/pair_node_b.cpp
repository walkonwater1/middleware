/**
 * ZeroMQ PAIR 模式 — Node B (C++)
 *
 * PAIR 独占对的另一端, 与 Node A 双向同步状态.
 *
 * 用法:
 *   终端1: ./pair_node_a
 *   终端2: ./pair_node_b
 */

#include <zmq.hpp>
#include <iostream>
#include <iomanip>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <sstream>
#include <cmath>

/* ============================================================
 * 配置
 * ============================================================ */
constexpr const char *ENDPOINT = "tcp://localhost:5559";

/* ============================================================
 * 信号
 * ============================================================ */
static std::atomic<bool> running{true};
void on_signal(int) { running = false; }

/* ============================================================
 * 状态数据 (精简版, 与 Node A 结构一致)
 * ============================================================ */
struct NodeState {
    std::string node_id;
    uint64_t    seq = 0;
    double      position_x = 0.0;
    double      position_y = 0.0;
    double      velocity   = 0.0;
    double      battery_soc = 100.0;
    std::string status = "INIT";

    std::string serialize() const {
        std::ostringstream oss;
        oss << "{"
            << R"("node":")" << node_id << "\""
            << R"(,"seq":)" << seq
            << R"(,"x":)" << std::fixed << std::setprecision(2) << position_x
            << R"(,"y":)" << position_y
            << R"(,"v":)" << velocity
            << R"(,"soc":)" << battery_soc
            << R"(,"status":")" << status << "\""
            << "}";
        return oss.str();
    }

    void move_one_step() {
        seq++;
        /* Node B 沿直线运动 (区别于 A 的圆周运动) */
        position_x += velocity * 0.1;
        position_y += velocity * 0.05;
        battery_soc -= 0.008;
        status = (battery_soc > 20.0) ? "RUNNING" : "LOW_BATTERY";
    }
};

/* ============================================================
 * 主逻辑
 * ============================================================ */
int main()
{
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    zmq::context_t context(1);
    zmq::socket_t  socket(context, ZMQ_PAIR);  /* PAIR = 独占对 */

    std::cout << "[PAIR-B] 正在连接 Node A: " << ENDPOINT << std::endl;
    socket.connect(ENDPOINT);
    std::cout << "[PAIR-B] 已连接, 开始双向同步... (Ctrl-C 退出)" << std::endl;
    std::cout << std::endl;

    NodeState state_b;
    state_b.node_id = "Node-B";
    state_b.position_x = 0.0;
    state_b.position_y = 0.0;
    state_b.velocity = 3.0;

    uint64_t recv_count = 0;

    try {
        while (running) {
            /* 接收 Node A 的状态 (阻塞等待) */
            zmq::message_t in_msg;
            auto recv_result = socket.recv(in_msg);
            if (!recv_result) continue;

            recv_count++;
            std::string in_str(static_cast<char *>(in_msg.data()),
                              in_msg.size());

            std::cout << "[← PAIR-A]: " << in_str << std::endl;

            /* 更新自身状态 */
            state_b.move_one_step();
            std::string reply = state_b.serialize();

            /* 发送自身状态到 Node A */
            zmq::message_t out_msg(reply.size());
            memcpy(out_msg.data(), reply.data(), reply.size());
            socket.send(out_msg, zmq::send_flags::none);

            std::cout << "[PAIR-B →] seq=" << std::setw(3) << state_b.seq
                      << " | pos=(" << state_b.position_x
                      << "," << state_b.position_y << ")"
                      << " | v=" << state_b.velocity
                      << " | soc=" << state_b.battery_soc << "%"
                      << " | " << state_b.status
                      << " | 收到 " << recv_count << " 帧"
                      << std::endl;
            std::cout << "---" << std::endl;

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    catch (const zmq::error_t &e) {
        std::cerr << "[ERR] ZMQ 异常: " << e.what() << std::endl;
    }

    socket.close();
    context.shutdown();
    std::cout << "\n[PAIR-B] 停止, 共收发 " << recv_count << " 轮" << std::endl;
    return 0;
}
