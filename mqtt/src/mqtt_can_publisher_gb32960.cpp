/**
 * mqtt_can_publisher_gb32960.cpp — 国标 GB/T 32960 上报发布者
 *
 * 演示:
 *   1. GB/T 32960-2016 周期上报 (整车/电机/电池, ≤30s)
 *   2. 企标变化上报 (急加速/急减速/制动事件)
 *   3. 整车状态切换 (行驶/充电/熄火) 影响上报策略
 *   4. Protobuf 原始 CAN 帧 + 信号快照 仍然正常发布
 *
 * Topic:
 *   - vehicle/can/raw           : 原始 CAN 帧
 *   - vehicle/can/signals       : Protobuf 信号快照
 *   - vehicle/gb32960/{signal}  : 国标周期/变化上报
 *
 * 用法: ./mqtt_can_publisher_gb32960 [--config path/to.conf]
 */

#include "mqtt_can/can_simulator.hpp"
#include "mqtt_can/can_parser.hpp"
#include "mqtt_can/mqtt_client.hpp"
#include "mqtt_can/config_manager.hpp"
#include "mqtt_can/report_scheduler.hpp"

#include <cstdio>
#include <csignal>
#include <ctime>
#include <thread>
#include <chrono>
#include <atomic>
#include <sstream>
#include <random>

#include "proto/vehicle_can.pb-c.h"

using namespace mqtt_can;

static std::atomic<bool> running{true};
static void signal_handler(int) { running = false; }

// ---- Protobuf 序列化 ----
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
    std::string client_id   = cfg.get_string("publisher.client_id", "can-pub-gb32960");
    std::string vehicle_id  = cfg.get_string("publisher.vehicle_id", "VIN_GB32960_TEST");
    bool        cold_start  = cfg.get_bool("publisher.cold_start", false);

    std::string t_raw      = cfg.get_string("topics.raw.topic", "vehicle/can/raw");
    int         q_raw      = cfg.get_int("topics.raw.qos", 0);
    std::string t_signals  = cfg.get_string("topics.signals.topic", "vehicle/can/signals");
    int         q_signals  = cfg.get_int("topics.signals.qos", 1);

    // ---- MQTT 客户端 (v5 + 自动重连) ----
    MqttClient::Config mqtt_cfg;
    mqtt_cfg.broker_address = broker_addr;
    mqtt_cfg.client_id      = client_id;
    mqtt_cfg.mqtt_version   = 5;
    mqtt_cfg.auto_reconnect = true;
    mqtt_cfg.reconnect_min_ms = 500;
    mqtt_cfg.reconnect_max_ms = 30000;

    MqttClient mqtt(mqtt_cfg);
    mqtt.set_on_status([](const std::string& s, const std::string& d) {
        std::printf("[MQTT] 状态: %s (%s)\n", s.c_str(), d.c_str());
    });

    if (!mqtt.connect()) return 1;

    // ---- 初始化 ----
    CanDatabase::instance().init_default();
    CanSimulator sim(cold_start);
    CanParser parser(vehicle_id);

    // ---- 国标上报调度器 (周期 + 变化) ----
    ReportScheduler gb_scheduler(gb32960_signals(), vehicle_id);
    ReportScheduler ent_scheduler(enterprise_signals(), vehicle_id);

    // 模拟车辆状态切换 (随机)
    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<double> dist(0, 1);
    auto last_status_switch = std::chrono::steady_clock::now();

    std::printf("\n============================================================\n");
    std::printf("  GB/T 32960 上报发布者 (车辆: %s)\n", vehicle_id.c_str());
    std::printf("  国标周期信号: 9 项 (≤30s)  |  企标变化信号: 6 项\n");
    std::printf("  Broker: %s  |  MQTT v5  |  Auto Reconnect\n", broker_addr.c_str());
    std::printf("  按 Ctrl+C 退出\n");
    std::printf("============================================================\n\n");

    auto last_pb = std::chrono::steady_clock::now();
    int frame_count = 0;

    while (running) {
        // 1. CAN 数据采集
        auto frames = sim.tick(8);
        frame_count += static_cast<int>(frames.size());
        for (const auto& f : frames) {
            parser.feed(f);
            // 原始帧用 QoS 0 (量大)
            VehicleCan__CanFrame rpb = VEHICLE_CAN__CAN_FRAME__INIT;
            rpb.can_id = f.can_id;
            rpb.timestamp_ms = f.timestamp_ms;
            rpb.data.data = const_cast<uint8_t*>(f.data.data());
            rpb.data.len = f.dlc;
            rpb.dlc = f.dlc;
            size_t rlen = vehicle_can__can_frame__get_packed_size(&rpb);
            std::vector<uint8_t> rbuf(rlen);
            vehicle_can__can_frame__pack(&rpb, rbuf.data());
            mqtt.publish(t_raw, rbuf.data(), rbuf.size(), q_raw);
        }

        // 2. 国标/企标上报 (由调度器决定是否上报)
        gb_scheduler.tick(parser, [&](const std::string& json, const std::string& signal) {
            std::string topic = "vehicle/gb32960/" + signal;
            mqtt.publish(topic, json.data(), json.size(), 1);
            std::printf("[GB32960] 上报: %s (%s)\n", signal.c_str(),
                        json.find("periodic") != std::string::npos ? "周期" : "变化");
        });

        ent_scheduler.tick(parser, [&](const std::string& json, const std::string& signal) {
            std::string topic = "vehicle/enterprise/" + signal;
            mqtt.publish(topic, json.data(), json.size(), 1);
            std::printf("[企业] 上报: %s (%s)\n", signal.c_str(),
                        json.find("periodic") != std::string::npos ? "周期" : "变化");
        });

        // 3. Protobuf 信号快照 (每秒)
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last_pb).count() >= 1000) {
            last_pb = now;

            auto pb = build_signals_pb(parser);
            mqtt.publish(t_signals, pb.data(), pb.size(), q_signals);

            // 4. 随机切换车辆状态 (模拟充电/行驶/熄火)
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                now - last_status_switch).count();
            if (elapsed > 60 && dist(rng) < 0.3) {
                last_status_switch = now;
                const char* statuses[] = {"running", "charging", "parked", "running"};
                const char* new_status = statuses[std::uniform_int_distribution<int>(0, 3)(rng)];
                gb_scheduler.set_vehicle_status(new_status);
                ent_scheduler.set_vehicle_status(new_status);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    std::printf("\n[EXIT] 共处理 %d 个 CAN 帧\n", frame_count);
    mqtt.disconnect();
    return 0;
}
