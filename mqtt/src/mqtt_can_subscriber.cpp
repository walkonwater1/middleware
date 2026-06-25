/**
 * mqtt_can_subscriber.cpp — MQTT CAN 数据订阅者 (C++)
 *
 * 配置文件: config/can_mqtt.conf
 * 用法: ./mqtt_can_subscriber [--config path/to.conf]
 */

#include "mqtt_can/mqtt_client.hpp"
#include "mqtt_can/config_manager.hpp"

#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <cstring>
#include <string>
#include <atomic>
#include <thread>
#include <chrono>

#include "proto/vehicle_can.pb-c.h"

using namespace mqtt_can;

static std::atomic<bool> running{true};
static void signal_handler(int) { running = false; }

// ============================================================
// JSON 简易解析
// ============================================================

static double json_find_number(const std::string& json, const std::string& key)
{
    std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) return 0.0;
    return std::strtod(json.c_str() + pos + search.size(), nullptr);
}

static std::string json_find_string(const std::string& json, const std::string& key)
{
    std::string search = "\"" + key + "\":\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";
    auto start = pos + search.size();
    auto end = json.find('"', start);
    if (end == std::string::npos) return "";
    return json.substr(start, end - start);
}

// ============================================================
// 消息处理
// ============================================================

static void handle_protobuf(const uint8_t* payload, size_t len)
{
    auto* msg = vehicle_can__vehicle_signals__unpack(nullptr, len, payload);
    if (!msg) { std::printf("[ERR] protobuf 解码失败\n"); return; }

    const char* gear_names[]  = {"?", "P", "R", "N", "D", "S"};
    const char* brake_names[] = {"OFF", "ON", "ABS"};

    std::printf("\n[PROTO] %s @ %llu\n",
                msg->vehicle_id ? msg->vehicle_id : "?",
                static_cast<unsigned long long>(msg->timestamp));
    std::printf("  动力: 转速=%.0frpm  车速=%.1fkm/h  油门=%.0f%%\n",
                msg->engine_speed, msg->vehicle_speed, msg->throttle_position);
    std::printf("  电气: 水温=%.0f°C  电压=%.1fV  燃油=%.0f%%\n",
                msg->coolant_temp, msg->battery_voltage, msg->fuel_level);
    std::printf("  底盘: 档位=%s  转向=%.1f°  转向率=%.1f°/s\n",
                (msg->gear_position >= 0 && static_cast<size_t>(msg->gear_position) <= 5)
                    ? gear_names[msg->gear_position] : "?",
                msg->steering_angle, msg->steering_rate);
    std::printf("  制动: %s  里程=%.0fkm\n",
                (msg->brake_switch >= 0 && static_cast<size_t>(msg->brake_switch) <= 2)
                    ? brake_names[msg->brake_switch] : "?",
                msg->odometer);

    vehicle_can__vehicle_signals__free_unpacked(msg, nullptr);
}

static void handle_json(const uint8_t* payload, size_t len)
{
    std::string json(reinterpret_cast<const char*>(payload), len);
    auto vid = json_find_string(json, "vehicle_id");
    double ts = json_find_number(json, "ts");
    std::printf("\n[JSON] %s @ %.0f\n", vid.c_str(), ts);

    static const struct { const char* field; const char* label; const char* unit; } fields[] = {
        {"engine_speed","转速","rpm"}, {"vehicle_speed","车速","km/h"},
        {"throttle_position","油门","%"}, {"coolant_temp","水温","°C"},
        {"battery_voltage","电池电压","V"}, {"fuel_level","燃油","%"},
        {"steering_angle","转向角","deg"}, {"steering_rate","转向率","deg/s"},
        {"odometer","里程","km"},
    };
    for (const auto& f : fields) {
        double val = json_find_number(json, f.field);
        std::printf("  %s: %.1f%s\n", f.label, val, f.unit);
    }
    auto gear = json_find_string(json, "gear");
    if (!gear.empty()) std::printf("  档位: %s\n", gear.c_str());
    auto brake = json_find_string(json, "brake");
    if (!brake.empty()) std::printf("  制动: %s\n", brake.c_str());
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
    std::string broker_addr = cfg.get_string("broker.address", "tcp://test.mosquitto.org:1883");
    int         broker_keep = cfg.get_int("broker.keepalive", 60);

    std::string client_id    = cfg.get_string("subscriber.client_id", "can-subscriber");

    std::string topic_signals = cfg.get_string("topics.signals.topic", "vehicle/can/signals");
    int         qos_signals   = cfg.get_int("topics.signals.qos", 1);
    std::string topic_json    = cfg.get_string("topics.json.topic", "vehicle/can/json");
    int         qos_json      = cfg.get_int("topics.json.qos", 1);

    // ---- 初始化 ----
    MqttClient::Config mqtt_cfg;
    mqtt_cfg.broker_address = broker_addr;
    mqtt_cfg.client_id      = client_id;
    mqtt_cfg.keepalive      = broker_keep;

    MqttClient mqtt(mqtt_cfg);

    mqtt.set_on_status([](const std::string& status, const std::string& detail) {
        if (status == "connected") std::printf("[MQTT] 已连接\n");
        else std::printf("[MQTT] %s: %s\n", status.c_str(), detail.c_str());
    });

    mqtt.set_on_message([&topic_signals, &topic_json](
            const std::string& topic, const uint8_t* payload, size_t len) {
        if (topic == topic_signals) {
            handle_protobuf(payload, len);
        } else if (topic == topic_json) {
            handle_json(payload, len);
        }
    });

    if (!mqtt.connect()) return 1;
    mqtt.subscribe({{topic_signals, qos_signals}, {topic_json, qos_json}});

    std::printf("\n============================================================\n");
    std::printf("  CAN -> MQTT 订阅者启动\n");
    std::printf("  Broker: %s\n", broker_addr.c_str());
    std::printf("  Topics: %s / %s\n", topic_signals.c_str(), topic_json.c_str());
    std::printf("  按 Ctrl+C 退出\n");
    std::printf("============================================================\n\n");

    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::printf("\n[EXIT] 停止订阅\n");
    mqtt.disconnect();
    return 0;
}
