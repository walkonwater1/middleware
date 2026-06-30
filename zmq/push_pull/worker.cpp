/**
 * ZeroMQ PUSH/PULL 模式 — 工作节点 (C++)
 *
 * 从任务分发器接收传感器数据帧并模拟处理.
 * 可启动多个 worker 实例, ZMQ 自动以 round-robin 方式分发.
 *
 * 用法:
 *   ./push_pull_worker                    # 默认 worker
 *   ./push_pull_worker --id=worker-2       # 指定 worker ID
 */

#include <zmq.hpp>
#include <iostream>
#include <iomanip>
#include <string>
#include <thread>
#include <chrono>
#include <random>
#include <atomic>
#include <csignal>
#include <cstring>

/* ============================================================
 * 配置
 * ============================================================ */
constexpr const char *ENDPOINT = "tcp://localhost:5557";

/* ============================================================
 * 信号处理
 * ============================================================ */
static std::atomic<bool> running{true};
void on_signal(int) { running = false; }

/* ============================================================
 * 简易 JSON 值提取
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

static double json_get_number(const std::string &json, const std::string &key)
{
    std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) return 0;
    pos += search.length();
    size_t end;
    double val = std::stod(json.substr(pos), &end);
    return val;
}

/* ============================================================
 * 主逻辑
 * ============================================================ */
int main(int argc, char *argv[])
{
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    /* 解析 worker ID */
    std::string worker_id = "worker-1";
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg.rfind("--id=", 0) == 0) {
            worker_id = arg.substr(5);
        }
    }

    zmq::context_t context(1);
    zmq::socket_t  socket(context, ZMQ_PULL);  /* PULL = 拉取端 */

    std::cout << "[PULL:" << worker_id << "] 正在连接任务分发器: " << ENDPOINT << std::endl;
    socket.connect(ENDPOINT);
    std::cout << "[PULL:" << worker_id << "] 等待传感器数据帧... (Ctrl-C 退出)" << std::endl;
    std::cout << std::endl;

    /* 模拟处理耗时的随机分布 (ms): 不同传感器的处理耗时不同 */
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<double> processing_time_ms(20.0, 100.0);

    uint64_t processed = 0;
    uint64_t total_bytes = 0;
    double   total_proc_time_ms = 0;
    auto start_time = std::chrono::steady_clock::now();

    try {
        while (running) {
            zmq::message_t msg;
            auto recv_result = socket.recv(msg);
            (void)recv_result;

            std::string payload(static_cast<char *>(msg.data()), msg.size());
            processed++;

            /* 提取任务信息 */
            std::string label = json_get_string(payload, "label");
            std::string sensor = json_get_string(payload, "sensor_name");
            double data_rate = json_get_number(payload, "data_rate_mbps");
            uint64_t payload_bytes = static_cast<uint64_t>(
                json_get_number(payload, "payload_bytes"));
            total_bytes += payload_bytes;

            /* 模拟处理耗时 (按数据大小比例) */
            double proc_time = (payload_bytes / 1000000.0) * 10.0 + 10.0; /* ms */
            proc_time = std::max(10.0, std::min(proc_time, 100.0));
            total_proc_time_ms += proc_time;

            std::cout << std::fixed << std::setprecision(1)
                      << "[" << worker_id << "] #" << std::setw(4) << processed
                      << " | " << label
                      << " | " << sensor
                      << " | " << payload_bytes << " bytes"
                      << " | " << data_rate << " Mbps"
                      << " | 处理耗时: " << proc_time << "ms"
                      << std::endl;

            std::this_thread::sleep_for(
                std::chrono::milliseconds(static_cast<int>(proc_time)));
        }
    }
    catch (const zmq::error_t &e) {
        std::cerr << "[ERR] ZMQ 异常: " << e.what() << std::endl;
    }

    socket.close();
    context.shutdown();

    auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start_time).count();

    std::cout << "\n[PULL:" << worker_id << "] 停止.\n"
              << "  处理帧数: " << processed << "\n"
              << "  总数据量: " << (total_bytes / 1048576.0) << " MB\n"
              << "  总耗时: " << std::fixed << std::setprecision(1)
              << elapsed << "s\n"
              << "  吞吐量: " << (processed / elapsed) << " fps\n"
              << "  平均处理: " << (total_proc_time_ms / processed) << " ms/frame"
              << std::endl;
    return 0;
}
