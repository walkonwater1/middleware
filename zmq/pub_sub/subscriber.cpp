/**
 * ZeroMQ PUB/SUB 模式 — 订阅者 (C++)
 *
 * 订阅机器人传感器数据: 关节角度、IMU、电池电压
 * 支持前缀匹配订阅
 */

#include <zmq.hpp>
#include <iostream>
#include <iomanip>
#include <string>

/* ============================================================
 * 配置
 * ============================================================ */
constexpr const char *ENDPOINT = "tcp://localhost:5556";

/* 订阅主题 (前缀匹配)
 * ""       = 订阅所有
 * "robot/" = 订阅所有 robot/ 开头的
 * "robot/joints" = 只订阅关节角度
 */
constexpr const char *SUBSCRIBE_FILTER = "robot/";

/* ============================================================
 * 简易 JSON 数值提取 (不引入第三方库)
 * ============================================================ */
static double json_get_double(const std::string &json, const std::string &key)
{
    std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) return 0.0;
    pos += search.length();
    return std::stod(json.substr(pos));
}

/* ============================================================
 * 主逻辑
 * ============================================================ */
int main()
{
    zmq::context_t context(1);
    zmq::socket_t  socket(context, ZMQ_SUB);  /* SUB = 订阅端 */

    std::cout << "[SUB] 正在连接发布者: " << ENDPOINT << std::endl;
    socket.connect(ENDPOINT);

    /* 设置订阅过滤 (必须设置, 否则收不到消息) */
    socket.set(zmq::sockopt::subscribe, SUBSCRIBE_FILTER);
    std::cout << "[SUB] 订阅主题: '" << SUBSCRIBE_FILTER << "'" << std::endl;
    std::cout << "等待数据..." << std::endl << std::endl;

    try {
        while (true) {
            /* 接收 topic 帧 */
            zmq::message_t topic_msg;
            auto recv_topic = socket.recv(topic_msg);
            (void)recv_topic;
            std::string topic(static_cast<char *>(topic_msg.data()),
                              topic_msg.size());

            /* 接收 payload 帧 */
            zmq::message_t payload_msg;
            auto recv_payload = socket.recv(payload_msg);
            (void)recv_payload;
            std::string payload(static_cast<char *>(payload_msg.data()),
                                payload_msg.size());

            /* 按主题格式化输出 */
            if (topic.find("joints") != std::string::npos) {
                std::cout << "[SUB] 关节角度: ";
                /* 简易提取6个关节值 */
                for (int i = 1; i <= 6; ++i) {
                    double angle = json_get_double(payload, "joint_" + std::to_string(i));
                    std::cout << "J" << i << "=" << std::fixed
                              << std::setprecision(2) << angle << "° ";
                }
                std::cout << std::endl;

            } else if (topic.find("imu") != std::string::npos) {
                double ax = json_get_double(payload, "\"x\":");
                double ay = 0.0, az = 0.0;
                /* 更精确的提取 */
                auto pos_x = payload.find("\"x\":");
                if (pos_x != std::string::npos) {
                    ax = std::stod(payload.substr(pos_x + 4));
                }
                /* IMU 整体打印 */
                std::cout << "[SUB] IMU: " << payload << std::endl;

            } else if (topic.find("battery") != std::string::npos) {
                double voltage = json_get_double(payload, "voltage");
                double current = json_get_double(payload, "current");
                double soc     = json_get_double(payload, "soc");
                double temp    = json_get_double(payload, "temp");

                std::cout << "[SUB] 电池: " << std::fixed << std::setprecision(1)
                          << voltage << "V, "
                          << current << "A, SOC="
                          << soc << "%, " << temp << "°C" << std::endl;

            } else {
                std::cout << "[SUB] " << topic << ": " << payload << std::endl;
            }
        }
    }
    catch (const zmq::error_t &e) {
        std::cerr << "[ERR] ZMQ 异常: " << e.what() << std::endl;
    }

    socket.close();
    context.shutdown();
    return 0;
}
