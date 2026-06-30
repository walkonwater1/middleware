/**
 * ZeroMQ PAIR 模式 — Node A (C++)
 *
 * PAIR 是 ZMQ 中最简单的模式: 独占的一对一双向通道.
 * 一个 PAIR socket 只能连接另一个 PAIR socket, 不能多对多.
 *
 * 适用场景:
 *   - 同一进程内两个线程间的精确通信 (inproc://)
 *   - 两个进程间的独占双向管道
 *   - 不需要路由/分发/负载均衡的简单场景
 *
 * 本 demo 模拟两个控制节点间的状态同步.
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
#include <random>

/* ============================================================
 * 配置
 * ============================================================ */
constexpr const char *ENDPOINT = "tcp://*:5559";

/* ============================================================
 * 信号
 * ============================================================ */
static std::atomic<bool> running{true};
void on_signal(int) { running = false; }

/* ============================================================
 * 状态数据
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
        position_x += velocity * 0.1 * cos(seq * 0.5);  /* 模拟圆周运动 */
        position_y += velocity * 0.1 * sin(seq * 0.5);
        battery_soc -= 0.01;
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

    socket.bind(ENDPOINT);
    std::cout << "[PAIR-A] 独占通道已创建, 绑定 " << ENDPOINT << std::endl;
    std::cout << "[PAIR-A] 等待 Node B 连接... (Ctrl-C 退出)" << std::endl;
    std::cout << std::endl;

    NodeState state_a;
    state_a.node_id = "Node-A";
    state_a.position_x = 10.0;
    state_a.position_y = 20.0;
    state_a.velocity = 5.0;

    NodeState state_b;  /* 从 Node B 接收的状态 */

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<double> dist(0.9, 1.1);

    try {
        while (running) {
            /* 更新自身状态 */
            state_a.move_one_step();

            /* 发送自身状态到 Node B */
            std::string msg = state_a.serialize();
            zmq::message_t out_msg(msg.size());
            memcpy(out_msg.data(), msg.data(), msg.size());
            socket.send(out_msg, zmq::send_flags::none);

            std::cout << "[PAIR-A →] seq=" << std::setw(3) << state_a.seq
                      << " | pos=(" << state_a.position_x
                      << "," << state_a.position_y << ")"
                      << " | v=" << state_a.velocity
                      << " | soc=" << state_a.battery_soc << "%"
                      << " | " << state_a.status
                      << std::endl;

            /* 接收 Node B 的状态 (带超时, 不阻塞) */
            zmq::message_t in_msg;
            auto recv_result = socket.recv(in_msg, zmq::recv_flags::dontwait);

            if (recv_result) {
                std::string in_str(static_cast<char *>(in_msg.data()),
                                  in_msg.size());
                std::cout << "               [← PAIR-B]: " << in_str << std::endl;

                /* 简单解析 Node B 的位置 (演示双向通信) */
                auto pos = in_str.find("\"x\":");
                if (pos != std::string::npos) {
                    state_b.position_x = std::stod(in_str.substr(pos + 4));
                }
                pos = in_str.find("\"y\":");
                if (pos != std::string::npos) {
                    state_b.position_y = std::stod(in_str.substr(pos + 4));
                }

                /* 计算两节点距离 */
                double dx = state_a.position_x - state_b.position_x;
                double dy = state_a.position_y - state_b.position_y;
                double distance = sqrt(dx * dx + dy * dy);
                std::cout << "               [INFO] 节点间距: "
                          << std::fixed << std::setprecision(2)
                          << distance << "m" << std::endl;
            } else {
                std::cout << "               [WAIT] Node B 暂无数据..."
                          << std::endl;
            }

            std::cout << "---" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
    catch (const zmq::error_t &e) {
        std::cerr << "[ERR] ZMQ 异常: " << e.what() << std::endl;
    }

    socket.close();
    context.shutdown();
    std::cout << "\n[PAIR-A] 停止, 共同步 " << state_a.seq << " 帧" << std::endl;
    return 0;
}
