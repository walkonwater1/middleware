/**
 * WebSocket Client — 机器人遥测监控终端
 *
 * 演示:
 *   1. 连接到 ws://server:port
 *   2. 发送文本命令 (status / echo / cmd:xxx)
 *   3. 接收服务端广播的遥测 JSON
 *   4. 自动处理 Ping/Pong
 */

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <signal.h>

#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>

using websocketpp::connection_hdl;
typedef websocketpp::client<websocketpp::config::asio_client> WsClient;
typedef websocketpp::config::asio_client::message_type::ptr message_ptr;

std::atomic<bool> g_running{true};

void on_signal(int) {
    g_running = false;
}

// ============================================================================
// 辅助函数: JSON 美化打印
// ============================================================================
void pretty_print(const std::string& json) {
    // 简单缩进
    int indent = 0;
    bool in_string = false;
    for (size_t i = 0; i < json.size(); i++) {
        char c = json[i];
        if (c == '"' && (i == 0 || json[i-1] != '\\')) in_string = !in_string;
        if (in_string) { std::cout << c; continue; }
        if (c == '{' || c == '[') {
            std::cout << c << "\n" << std::string(++indent * 2, ' ');
        } else if (c == '}' || c == ']') {
            std::cout << "\n" << std::string(--indent * 2, ' ') << c;
        } else if (c == ',') {
            std::cout << c << "\n" << std::string(indent * 2, ' ');
        } else if (c == ':') {
            std::cout << c << " ";
        } else {
            std::cout << c;
        }
    }
    std::cout << std::endl;
}

// ============================================================================
// 遥测数据解析
// ============================================================================
void parse_telemetry(const std::string& json) {
    // 简单提取关键字段 (不引入完整 JSON 解析库)
    auto find_val = [&](const std::string& key) -> std::string {
        size_t pos = json.find("\"" + key + "\":");
        if (pos == std::string::npos) return "?";
        pos += key.size() + 3;
        size_t end = json.find_first_of(",}", pos);
        return json.substr(pos, end - pos);
    };

    std::cout << "  📡 电池: " << find_val("battery") << "%"
              << " | CPU: " << find_val("cpu") << "%"
              << " | 内存: " << find_val("mem") << "%"
              << " | 连接数: " << find_val("connections")
              << " | 时间: " << find_val("timestamp") << std::endl;
}

// ============================================================================
// 客户端主逻辑
// ============================================================================
class RobotWsClient {
public:
    RobotWsClient() {
        client_.init_asio();
        // 注意: ws:// 明文连接无需 TLS handler
        // 若需要 wss:// 加密, 使用 asio_client (非 no_tls) + set_tls_init_handler

        client_.set_open_handler([this](connection_hdl hdl) {
            std::cout << "✅ 已连接到服务器" << std::endl;
            conn_ = hdl;
            connected_ = true;
        });

        client_.set_close_handler([this](connection_hdl) {
            std::cout << "🔌 连接已断开" << std::endl;
            connected_ = false;
            g_running = false;
        });

        client_.set_fail_handler([this](connection_hdl) {
            std::cerr << "❌ 连接失败" << std::endl;
            g_running = false;
        });

        client_.set_message_handler([this](connection_hdl, message_ptr msg) {
            std::string payload = msg->get_payload();
            std::cout << "\n📩 收到消息 "
                      << (msg->get_opcode() == websocketpp::frame::opcode::text ? "[text]" : "[binary]")
                      << " (" << payload.size() << " bytes):" << std::endl;

            // 根据消息类型处理
            if (payload.find("\"type\":\"welcome\"") != std::string::npos) {
                std::cout << "  👋 服务端欢迎消息" << std::endl;
                pretty_print(payload);
            } else if (payload.find("\"type\":\"telemetry\"") != std::string::npos) {
                parse_telemetry(payload);
            } else {
                pretty_print(payload);
            }
        });
    }

    bool connect(const std::string& uri) {
        websocketpp::lib::error_code ec;
        auto con = client_.get_connection(uri, ec);
        if (ec) {
            std::cerr << "连接创建失败: " << ec.message() << std::endl;
            return false;
        }
        client_.connect(con);
        return true;
    }

    void send(const std::string& text) {
        if (!connected_) return;
        websocketpp::lib::error_code ec;
        client_.send(conn_, text, websocketpp::frame::opcode::text, ec);
        if (!ec) {
            std::cout << "📤 发送: " << text << std::endl;
        }
    }

    void run() {
        std::thread([this]() {
            try { client_.run(); }
            catch (const std::exception& e) {
                std::cerr << "客户端异常: " << e.what() << std::endl;
            }
        }).detach();
    }

    bool is_connected() const { return connected_; }

private:
    WsClient client_;
    connection_hdl conn_;
    std::atomic<bool> connected_{false};
};

// ============================================================================
// Main
// ============================================================================
int main(int argc, char* argv[]) {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    std::string uri = "ws://localhost:9002";
    if (argc > 1) uri = argv[1];

    std::cout << "========================================" << std::endl;
    std::cout << "  WebSocket 机器人遥测客户端" << std::endl;
    std::cout << "  目标: " << uri << std::endl;
    std::cout << "========================================" << std::endl;

    RobotWsClient client;
    if (!client.connect(uri)) return 1;

    client.run();

    // 等待连接建立
    int wait = 0;
    while (!client.is_connected() && wait < 50) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        wait++;
    }
    if (!client.is_connected()) {
        std::cerr << "连接超时" << std::endl;
        return 1;
    }

    // 自动演示序列
    std::cout << "\n--- 自动演示: 发送命令 ---" << std::endl;

    // 1. 查询状态
    std::this_thread::sleep_for(std::chrono::seconds(1));
    client.send("status");

    // 2. Echo 测试
    std::this_thread::sleep_for(std::chrono::seconds(2));
    client.send("echo");

    // 3. 命令测试
    std::this_thread::sleep_for(std::chrono::seconds(2));
    client.send("cmd:self_check");

    // 4. 等待接收更多遥测广播
    std::cout << "\n--- 等待遥测广播 (Ctrl-C 退出) ---" << std::endl;
    int tick = 0;
    while (g_running && tick < 15) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        tick++;
    }

    std::cout << "\n✅ 演示完成, 断开连接" << std::endl;
    return 0;
}
