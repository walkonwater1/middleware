/**
 * SOME/IP 车辆服务端 (vsomeip C++) — 提供车辆数据服务
 *
 * 模式:
 *   Event:  车速变更 (0x8001), 车门状态 (0x8002)
 *   Method: 车窗控制 (0x0001)
 *   Field:  空调温度 (Getter 0x0002 / Setter 0x0003 / Notifier 0x8003)
 *
 * 构建: mkdir -p build && cd build && cmake .. && make
 * 运行: VSOMEIP_CONFIGURATION=../vsomeip-service.json ./vehicle_service
 */

#include "include/vehicle_someip.hpp"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <thread>
#include <chrono>
#include <atomic>
#include <random>
#include <csignal>

using namespace vehicle;

/* ============================================================
 * 全局变量
 * ============================================================ */
static std::shared_ptr<vsomeip::application> g_app;
static std::atomic<bool> g_running{true};

static double       g_speed       = 0.0;
static bool         g_door_open   = false;
static int          g_window_pos  = 50;     /* 车窗开度 0~100 */
static ClimateSettings g_climate;           /* 空调设置 */
static std::mt19937 g_rng{std::random_device{}()};
static int          g_tick = 0;

/* ============================================================
 * 信号处理
 * ============================================================ */
static void on_signal(int) { g_running = false; }

/* ============================================================
 * 车窗控制方法
 * ============================================================ */
static void on_window_control(const std::shared_ptr<vsomeip::message> &request)
{
    int target_pos = deserialize_window(request->get_payload());

    if (target_pos < 0)  target_pos = 0;
    if (target_pos > 100) target_pos = 100;

    g_window_pos = target_pos;
    std::cout << "[SERVICE] 车窗控制: " << target_pos
              << "% (client=0x" << std::hex << request->get_client() << std::dec
              << ")" << std::endl;

    auto response = vsomeip::runtime::get()->create_response(request);
    response->set_payload(serialize_window(g_window_pos));
    response->set_message(vsomeip::message_type_e::MT_RESPONSE);
    response->set_return_code(vsomeip::return_code_e::E_OK);
    g_app->send(response);
}

/* ============================================================
 * 空调温度 Getter (Field Get)
 * ============================================================ */
static void on_climate_get(const std::shared_ptr<vsomeip::message> &request)
{
    std::cout << "[SERVICE] 空调温度查询 (client=0x"
              << std::hex << request->get_client() << std::dec
              << ") → " << g_climate.temperature << "°C" << std::endl;

    auto response = vsomeip::runtime::get()->create_response(request);
    response->set_payload(serialize_climate_temp(g_climate.temperature));
    response->set_message(vsomeip::message_type_e::MT_RESPONSE);
    response->set_return_code(vsomeip::return_code_e::E_OK);
    g_app->send(response);
}

/* ============================================================
 * 空调温度 Setter (Field Set)
 * ============================================================ */
static void on_climate_set(const std::shared_ptr<vsomeip::message> &request)
{
    double new_temp = deserialize_climate_temp(request->get_payload());

    /* 范围限制 */
    if (new_temp < 16.0) new_temp = 16.0;
    if (new_temp > 32.0) new_temp = 32.0;

    double old_temp = g_climate.temperature;
    g_climate.temperature = new_temp;

    std::cout << "[SERVICE] 空调温度设置: " << old_temp << "°C → "
              << new_temp << "°C (client=0x"
              << std::hex << request->get_client() << std::dec << ")"
              << std::endl;

    /* 设置成功后, 通过 Notifier 事件通知所有订阅者 */
    g_app->notify(SERVICE_ID, INSTANCE_ID, EVENT_CLIMATE_TEMP,
                  serialize_climate_temp(g_climate.temperature));
    std::cout << "[SERVICE]   已通知所有订阅者温度变更" << std::endl;

    /* 响应 Setter */
    auto response = vsomeip::runtime::get()->create_response(request);
    response->set_payload(serialize_climate_temp(g_climate.temperature));
    response->set_message(vsomeip::message_type_e::MT_RESPONSE);
    response->set_return_code(vsomeip::return_code_e::E_OK);
    g_app->send(response);
}

/* ============================================================
 * 状态回调
 * ============================================================ */
static void on_state_registered(vsomeip::state_type_e state)
{
    if (state == vsomeip::state_type_e::ST_REGISTERED) {
        std::cout << "[SERVICE] 已注册到 SOME/IP 路由" << std::endl;
    }
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
    g_app = runtime->create_application("vehicle_service");
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

    /* Method: 车窗控制 */
    g_app->register_message_handler(SERVICE_ID, INSTANCE_ID, METHOD_WINDOW,
        std::bind(&on_window_control, std::placeholders::_1));

    /* Field Get/Set: 空调温度 */
    g_app->register_message_handler(SERVICE_ID, INSTANCE_ID, METHOD_CLIMATE_GET,
        std::bind(&on_climate_get, std::placeholders::_1));
    g_app->register_message_handler(SERVICE_ID, INSTANCE_ID, METHOD_CLIMATE_SET,
        std::bind(&on_climate_set, std::placeholders::_1));

    /* 4. 提供服务 */
    g_app->offer_service(SERVICE_ID, INSTANCE_ID);
    std::cout << "[SERVICE] ═══ 车辆服务已提供 ═══" << std::endl;
    print_ids();

    /* 5. 注册事件 */
    std::set<vsomeip::eventgroup_id_t> groups = {EVENTGROUP_VEHICLE};

    auto offer_ev = [&](vsomeip::event_id_t eid, const char* name) {
        g_app->offer_event(SERVICE_ID, INSTANCE_ID, eid, groups,
                           vsomeip::event_type_e::ET_EVENT,
                           std::chrono::milliseconds::zero(),
                           false, true, nullptr,
                           vsomeip::reliability_type_e::RT_UNRELIABLE);
        std::cout << "[SERVICE]   事件 " << name
                  << " (0x" << std::hex << eid << std::dec << ")" << std::endl;
    };

    offer_ev(EVENT_SPEED, "SPEED");
    offer_ev(EVENT_DOOR, "DOOR");
    offer_ev(EVENT_CLIMATE_TEMP, "CLIMATE_TEMP (Field Notifier)");

    /* 6. 启动 */
    g_app->start();
    std::cout << "[SERVICE] 开始模拟车辆数据... (Ctrl-C 退出)" << std::endl;
    std::cout << std::endl;

    /* 7. 模拟循环 */
    std::uniform_real_distribution<double> speed_noise(-2.0, 2.0);
    std::uniform_real_distribution<double> temp_drift(-0.3, 0.3);

    while (g_running) {
        g_tick++;

        /* 车速模拟 */
        if (g_tick <= 10) {
            g_speed += 8.0;
        } else if (g_tick <= 20) {
            g_speed += speed_noise(g_rng);
        } else if (g_tick <= 25) {
            g_speed -= 16.0;
        } else {
            g_speed = 0.0;
            if (g_tick > 30) g_tick = 0;
        }
        if (g_speed < 0.0) g_speed = 0.0;
        if (g_speed > 120.0) g_speed = 120.0;

        /* 车门模拟 */
        if (g_speed == 0.0 && g_tick % 10 == 0) {
            g_door_open = !g_door_open;
        } else if (g_speed > 0.0) {
            g_door_open = false;
        }

        /* 空调温度漂移 */
        g_climate.temperature += temp_drift(g_rng);
        if (g_climate.temperature < 16.0) g_climate.temperature = 16.0;
        if (g_climate.temperature > 32.0) g_climate.temperature = 32.0;

        /* 通知事件 */
        g_app->notify(SERVICE_ID, INSTANCE_ID, EVENT_SPEED,
                      serialize_speed(g_speed));
        g_app->notify(SERVICE_ID, INSTANCE_ID, EVENT_DOOR,
                      serialize_door(g_door_open));
        g_app->notify(SERVICE_ID, INSTANCE_ID, EVENT_CLIMATE_TEMP,
                      serialize_climate_temp(g_climate.temperature));

        std::cout << std::fixed << std::setprecision(1)
                  << "[SERVICE] speed=" << g_speed << "km/h"
                  << " | door=" << (g_door_open ? "开" : "关")
                  << " | window=" << g_window_pos << "%"
                  << " | climate=" << g_climate.temperature << "°C"
                  << " (fan=" << g_climate.fan_speed
                  << " AC=" << (g_climate.ac_on ? "ON" : "OFF") << ")"
                  << std::endl;

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    /* 8. 清理 */
    g_app->stop_offer_service(SERVICE_ID, INSTANCE_ID);
    g_app->stop();
    std::cout << "\n[SERVICE] 服务已停止" << std::endl;
    return 0;
}
