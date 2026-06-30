/**
 * ZeroMQ PUSH/PULL 模式 — 生产者 (C++)
 *
 * 场景: 传感器数据采集后分发给多个 Worker 并行处理
 * ZMQ 自动以 round-robin 方式将任务均匀分发给连接的 PULL 端
 *
 * 支持启动多个 worker 实例, ZMQ 自动负载均衡.
 *
 * 编译: 依赖 libzmq + cppzmq (header-only)
 * 安装: sudo apt install libzmq3-dev
 *       cppzmq: https://github.com/zeromq/cppzmq
 */

#include <zmq.hpp>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>
#include <random>
#include <atomic>
#include <csignal>

/* ============================================================
 * 配置
 * ============================================================ */
constexpr const char *ENDPOINT = "tcp://*:5557";

/* ============================================================
 * 信号处理
 * ============================================================ */
static std::atomic<bool> running{true};
void on_signal(int) { running = false; }

/* ============================================================
 * 任务结构 (模拟传感器数据帧)
 * ============================================================ */
struct SensorTask {
    uint64_t frame_id;
    uint64_t timestamp_us;
    int      sensor_type;    // 1=LiDAR, 2=Camera, 3=Radar, 4=Ultrasonic
    double   data_rate_mbps;
    uint32_t payload_bytes;
    char     label[32];
};

static const char* sensor_type_name(int t) {
    switch (t) {
        case 1: return "LiDAR";
        case 2: return "Camera";
        case 3: return "Radar";
        case 4: return "Ultrasonic";
        default: return "UNKNOWN";
    }
}

/* ============================================================
 * 数据生成器
 * ============================================================ */
class TaskGenerator {
public:
    SensorTask generate(uint64_t frame_id) {
        SensorTask t;
        t.frame_id    = frame_id;
        t.sensor_type = (frame_id % 4) + 1;  /* 轮流生成 4 种传感器 */

        using namespace std::chrono;
        t.timestamp_us = duration_cast<microseconds>(
            system_clock::now().time_since_epoch()).count();

        /* 模拟不同传感器的数据特征 */
        switch (t.sensor_type) {
            case 1: /* LiDAR */
                t.data_rate_mbps = 70.0 + dist_(gen_) * 10.0;
                t.payload_bytes  = 1310720;  /* ~1.3MB per frame */
                break;
            case 2: /* Camera */
                t.data_rate_mbps = 200.0 + dist_(gen_) * 50.0;
                t.payload_bytes  = 8294400;  /* ~8MB per frame */
                break;
            case 3: /* Radar */
                t.data_rate_mbps = 1.0 + dist_(gen_) * 0.5;
                t.payload_bytes  = 8192;
                break;
            case 4: /* Ultrasonic */
                t.data_rate_mbps = 0.01 + dist_(gen_) * 0.005;
                t.payload_bytes  = 64;
                break;
        }

        snprintf(t.label, sizeof(t.label), "%s-F%06lu",
                 sensor_type_name(t.sensor_type), frame_id);
        return t;
    }

private:
    std::random_device rd_;
    std::mt19937 gen_{rd_()};
    std::uniform_real_distribution<double> dist_{-1.0, 1.0};
};

/* ============================================================
 * 序列化 (简易二进制格式)
 * ============================================================ */
static std::string serialize(const SensorTask &t) {
    std::ostringstream oss;
    oss << "{"
        << "\"frame_id\":" << t.frame_id
        << ",\"timestamp_us\":" << t.timestamp_us
        << ",\"sensor_type\":" << t.sensor_type
        << ",\"sensor_name\":\"" << sensor_type_name(t.sensor_type) << "\""
        << ",\"data_rate_mbps\":" << std::fixed << std::setprecision(2) << t.data_rate_mbps
        << ",\"payload_bytes\":" << t.payload_bytes
        << ",\"label\":\"" << t.label << "\""
        << "}";
    return oss.str();
}

/* ============================================================
 * 主逻辑
 * ============================================================ */
int main()
{
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    zmq::context_t context(1);
    zmq::socket_t  socket(context, ZMQ_PUSH);  /* PUSH = 推送端 */

    socket.bind(ENDPOINT);
    std::cout << "[PUSH] 任务分发器已启动, 绑定 " << ENDPOINT << std::endl;
    std::cout << "[PUSH] 分发策略: ZMQ round-robin 自动负载均衡" << std::endl;
    std::cout << "[PUSH] 可启动多个 worker 实例并行处理 (Ctrl-C 退出)..." << std::endl;
    std::cout << std::endl;

    /* 等待 worker 连接 */
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    TaskGenerator gen;
    uint64_t frame_id = 0;
    uint64_t total_sent = 0;
    auto start_time = std::chrono::steady_clock::now();

    try {
        while (running) {
            frame_id++;
            SensorTask task = gen.generate(frame_id);
            std::string payload = serialize(task);

            zmq::message_t msg(payload.size());
            memcpy(msg.data(), payload.data(), payload.size());
            socket.send(msg, zmq::send_flags::none);

            total_sent++;

            /* 每秒打印统计 */
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration<double>(now - start_time).count();
            if (elapsed >= 1.0) {
                double rate = total_sent / elapsed;
                std::cout << std::fixed << std::setprecision(1)
                          << "[PUSH] 已分发 " << total_sent << " 个任务"
                          << " | 速率: " << rate << " tasks/s"
                          << " | 最新: " << task.label
                          << " (" << task.data_rate_mbps << " Mbps)"
                          << std::endl;
                start_time = now;
                total_sent = 0;
            }

            /* 模拟传感器采集频率: ~20Hz (50ms per frame) */
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    catch (const zmq::error_t &e) {
        std::cerr << "[ERR] ZMQ 异常: " << e.what() << std::endl;
    }

    socket.close();
    context.shutdown();

    std::cout << "\n[PUSH] 停止, 共发送 " << frame_id << " 帧" << std::endl;
    return 0;
}
