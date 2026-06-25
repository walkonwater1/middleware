/**
 * mqtt_can_publisher.cpp — MQTT CAN 数据发布者 (C++)
 *
 * Pipeline: CanSimulator → CanFrame → CanParser → Protobuf → MQTT
 *
 * 配置文件: config/can_mqtt.conf
 * 用法: ./mqtt_can_publisher [--config path/to.conf]
 */

#include "mqtt_can/can_simulator.hpp"
#include "mqtt_can/can_parser.hpp"
#include "mqtt_can/mqtt_client.hpp"
#include "mqtt_can/config_manager.hpp"

#include <cstdio>
#include <csignal>
#include <ctime>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <sstream>

#include "proto/vehicle_can.pb-c.h"

using namespace mqtt_can;

static std::atomic<bool> running{true};
static void signal_handler(int) { running = false; }

// ============================================================
// Protobuf 序列化辅助
// ============================================================

static std::vector<uint8_t> build_raw_frame_pb(const CanFrame& cf)
{
    // proto 包名 vehicle_can，生成类型前缀 VehicleCan__
    VehicleCan__CanFrame pb = VEHICLE_CAN__CAN_FRAME__INIT;
    pb.can_id       = cf.can_id;
    pb.timestamp_ms = cf.timestamp_ms;
    pb.data.data    = const_cast<uint8_t*>(cf.data.data());
    pb.data.len     = cf.dlc;
    pb.dlc          = cf.dlc;

    size_t len = vehicle_can__can_frame__get_packed_size(&pb);
    std::vector<uint8_t> buf(len);
    vehicle_can__can_frame__pack(&pb, buf.data());
    return buf;
}

static std::vector<uint8_t> build_signals_pb(const CanParser& parser)
{
    VehicleCan__VehicleSignals pb = VEHICLE_CAN__VEHICLE_SIGNALS__INIT;
    std::string vid = parser.vehicle_id();

    pb.vehicle_id = const_cast<char*>(vid.c_str());
    pb.timestamp  = static_cast<uint64_t>(std::time(nullptr));

    auto set_float = [&](const std::string& name, float& field) {
        auto v = parser.get(name);
        if (v) field = *v;
    };
    set_float("EngineSpeed",      pb.engine_speed);
    set_float("VehicleSpeed",     pb.vehicle_speed);
    set_float("ThrottlePosition", pb.throttle_position);
    set_float("CoolantTemp",      pb.coolant_temp);
    set_float("BatteryVoltage",   pb.battery_voltage);
    set_float("FuelLevel",        pb.fuel_level);
    set_float("SteeringAngle",    pb.steering_angle);
    set_float("SteeringRate",     pb.steering_rate);
    set_float("Odometer",         pb.odometer);

    auto gear_v = parser.get("GearPosition");
    if (gear_v) {
        int g = static_cast<int>(*gear_v);
        pb.gear_position = (g >= 1 && g <= 5)
            ? static_cast<VehicleCan__GearPosition>(g)
            : VEHICLE_CAN__GEAR_POSITION__GEAR_UNKNOWN;
    }

    auto brake_v = parser.get("BrakeSwitch");
    if (brake_v) {
        int b = static_cast<int>(*brake_v);
        pb.brake_switch = (b == 1) ? VEHICLE_CAN__BRAKE_STATE__BRAKE_ENGAGED
                       : (b == 2) ? VEHICLE_CAN__BRAKE_STATE__BRAKE_ABS_ACTIVE
                       : VEHICLE_CAN__BRAKE_STATE__BRAKE_OFF;
    }

    size_t len = vehicle_can__vehicle_signals__get_packed_size(&pb);
    std::vector<uint8_t> buf(len);
    vehicle_can__vehicle_signals__pack(&pb, buf.data());
    return buf;
}

static std::string build_signals_json(const CanParser& parser)
{
    const char* gear_names[]  = {"未知", "P", "R", "N", "D", "S"};
    const char* brake_names[] = {"OFF", "ON", "ABS"};

    auto get_or = [&](const std::string& name, float def = 0.0f) -> float {
        auto v = parser.get(name); return v ? *v : def;
    };
    int gear_val  = static_cast<int>(get_or("GearPosition"));
    int brake_val = static_cast<int>(get_or("BrakeSwitch"));

    std::ostringstream oss;
    oss << "{\"vehicle_id\":\"" << parser.vehicle_id() << "\""
        << ",\"ts\":" << std::time(nullptr)
        << ",\"engine_speed\":" << get_or("EngineSpeed")
        << ",\"vehicle_speed\":" << get_or("VehicleSpeed")
        << ",\"throttle_position\":" << get_or("ThrottlePosition")
        << ",\"coolant_temp\":" << get_or("CoolantTemp")
        << ",\"battery_voltage\":" << get_or("BatteryVoltage")
        << ",\"fuel_level\":" << get_or("FuelLevel")
        << ",\"gear\":\"" << (gear_val >= 0 && gear_val <= 5 ? gear_names[gear_val] : "?") << "\""
        << ",\"steering_angle\":" << get_or("SteeringAngle")
        << ",\"steering_rate\":" << get_or("SteeringRate")
        << ",\"brake\":\"" << (brake_val >= 0 && brake_val <= 2 ? brake_names[brake_val] : "?") << "\""
        << ",\"odometer\":" << get_or("Odometer") << "}";
    return oss.str();
}

// ============================================================
// main
// ============================================================

int main(int argc, char* argv[])
{
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // ---- 加载配置 ----
    std::string config_path = "config/can_mqtt.conf";
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        }
    }

    ConfigManager cfg;
    if (!cfg.load(config_path)) {
        std::fprintf(stderr, "[ERR] 无法加载配置文件, 使用默认值\n");
    }
    cfg.print();

    // ---- 读取配置项 ----
    std::string broker_addr  = cfg.get_string("broker.address", "tcp://test.mosquitto.org:1883");
    int         broker_keep  = cfg.get_int("broker.keepalive", 60);

    std::string client_id    = cfg.get_string("publisher.client_id", "can-publisher");
    std::string vehicle_id   = cfg.get_string("publisher.vehicle_id", "VIN_LX_TEST_001");
    bool        cold_start   = cfg.get_bool("publisher.cold_start", false);
    int         pub_interval = cfg.get_int("publisher.publish_interval_ms", 1000);

    // 各 Topic 独立 QoS
    std::string topic_raw     = cfg.get_string("topics.raw.topic", "vehicle/can/raw");
    int         qos_raw       = cfg.get_int("topics.raw.qos", 0);
    std::string topic_signals = cfg.get_string("topics.signals.topic", "vehicle/can/signals");
    int         qos_signals   = cfg.get_int("topics.signals.qos", 1);
    std::string topic_json    = cfg.get_string("topics.json.topic", "vehicle/can/json");
    int         qos_json      = cfg.get_int("topics.json.qos", 1);

    // ---- 初始化 ----
    CanDatabase::instance().init_default();
    CanDatabase::instance().print();

    CanSimulator sim(cold_start);
    CanParser parser(vehicle_id);

    MqttClient::Config mqtt_cfg;
    mqtt_cfg.broker_address = broker_addr;
    mqtt_cfg.client_id      = client_id;
    mqtt_cfg.keepalive      = broker_keep;

    MqttClient mqtt(mqtt_cfg);
    mqtt.set_on_status([](const std::string& status, const std::string& detail) {
        if (status == "connected") std::printf("[MQTT] 已连接\n");
        else std::printf("[MQTT] %s: %s\n", status.c_str(), detail.c_str());
    });

    if (!mqtt.connect()) return 1;

    std::printf("\n============================================================\n");
    std::printf("  CAN -> MQTT 发布者启动 (车辆: %s)\n", vehicle_id.c_str());
    std::printf("  Broker: %s\n", broker_addr.c_str());
    std::printf("  Topics: %s / %s / %s\n",
                topic_raw.c_str(), topic_signals.c_str(), topic_json.c_str());
    std::printf("  按 Ctrl+C 退出\n");
    std::printf("============================================================\n\n");

    int frame_count = 0;
    auto last_publish = std::chrono::steady_clock::now();

    while (running) {
        auto frames = sim.tick(8);
        frame_count += static_cast<int>(frames.size());

        for (const auto& f : frames) {
            parser.feed(f);
            auto raw_pb = build_raw_frame_pb(f);
            mqtt.publish(topic_raw, raw_pb.data(), raw_pb.size(), qos_raw);
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_publish).count();
        if (elapsed >= pub_interval) {
            last_publish = now;

            auto sig_pb = build_signals_pb(parser);
            mqtt.publish(topic_signals, sig_pb.data(), sig_pb.size(), qos_signals);

            auto json_str = build_signals_json(parser);
            mqtt.publish(topic_json, json_str.data(), json_str.size(), qos_json);

            parser.print_snapshot();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    std::printf("\n[EXIT] 共处理 %d 个 CAN 帧\n", frame_count);
    mqtt.disconnect();
    return 0;
}
