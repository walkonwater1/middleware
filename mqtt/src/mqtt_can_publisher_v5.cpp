/**
 * mqtt_can_publisher_v5.cpp — MQTT v5 CAN 数据发布者
 *
 * 演示 MQTT v5 新特性:
 *   - Topic Alias: 将长 Topic 名压缩为整型别名，节省带宽
 *   - Session Expiry: 断连后 Broker 保留会话，重连后自动恢复订阅
 *   - 自动重连: 指数退避
 *
 * 用法: ./mqtt_can_publisher_v5 [--config path/to.conf]
 */

#include "mqtt_can/can_simulator.hpp"
#include "mqtt_can/can_parser.hpp"
#include "mqtt_can/mqtt_client.hpp"
#include "mqtt_can/config_manager.hpp"

#include <cstdio>
#include <csignal>
#include <ctime>
#include <thread>
#include <chrono>
#include <atomic>
#include <sstream>

#include "proto/vehicle_can.pb-c.h"

using namespace mqtt_can;

static std::atomic<bool> running{true};
static void signal_handler(int) { running = false; }

// ---- Protobuf 序列化 (同 v3 版本) ----
static std::vector<uint8_t> build_signals_pb(const CanParser& parser)
{
    VehicleCan__VehicleSignals pb = VEHICLE_CAN__VEHICLE_SIGNALS__INIT;
    std::string vid = parser.vehicle_id();
    pb.vehicle_id = const_cast<char*>(vid.c_str());
    pb.timestamp  = static_cast<uint64_t>(std::time(nullptr));

    auto set = [&](const std::string& n, float& f) {
        auto v = parser.get(n); if (v) f = *v;
    };
    set("EngineSpeed", pb.engine_speed);
    set("VehicleSpeed", pb.vehicle_speed);
    set("ThrottlePosition", pb.throttle_position);
    set("CoolantTemp", pb.coolant_temp);
    set("BatteryVoltage", pb.battery_voltage);
    set("FuelLevel", pb.fuel_level);
    set("SteeringAngle", pb.steering_angle);
    set("SteeringRate", pb.steering_rate);
    set("Odometer", pb.odometer);

    auto g = parser.get("GearPosition");
    if (g) pb.gear_position = static_cast<VehicleCan__GearPosition>(
        std::clamp(static_cast<int>(*g), 1, 5));

    auto b = parser.get("BrakeSwitch");
    if (b) pb.brake_switch = (static_cast<int>(*b) == 1) ? VEHICLE_CAN__BRAKE_STATE__BRAKE_ENGAGED
                           : (static_cast<int>(*b) == 2) ? VEHICLE_CAN__BRAKE_STATE__BRAKE_ABS_ACTIVE
                           : VEHICLE_CAN__BRAKE_STATE__BRAKE_OFF;

    size_t len = vehicle_can__vehicle_signals__get_packed_size(&pb);
    std::vector<uint8_t> buf(len);
    vehicle_can__vehicle_signals__pack(&pb, buf.data());
    return buf;
}

// ============================================================
// main
// ============================================================

int main(int argc, char* argv[])
{
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    ConfigManager cfg;
    std::string config_path = "config/can_mqtt.conf";
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--config" && i + 1 < argc)
            config_path = argv[++i];
    }
    cfg.load(config_path);
    cfg.print();

    // 读取配置
    std::string broker_addr = cfg.get_string("broker.address", "tcp://test.mosquitto.org:1883");
    std::string client_id   = cfg.get_string("publisher.client_id", "can-pub-v5");
    std::string vehicle_id  = cfg.get_string("publisher.vehicle_id", "VIN_LX_TEST_001");
    bool        cold_start  = cfg.get_bool("publisher.cold_start", false);
    int         pub_interval= cfg.get_int("publisher.publish_interval_ms", 1000);

    std::string t_signals = cfg.get_string("topics.signals.topic", "vehicle/can/signals");
    int         q_signals = cfg.get_int("topics.signals.qos", 1);

    // ---- MQTT v5 配置 ----
    MqttClient::Config mqtt_cfg;
    mqtt_cfg.broker_address     = broker_addr;
    mqtt_cfg.client_id          = client_id;
    mqtt_cfg.mqtt_version       = 5;                // ← v5 协议
    mqtt_cfg.session_expiry_sec = 3600;             // 断连 1 小时内 Broker 保留会话
    mqtt_cfg.auto_reconnect     = true;             // ← 自动重连
    mqtt_cfg.reconnect_min_ms   = 500;
    mqtt_cfg.reconnect_max_ms   = 30000;

    MqttClient mqtt(mqtt_cfg);

    mqtt.set_on_status([](const std::string& s, const std::string& d) {
        std::printf("[MQTT] 状态: %s (%s)\n", s.c_str(), d.c_str());
    });

    // ---- 注册 Topic Alias: 长 Topic 名 → 整型别名 ----
    int alias_signals = mqtt.register_topic_alias(t_signals);
    std::printf("[v5] Topic Alias 已注册: %s → alias#%d\n",
                t_signals.c_str(), alias_signals);

    if (!mqtt.connect()) return 1;

    // ---- 初始化 ----
    CanDatabase::instance().init_default();
    CanSimulator sim(cold_start);
    CanParser parser(vehicle_id);

    std::printf("\n============================================================\n");
    std::printf("  MQTT v5 CAN 发布者 (车辆: %s)\n", vehicle_id.c_str());
    std::printf("  v5 特性: Topic Alias | Session Expiry=%us | Auto Reconnect\n",
                mqtt_cfg.session_expiry_sec);
    std::printf("  Broker: %s\n", broker_addr.c_str());
    std::printf("  按 Ctrl+C 退出\n");
    std::printf("============================================================\n\n");

    auto last_publish = std::chrono::steady_clock::now();
    int frame_count = 0;

    while (running) {
        auto frames = sim.tick(8);
        frame_count += static_cast<int>(frames.size());

        for (const auto& f : frames)
            parser.feed(f);

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last_publish).count() >= pub_interval) {
            last_publish = now;

            if (mqtt.is_connected()) {
                auto pb = build_signals_pb(parser);
                // Topic Alias 自动生效: MqttClient 查找已注册的别名
                mqtt.publish(t_signals, pb.data(), pb.size(), q_signals);
            } else {
                std::printf("[v5] 离线中... 数据已丢弃 (Broker 将在重连后恢复)\n");
            }

            parser.print_snapshot();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    std::printf("\n[EXIT] 共处理 %d 个 CAN 帧\n", frame_count);
    mqtt.disconnect();
    return 0;
}
