/**
 * mqtt_can/mqtt_client.hpp — MQTT 客户端 RAII 封装
 *
 * 支持:
 *   - MQTT v3.1.1 / v5 协议切换
 *   - 指数退避自动重连
 *   - 连接状态回调 & 健康检查
 */

#ifndef MQTT_CAN_MQTT_CLIENT_HPP
#define MQTT_CAN_MQTT_CLIENT_HPP

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <atomic>
#include <chrono>

#include <MQTTAsync.h>

namespace mqtt_can {

class MqttClient {
public:
    using MessageCallback = std::function<void(const std::string& topic,
                                                const uint8_t* payload,
                                                size_t len)>;
    // status: "connected" | "disconnected" | "reconnecting" | "lost"
    using StatusCallback  = std::function<void(const std::string& status,
                                                const std::string& detail)>;

    struct Config {
        std::string broker_address = "tcp://test.mosquitto.org:1883";
        std::string client_id      = "mqtt-can-client";
        int         keepalive      = 60;
        // 是否使用 MQTT v5 (0=3.1.1, 5=v5)
        int         mqtt_version   = 0;
        // 断线重连配置
        bool        auto_reconnect     = false;
        int         reconnect_min_ms   = 500;    // 首次重连间隔
        int         reconnect_max_ms   = 30000;  // 最大重连间隔 (指数退避上限)
        // v5 特有
        uint32_t    session_expiry_sec = 3600;   // 断连后 Broker 保留会话时长 (v5)
    };

    struct SubscribeEntry {
        std::string topic;
        int         qos = 1;
    };

    explicit MqttClient(Config cfg);
    ~MqttClient();

    MqttClient(const MqttClient&) = delete;
    MqttClient& operator=(const MqttClient&) = delete;

    /** 连接 (auto_reconnect=true 时会后台持续重连) */
    bool connect(int timeout_ms = 5000);

    /** 断开 (标记停止重连) */
    void disconnect();

    bool subscribe(const std::vector<SubscribeEntry>& entries);

    /** 发布 (使用 Topic Alias 时传 0 即用别名) */
    bool publish(const std::string& topic, const void* payload, size_t len,
                 int qos = 1, bool retained = false);

    bool is_connected() const { return connected_; }

    void set_on_status(StatusCallback cb) { on_status_ = std::move(cb); }
    void set_on_message(MessageCallback cb) { on_message_ = std::move(cb); }

    /** 注册 Topic Alias (v5), 返回别名编号 */
    int register_topic_alias(const std::string& topic);

private:
    static void c_on_connect(void* ctx, MQTTAsync_successData* r);
    static void c_on_connect_failure(void* ctx, MQTTAsync_failureData* r);
    static void c_on_disconnect(void* ctx, MQTTAsync_successData* r);
    static int  c_on_message(void* ctx, char* topic, int len, MQTTAsync_message* msg);

    void do_connect();
    void start_reconnect();
    void stop_reconnect();

    Config         config_;
    MQTTAsync      handle_    = nullptr;
    std::atomic<bool> connected_{false};
    std::atomic<bool> shutdown_{false};
    std::atomic<bool> reconnecting_{false};

    std::vector<std::string> topic_aliases_;  // v5: 别名→完整Topic

    StatusCallback  on_status_;
    MessageCallback on_message_;
};

} // namespace mqtt_can

#endif
