/**
 * SOME/IP 车辆服务端 (vsomeip C++) — 提供车辆数据服务
 *
 * 服务 ID:     0x1234
 * 实例 ID:     0x5678
 * 事件 ID:     0x8001 (车速变更)
 *              0x8002 (车门状态变更)
 * 方法 ID:     0x0001 (控制车窗)
 *
 * 构建: cmake .. && make
 * 运行: VSOMEIP_CONFIGURATION=../vsomeip-service.json ./vehicle_service
 */

#include <vsomeip/vsomeip.hpp>

#include <iostream>
#include <iomanip>
#include <sstream>
#include <thread>
#include <chrono>
#include <atomic>
#include <random>
#include <cstring>

/* ============================================================
 * ID 常量定义 (与 .json 配置文件匹配)
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

static double       g_speed       = 0.0;
static bool         g_door_open   = false;
static int          g_window_pos  = 50;   /* 车窗开度 0~100 */
static std::mt19937 g_rng{std::random_device{}()};

/* ============================================================
 * 辅助函数: 序列化
 * ============================================================ */
static std::shared_ptr<vsomeip::payload>
create_speed_payload(double speed)
{
    auto payload = vsomeip::runtime::get()->create_payload();
    std::vector<vsomeip::byte_t> data(sizeof(double));
    std::memcpy(data.data(), &speed, sizeof(double));
    payload->set_data(data);
    return payload;
}

static std::shared_ptr<vsomeip::payload>
create_door_payload(bool door_open)
{
    auto payload = vsomeip::runtime::get()->create_payload();
    std::vector<vsomeip::byte_t> data(sizeof(bool));
    data[0] = door_open ? 1 : 0;
    payload->set_data(data);
    return payload;
}

static std::shared_ptr<vsomeip::payload>
create_window_payload(int position)
{
    auto payload = vsomeip::runtime::get()->create_payload();
    std::vector<vsomeip::byte_t> data(sizeof(int));
    std::memcpy(data.data(), &position, sizeof(int));
    payload->set_data(data);
    return payload;
}

/* ============================================================
 * 方法调用处理: 控制车窗
 * ============================================================ */
static void on_window_control(const std::shared_ptr<vsomeip::message> &request)
{
    /* 解析请求: 期望的窗位置 (0=关, 100=全开) */
    auto payload = request->get_payload();
    int target_pos = g_window_pos;

    if (payload && payload->get_length() >= sizeof(int)) {
        std::memcpy(&target_pos, payload->get_data(), sizeof(int));
    }

    /* 范围限制 */
    if (target_pos < 0)  target_pos = 0;
    if (target_pos > 100) target_pos = 100;

    g_window_pos = target_pos;
    std::cout << "[SERVICE] 车窗控制请求: " << target_pos
              << "% (来自: " << std::hex << request->get_client() << std::dec
              << ")" << std::endl;

    /* 构造响应 */
    auto response = vsomeip::runtime::get()->create_response(request);
    auto resp_payload = create_window_payload(g_window_pos);
    response->set_payload(resp_payload);
    response->set_message(vsomeip::message_type_e::MT_RESPONSE);
    response->set_return_code(vsomeip::return_code_e::E_OK);

    g_app->send(response);
    std::cout << "[SERVICE] 车窗控制完成: " << g_window_pos << "%" << std::endl;
}

/* ============================================================
 * 状态变更注册处理
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

    /* 1. 获取运行时 & 创建应用 */
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

    /* 注册车窗控制方法处理器 */
    g_app->register_message_handler(
        SERVICE_ID, INSTANCE_ID, METHOD_WINDOW,
        std::bind(&on_window_control, std::placeholders::_1));

    /* 4. 提供服务 */
    g_app->offer_service(SERVICE_ID, INSTANCE_ID);
    std::cout << "[SERVICE] 车辆服务已提供" << std::endl;
    std::cout << "  Service ID:  0x" << std::hex << SERVICE_ID << std::dec << std::endl;
    std::cout << "  Instance ID: 0x" << std::hex << INSTANCE_ID << std::dec << std::endl;

    /* 5. 注册并初始化事件 */
    {
        /* 车速事件 */
        std::set<vsomeip::eventgroup_id_t> groups = {EVENTGROUP_VEHICLE};
        g_app->offer_event(SERVICE_ID, INSTANCE_ID, EVENT_SPEED, groups,
                           vsomeip::event_type_e::ET_EVENT,
                           std::chrono::milliseconds::zero(),
                           false, true, nullptr,
                           vsomeip::reliability_type_e::RT_UNRELIABLE);
    }
    {
        std::set<vsomeip::eventgroup_id_t> groups = {EVENTGROUP_VEHICLE};
        g_app->offer_event(SERVICE_ID, INSTANCE_ID, EVENT_DOOR, groups,
                           vsomeip::event_type_e::ET_EVENT,
                           std::chrono::milliseconds::zero(),
                           false, true, nullptr,
                           vsomeip::reliability_type_e::RT_UNRELIABLE);
    }

    /* 6. 启动应用 (在独立线程中运行消息循环) */
    g_app->start();
    std::cout << "[SERVICE] 服务已启动, 开始模拟车辆数据..." << std::endl;

    /* 7. 模拟车辆数据变化循环 */
    std::uniform_real_distribution<double> speed_noise(-2.0, 2.0);
    int tick = 0;

    while (g_running) {
        tick++;

        /* 模拟车速变化 (0→80→60→0 循环) */
        if (tick <= 10) {
            g_speed += 8.0;                        /* 加速 0→80 */
        } else if (tick <= 20) {
            g_speed += speed_noise(g_rng);         /* 巡航 */
        } else if (tick <= 25) {
            g_speed -= 16.0;                       /* 减速 */
        } else {
            g_speed = 0.0;                         /* 停车 */
            if (tick > 30) tick = 0;
        }

        if (g_speed < 0.0) g_speed = 0.0;
        if (g_speed > 120.0) g_speed = 120.0;

        /* 模拟车门状态 */
        if (g_speed == 0.0 && tick % 10 == 0) {
            g_door_open = !g_door_open;
        } else if (g_speed > 0.0) {
            g_door_open = false;
        }

        /* 通知车速事件 */
        g_app->notify(SERVICE_ID, INSTANCE_ID, EVENT_SPEED,
                      create_speed_payload(g_speed));

        std::cout << "[SERVICE] 事件: speed=" << std::fixed << std::setprecision(1)
                  << g_speed << " km/h";

        /* 通知车门事件 */
        g_app->notify(SERVICE_ID, INSTANCE_ID, EVENT_DOOR,
                      create_door_payload(g_door_open));

        std::cout << " | door=" << (g_door_open ? "开" : "关")
                  << " | window=" << g_window_pos << "%" << std::endl;

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    /* 8. 清理 */
    g_app->stop_offer_service(SERVICE_ID, INSTANCE_ID);
    g_app->stop();
    std::cout << "[SERVICE] 服务已停止" << std::endl;

    return 0;
}
