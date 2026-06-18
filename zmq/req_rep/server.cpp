/**
 * ZeroMQ REQ/REP 模式 — 服务端 (C++)
 *
 * 依赖: libzmq + cppzmq (header-only)
 * 安装: sudo apt install libzmq3-dev
 *       从 https://github.com/zeromq/cppzmq 获取 zmq.hpp 放入 /usr/local/include
 *
 * 模拟车辆诊断服务: 接收诊断请求, 返回诊断结果
 */

#include <zmq.hpp>
#include <iostream>
#include <string>
#include <map>
#include <chrono>
#include <thread>
#include <sstream>

/* ============================================================
 * 配置
 * ============================================================ */
constexpr const char *ENDPOINT = "tcp://*:5555";

/* 模拟诊断数据库 */
static const std::map<std::string, std::string> DIAG_DB = {
    {"ENGINE_001",  R"({"status":"OK","temp":85.2,"rpm":2200})"},
    {"ENGINE_002",  R"({"status":"WARN","temp":108.5,"rpm":4500})"},
    {"BRAKE_001",   R"({"status":"OK","pad_thickness":8.5})"},
    {"BATTERY_001", R"({"status":"OK","voltage":12.6,"soc":92})"},
};

/* ============================================================
 * 简易 JSON 解析 (不引入第三方库, 仅演示用)
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

/* ============================================================
 * 处理诊断请求
 * ============================================================ */
static std::string handle_request(const std::string &request)
{
    std::string cmd       = json_get_string(request, "command");
    std::string component = json_get_string(request, "component");

    if (cmd == "diag_ping") {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        auto ts  = std::chrono::duration_cast<std::chrono::seconds>(now).count();
        return R"({"status":"OK","message":"pong","timestamp":)" +
               std::to_string(ts) + "}";
    }

    if (cmd == "diag_query") {
        auto it = DIAG_DB.find(component);
        if (it != DIAG_DB.end()) {
            return R"({"status":"OK","component":")" + component +
                   R"(","data":)" + it->second + "}";
        } else {
            return R"({"status":"ERR","message":"Unknown component: )" +
                   component + R"("})";
        }
    }

    return R"({"status":"ERR","message":"Unknown command: )" + cmd + R"("})";
}

/* ============================================================
 * 主逻辑
 * ============================================================ */
int main()
{
    zmq::context_t context(1);
    zmq::socket_t  socket(context, ZMQ_REP);  /* REP = 回复端 */

    socket.bind(ENDPOINT);
    std::cout << "[REP] 诊断服务已启动, 监听 " << ENDPOINT << std::endl;
    std::cout << "等待诊断请求..." << std::endl << std::endl;

    try {
        while (true) {
            /* 阻塞等待请求 */
            zmq::message_t request_msg;
            auto recv_result = socket.recv(request_msg);
            (void)recv_result;

            std::string request_str(static_cast<char *>(request_msg.data()),
                                    request_msg.size());
            std::cout << "[REQ] 收到请求: " << request_str << std::endl;

            /* 处理 */
            std::string response = handle_request(request_str);

            /* 返回 */
            zmq::message_t reply(response.size());
            memcpy(reply.data(), response.data(), response.size());
            socket.send(reply, zmq::send_flags::none);

            std::cout << "[REP] 发送响应: " << response << std::endl << std::endl;
        }
    }
    catch (const zmq::error_t &e) {
        std::cerr << "[ERR] ZMQ 异常: " << e.what() << std::endl;
    }

    socket.close();
    context.shutdown();
    return 0;
}
