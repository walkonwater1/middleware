/**
 * SOME/IP 车辆客户端 (vsomeip C++) — 订阅车辆数据 + 调用方法
 *
 * 功能:
 *   1. 通过 Service Discovery 自动发现服务
 *   2. 订阅车速/车门事件 (Event)
 *   3. 周期性调用车窗控制方法 (Method)
 *
 * 构建: cmake .. && make
 * 运行: VSOMEIP_CONFIGURATION=../vsomeip-client.json ./vehicle_client
 */

#include <vsomeip/vsomeip.hpp>

#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstring>

/* ============================================================
 * ID 常量 (必须与 service 一致)
 * ============================================================ */
constexpr vsomeip::service_id_t  SERVICE_ID     = 0x1234;
constexpr vsomeip::instance_id_t INSTANCE_ID    = 0x5678;
constexpr vsomeip::event_id_t    EVENT_SPEED    = 0x8001;
constexpr vsomeip::event_id_t    EVENT_DOOR     = 0x8002;
constexpr vsomeip::method_id_t   METHOD_WINDOW  = 0x0001;
constexpr vsomeip::eventgroup_id_t EVENTGROUP_VEHICLE = 0x0001;

/* ============================================================
 * 全局变量
 * ============================================================ */
static std::shared_ptr<vsomeip::application> g_app;
static std::atomic<bool> g_running{true};
static std::atomic<bool> g_service_available{false};
static std::atomic<int>  g_window_target{0};
static int g_event_count = 0;

/* ============================================================
 * 辅助: 反序列化
 * ============================================================ */
static double deserialize_speed(const std::shared_ptr<vsomeip::payload> &payload)
{
    if (!payload || payload->get_length() < sizeof(double)) return 0.0;
    double val;
    std::memcpy(&val, payload->get_data(), sizeof(double));
    return val;
}

static bool deserialize_door(const std::shared_ptr<vsomeip::payload> &payload)
{
    if (!payload || payload->get_length() < sizeof(bool)) return false;
    return payload->get_data()[0] != 0;
}

/* ============================================================
 * 车速事件回调
 * ============================================================ */
static void on_speed_event(const std::shared_ptr<vsomeip::message> &msg)
{
    double speed = deserialize_speed(msg->get_payload());
    g_event_count++;

    std::cout << "[EVENT] 车速: " << std::fixed << std::setprecision(1)
              << speed << " km/h";

    if (speed > 80.0) {
        std::cout << " ⚠ 超速!";
    }
    std::cout << std::endl;
}

/* ============================================================
 * 车门事件回调
 * ============================================================ */
static void on_door_event(const std::shared_ptr<vsomeip::message> &msg)
{
    bool door_open = deserialize_door(msg->get_payload());
    g_event_count++;

    std::cout << "[EVENT] 车门状态: " << (door_open ? "打开" : "关闭");

    if (door_open) {
        std::cout << " (停车开门)";
    }
    std::cout << std::endl;
}

/* ============================================================
 * 方法调用响应回调
 * ============================================================ */
static void on_window_response(const std::shared_ptr<vsomeip::message> &response)
{
    auto payload = response->get_payload();
    int position = 0;

    if (payload && payload->get_length() >= sizeof(int)) {
        std::memcpy(&position, payload->get_data(), sizeof(int));
    }

    std::cout << "[METHOD] 车窗控制响应: " << position << "%"
              << (response->get_return_code() == vsomeip::return_code_e::E_OK
                      ? " ✓" : " ✗")
              << std::endl;
}

/* ============================================================
 * 服务可用性回调
 * ============================================================ */
static void on_availability(vsomeip::service_id_t service,
                            vsomeip::instance_id_t instance,
                            bool available)
{
    (void)service;
    (void)instance;

    if (available) {
        std::cout << "[CLIENT] 服务已发现 (通过 Service Discovery)!" << std::endl;
        g_service_available = true;

        /* 订阅事件 */
        std::set<vsomeip::eventgroup_id_t> groups = {EVENTGROUP_VEHICLE};
        g_app->request_event(SERVICE_ID, INSTANCE_ID, EVENT_SPEED, groups,
                             vsomeip::event_type_e::ET_EVENT,
                             vsomeip::reliability_type_e::RT_UNRELIABLE);
        g_app->request_event(SERVICE_ID, INSTANCE_ID, EVENT_DOOR, groups,
                             vsomeip::event_type_e::ET_EVENT,
                             vsomeip::reliability_type_e::RT_UNRELIABLE);

        g_app->subscribe(SERVICE_ID, INSTANCE_ID, EVENTGROUP_VEHICLE);
        std::cout << "[CLIENT] 已订阅 Eventgroup 0x"
                  << std::hex << EVENTGROUP_VEHICLE << std::dec << std::endl;

    } else {
        std::cout << "[CLIENT] 服务已下线" << std::endl;
        g_service_available = false;
        g_app->unsubscribe(SERVICE_ID, INSTANCE_ID, EVENTGROUP_VEHICLE);
    }
}

/* ============================================================
 * 状态回调
 * ============================================================ */
static void on_state_registered(vsomeip::state_type_e state)
{
    if (state == vsomeip::state_type_e::ST_REGISTERED) {
        std::cout << "[CLIENT] 已注册到 SOME/IP 路由" << std::endl;

        /* 请求服务 */
        g_app->request_service(SERVICE_ID, INSTANCE_ID);
        std::cout << "[CLIENT] 正在通过 SD 发现 0x"
                  << std::hex << SERVICE_ID << ":" << INSTANCE_ID
                  << std::dec << " ..." << std::endl;
    }
}

/* ============================================================
 * 主逻辑
 * ============================================================ */
int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    /* 1. 运行时 & 应用 */
    auto runtime = vsomeip::runtime::get();
    g_app = runtime->create_application("vehicle_client");

    if (!g_app) {
        std::cerr << "[ERR] 创建应用失败" << std::endl;
        return 1;
    }

    /* 2. 初始化 */
    if (!g_app->init()) {
        std::cerr << "[ERR] 初始化失败" << std::endl;
        return 1;
    }

    /* 3. 注册回调 */
    g_app->register_state_handler(
        std::bind(&on_state_registered, std::placeholders::_1));

    g_app->register_availability_handler(SERVICE_ID, INSTANCE_ID,
        std::bind(&on_availability,
                  std::placeholders::_1,
                  std::placeholders::_2,
                  std::placeholders::_3));

    g_app->register_message_handler(SERVICE_ID, INSTANCE_ID, EVENT_SPEED,
        std::bind(&on_speed_event, std::placeholders::_1));

    g_app->register_message_handler(SERVICE_ID, INSTANCE_ID, EVENT_DOOR,
        std::bind(&on_door_event, std::placeholders::_1));

    /* 4. 启动 */
    g_app->start();
    std::cout << "[CLIENT] 客户端已启动" << std::endl;

    /* 5. 等待服务上线 */
    std::cout << "[CLIENT] 等待车载服务..." << std::endl;
    while (!g_service_available && g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    /* 6. 周期性调用车窗控制方法 */
    std::cout << "[CLIENT] 开始周期性调用车窗控制..." << std::endl;

    int call_seq = 0;
    while (g_running && g_service_available) {
        call_seq++;

        /* 交替控制车窗开关 */
        int target;
        if (call_seq % 4 == 1) target = 0;     /* 关窗 */
        else if (call_seq % 4 == 2) target = 50;  /* 半开 */
        else if (call_seq % 4 == 3) target = 100;  /* 全开 */
        else target = 25;                          /* 1/4开 */

        /* 构造方法调用请求 */
        auto request = vsomeip::runtime::get()->create_request();
        request->set_service(SERVICE_ID);
        request->set_instance(INSTANCE_ID);
        request->set_method(METHOD_WINDOW);

        /* 设置 payload: 车窗目标位置 */
        std::vector<vsomeip::byte_t> data(sizeof(int));
        std::memcpy(data.data(), &target, sizeof(int));
        auto payload = vsomeip::runtime::get()->create_payload();
        payload->set_data(data);
        request->set_payload(payload);

        /* 发送请求 */
        std::cout << "[METHOD] 车窗控制请求: " << target << "%" << std::endl;
        g_app->send(request);

        /* 也可以注册 callback 获取响应 */
        /* g_app->send(request, true); 需要先 register_async_handler */

        /* 读取响应 (通过 on_window_response 回调) */

        std::this_thread::sleep_for(std::chrono::seconds(4));
    }

    /* 7. 清理 */
    g_app->clear_all_handler();
    g_app->stop();
    std::cout << "[CLIENT] 客户端已停止 (收到 " << g_event_count
              << " 个事件)" << std::endl;

    return 0;
}
