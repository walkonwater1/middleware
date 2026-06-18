/**
 * ZeroMQ REQ/REP 模式 — 客户端 (C++)
 *
 * 向诊断服务发送请求并接收响应
 */

#include <zmq.hpp>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <vector>

/* ============================================================
 * 配置
 * ============================================================ */
constexpr const char *ENDPOINT = "tcp://localhost:5555";

static const std::vector<std::string> TEST_COMPONENTS = {
    "ENGINE_001",
    "ENGINE_002",
    "BRAKE_001",
    "BATTERY_001",
    "UNKNOWN_XYZ",   /* 测试未知组件 */
};

/* ============================================================
 * 发送请求并接收响应 (同步)
 * ============================================================ */
static std::string call_service(zmq::socket_t &socket, const std::string &request)
{
    zmq::message_t req_msg(request.size());
    memcpy(req_msg.data(), request.data(), request.size());
    socket.send(req_msg, zmq::send_flags::none);

    zmq::message_t reply_msg;
    auto recv_result = socket.recv(reply_msg);
    (void)recv_result;

    return std::string(static_cast<char *>(reply_msg.data()), reply_msg.size());
}

/* ============================================================
 * 主逻辑
 * ============================================================ */
int main()
{
    zmq::context_t context(1);
    zmq::socket_t  socket(context, ZMQ_REQ);  /* REQ = 请求端 */

    std::cout << "[REQ] 正在连接诊断服务: " << ENDPOINT << std::endl;
    socket.connect(ENDPOINT);

    /* 先 ping 一下 */
    std::string ping_req = R"({"command":"diag_ping"})";
    std::cout << "[REQ] 发送: " << ping_req << std::endl;
    std::string ping_resp = call_service(socket, ping_req);
    std::cout << "[REP] 收到: " << ping_resp << std::endl << std::endl;

    /* 批量查询 */
    for (const auto &component : TEST_COMPONENTS) {
        auto ts = std::chrono::system_clock::now().time_since_epoch();
        auto ts_sec = std::chrono::duration_cast<std::chrono::seconds>(ts).count();

        std::string request = R"({"command":"diag_query","component":")" +
                              component +
                              R"(","timestamp":)" + std::to_string(ts_sec) + "}";

        std::cout << "[REQ] 查询组件: " << component << std::endl;
        std::cout << "     请求: " << request << std::endl;

        std::string response = call_service(socket, request);
        std::cout << "[REP] 结果: " << response << std::endl << std::endl;

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    socket.close();
    context.shutdown();
    return 0;
}
