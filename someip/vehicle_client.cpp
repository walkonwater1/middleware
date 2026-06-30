/**
 * SOME/IP 车辆客户端 (vsomeip C++) — 订阅事件 + 调用方法 + 访问 Field
 *
 * 功能:
 *   1. 通过 Service Discovery 自动发现服务
 *   2. 订阅 3 个事件: 车速/车门/空调温度 (Event + Field Notifier)
 *   3. 周期性调用车窗控制方法 (Method)
 *   4. 空调温度 Get/Set (Field)
 *
 * 构建: mkdir -p build && cd build && cmake .. && make
 * 运行: VSOMEIP_CONFIGURATION=../vsomeip-client.json ./vehicle_client
 */

#include "include/vehicle_someip.hpp"

#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>

using namespace vehicle;

/* ============================================================
 * 全局变量
 * ============================================================ */
static std::shared_ptr<vsomeip::application> g_app;
static std::atomic<bool> g_running{true};
static std::atomic<bool> g_service_available{false};
static int g_event_count = 0;

/* ============================================================
 * 信号处理
 * ============================================================ */
static void on_signal(int) { g_running = false; }

/* ============================================================
 * 车速事件
 * ============================================================ */
static void on_speed_event(const std::shared_ptr<vsomeip::message> &msg)
{
    double speed = deserialize_speed(msg->get_payload());
    g_event_count++;

    std::cout << "[EVENT] 车速: " << std::fixed << std::setprecision(1)
              << speed << " km/h";
    if (speed > 80.0) std::cout << " ⚠ 超速!";
    std::cout << std::endl;
}

/* ============================================================
 * 车门事件
 * ============================================================ */
static void on_door_event(const std::shared_ptr<vsomeip::message> &msg)
{
    bool door_open = deserialize_door(msg->get_payload());
    g_event_count++;

    std::cout << "[EVENT] 车门: " << (door_open ? "打开 (停车中)" : "关闭")
              << std::endl;
}

/* ============================================================
 * 空调温度变化事件 (Field Notifier)
 * ============================================================ */
static void on_climate_event(const std::shared_ptr<vsomeip::message> &msg)
{
    double temp = deserialize_climate_temp(msg->get_payload());
    g_event_count++;

    std::cout << "[FIELD] 空调温度更新: " << std::fixed
              << std::setprecision(1) << temp << "°C"
              << " (via Notifier)" << std::endl;
}

/* ============================================================
 * 车窗控制响应
 * ============================================================ */
static void on_window_response(const std::shared_ptr<vsomeip::message> &response)
{
    int position = deserialize_window(response->get_payload());

    std::cout << "[METHOD] 车窗控制 → " << position << "%"
              << (response->get_return_code() == vsomeip::return_code_e::E_OK
                      ? " ✓" : " ✗")
              << std::endl;
}

/* ============================================================
 * 空调 Get 响应
 * ============================================================ */
static void on_climate_get_response(const std::shared_ptr<vsomeip::message> &response)
{
    double temp = deserialize_climate_temp(response->get_payload());

    std::cout << "[FIELD] 空调 GET → " << std::fixed << std::setprecision(1)
              << temp << "°C"
              << (response->get_return_code() == vsomeip::return_code_e::E_OK
                      ? " ✓" : " ✗")
              << std::endl;
}

/* ============================================================
 * 空调 Set 响应
 * ============================================================ */
static void on_climate_set_response(const std::shared_ptr<vsomeip::message> &response)
{
    double temp = deserialize_climate_temp(response->get_payload());

    std::cout << "[FIELD] 空调 SET → " << std::fixed << std::setprecision(1)
              << temp << "°C"
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
        std::cout << "[CLIENT] ✓ 服务已发现 (Service Discovery)" << std::endl;
        g_service_available = true;

        /* 订阅事件 + Field Notifier */
        std::set<vsomeip::eventgroup_id_t> groups = {EVENTGROUP_VEHICLE};

        auto sub_ev = [&](vsomeip::event_id_t eid, vsomeip::event_type_e type) {
            g_app->request_event(SERVICE_ID, INSTANCE_ID, eid, groups,
                                 type, vsomeip::reliability_type_e::RT_UNRELIABLE);
        };

        sub_ev(EVENT_SPEED, vsomeip::event_type_e::ET_EVENT);
        sub_ev(EVENT_DOOR, vsomeip::event_type_e::ET_EVENT);
        sub_ev(EVENT_CLIMATE_TEMP, vsomeip::event_type_e::ET_EVENT);

        g_app->subscribe(SERVICE_ID, INSTANCE_ID, EVENTGROUP_VEHICLE);
        std::cout << "[CLIENT]   已订阅 3 个事件 (SPEED/DOOR/CLIMATE)" << std::endl;

    } else {
        std::cout << "[CLIENT] ✗ 服务已下线" << std::endl;
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
        std::cout << "[CLIENT] 已注册到路由, 正在发现服务..."
                  << std::endl;
        g_app->request_service(SERVICE_ID, INSTANCE_ID);
    }
}

/* ============================================================
 * 辅助: 创建并发送方法请求
 * ============================================================ */
static void send_method_request(vsomeip::method_id_t method,
                                const std::shared_ptr<vsomeip::payload> &payload)
{
    auto request = vsomeip::runtime::get()->create_request();
    request->set_service(SERVICE_ID);
    request->set_instance(INSTANCE_ID);
    request->set_method(method);
    request->set_payload(payload);
    g_app->send(request);
}

/* ============================================================
 * 主逻辑
 * ============================================================ */
int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

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

    /* 事件回调 */
    g_app->register_message_handler(SERVICE_ID, INSTANCE_ID, EVENT_SPEED,
        std::bind(&on_speed_event, std::placeholders::_1));
    g_app->register_message_handler(SERVICE_ID, INSTANCE_ID, EVENT_DOOR,
        std::bind(&on_door_event, std::placeholders::_1));
    g_app->register_message_handler(SERVICE_ID, INSTANCE_ID, EVENT_CLIMATE_TEMP,
        std::bind(&on_climate_event, std::placeholders::_1));

    /* Method 响应回调 */
    g_app->register_message_handler(SERVICE_ID, INSTANCE_ID, METHOD_WINDOW,
        std::bind(&on_window_response, std::placeholders::_1));
    g_app->register_message_handler(SERVICE_ID, INSTANCE_ID, METHOD_CLIMATE_GET,
        std::bind(&on_climate_get_response, std::placeholders::_1));
    g_app->register_message_handler(SERVICE_ID, INSTANCE_ID, METHOD_CLIMATE_SET,
        std::bind(&on_climate_set_response, std::placeholders::_1));

    /* 4. 启动 */
    g_app->start();
    std::cout << "[CLIENT] ═══ 车辆客户端已启动 ═══" << std::endl;
    print_ids();

    /* 5. 等待服务上线 */
    std::cout << "[CLIENT] 等待车载服务..." << std::endl;
    while (!g_service_available && g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    if (!g_service_available) {
        g_app->clear_all_handler();
        g_app->stop();
        return 0;
    }

    /* 等待初始事件到达 */
    std::cout << "[CLIENT] 等待初始数据..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));

    /* 6. 交互循环: 车窗控制 + 空调 Field 访问 */
    std::cout << "\n[CLIENT] 开始交互测试...\n" << std::endl;

    int call_seq = 0;
    while (g_running && g_service_available) {
        call_seq++;

        /* ---- 6a. 车窗控制 Method ---- */
        int target;
        if (call_seq % 4 == 1) target = 0;
        else if (call_seq % 4 == 2) target = 50;
        else if (call_seq % 4 == 3) target = 100;
        else target = 25;

        std::cout << "[METHOD] 车窗控制请求: " << target << "%" << std::endl;
        send_method_request(METHOD_WINDOW, serialize_window(target));

        std::this_thread::sleep_for(std::chrono::seconds(1));

        /* ---- 6b. 空调 Field Get ---- */
        std::cout << "[FIELD] 查询空调温度 (GET)..." << std::endl;
        send_method_request(METHOD_CLIMATE_GET,
            make_payload(std::vector<vsomeip::byte_t>(1, 0)));

        std::this_thread::sleep_for(std::chrono::seconds(1));

        /* ---- 6c. 空调 Field Set (每4轮改一次温度) ---- */
        if (call_seq % 4 == 0) {
            double new_temp = 22.0 + (call_seq % 5) * 2.0;  /* 22→24→26→28 */
            std::cout << "[FIELD] 设置空调温度 (SET): "
                      << new_temp << "°C" << std::endl;
            send_method_request(METHOD_CLIMATE_SET,
                serialize_climate_temp(new_temp));

            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    /* 7. 清理 */
    g_app->clear_all_handler();
    g_app->stop();
    std::cout << "\n[CLIENT] 客户端已停止 (共收到 " << g_event_count
              << " 个事件)" << std::endl;
    return 0;
}
