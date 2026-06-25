/**
 * mqtt_client.cpp — MQTT 客户端 RAII 封装  (断线重连 + MQTT v5 支持)
 */

#include "mqtt_can/mqtt_client.hpp"
#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>
#include <algorithm>
#include <mutex>

namespace mqtt_can {

// ============================================================
// C 回调 → C++ 转发
// ============================================================

void MqttClient::c_on_connect(void* ctx, MQTTAsync_successData*)
{
    auto* self = static_cast<MqttClient*>(ctx);
    self->connected_ = true;
    self->reconnecting_ = false;
    if (self->on_status_)
        self->on_status_("connected", self->config_.broker_address);
}

void MqttClient::c_on_connect_failure(void* ctx, MQTTAsync_failureData* r)
{
    auto* self = static_cast<MqttClient*>(ctx);
    self->connected_ = false;
    std::string err = r && r->message ? r->message : "unknown";
    std::fprintf(stderr, "[MQTT] 连接失败: %s\n", err.c_str());
    if (self->on_status_) self->on_status_("disconnected", err);
    // 触发重连
    if (self->config_.auto_reconnect && !self->shutdown_)
        self->start_reconnect();
}

void MqttClient::c_on_disconnect(void* ctx, MQTTAsync_successData*)
{
    auto* self = static_cast<MqttClient*>(ctx);
    self->connected_ = false;
    if (self->on_status_) self->on_status_("disconnected", "graceful");
    // 异常断开也触发重连
    if (self->config_.auto_reconnect && !self->shutdown_ && !self->reconnecting_)
        self->start_reconnect();
}

int MqttClient::c_on_message(void* ctx, char* topic, int len, MQTTAsync_message* msg)
{
    auto* self = static_cast<MqttClient*>(ctx);
    (void)len;

    if (self->on_message_ && msg->payload && msg->payloadlen > 0) {
        self->on_message_(topic ? topic : "",
                          static_cast<const uint8_t*>(msg->payload),
                          static_cast<size_t>(msg->payloadlen));
    }

    MQTTAsync_freeMessage(&msg);
    MQTTAsync_free(topic);
    return 1;
}

// ============================================================
// 构造 / 析构
// ============================================================

MqttClient::MqttClient(Config cfg)
    : config_(std::move(cfg))
    , topic_aliases_(256)  // v5 最大支持 65535
{
    int rc = MQTTAsync_create(&handle_, config_.broker_address.c_str(),
                               config_.client_id.c_str(),
                               MQTTCLIENT_PERSISTENCE_NONE, nullptr);
    if (rc != MQTTASYNC_SUCCESS) {
        std::fprintf(stderr, "[MQTT] 创建客户端失败, rc=%d\n", rc);
        handle_ = nullptr;
    }
}

MqttClient::~MqttClient()
{
    stop_reconnect();
    if (handle_) MQTTAsync_destroy(&handle_);
}

// ============================================================
// 连接
// ============================================================

void MqttClient::do_connect()
{
    if (!handle_) return;

    MQTTAsync_setCallbacks(handle_, this, nullptr, c_on_message, nullptr);

    MQTTAsync_connectOptions opts = MQTTAsync_connectOptions_initializer;
    opts.onSuccess         = c_on_connect;
    opts.onFailure         = c_on_connect_failure;
    opts.context           = this;
    opts.keepAliveInterval = config_.keepalive;
    opts.cleansession      = (config_.mqtt_version != 5) ? 1 : 0;
    opts.MQTTVersion       = config_.mqtt_version;

    // v5: Session Expiry
    if (config_.mqtt_version == 5 && config_.session_expiry_sec > 0) {
        MQTTProperties props = MQTTProperties_initializer;
        MQTTProperty prop;
        prop.identifier = MQTTPROPERTY_CODE_SESSION_EXPIRY_INTERVAL;
        prop.value.integer4 = config_.session_expiry_sec;
        MQTTProperties_add(&props, &prop);
        opts.connectProperties = &props;
    }

    int rc = MQTTAsync_connect(handle_, &opts);
    if (rc != MQTTASYNC_SUCCESS) {
        std::fprintf(stderr, "[MQTT] 发起连接失败, rc=%d\n", rc);
    }
}

bool MqttClient::connect(int timeout_ms)
{
    if (!handle_) return false;

    std::printf("[MQTT] 正在连接 %s%s...\n",
                config_.broker_address.c_str(),
                config_.mqtt_version == 5 ? " (v5)" : "");

    do_connect();

    auto start = std::chrono::steady_clock::now();
    while (!connected_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed > timeout_ms) {
            std::fprintf(stderr, "[MQTT] 连接超时\n");
            if (config_.auto_reconnect) start_reconnect();
            return false;
        }
    }
    return true;
}

// ============================================================
// 断线重连 (指数退避)
// ============================================================

void MqttClient::start_reconnect()
{
    if (reconnecting_.exchange(true)) return;  // 已在重连
    if (shutdown_) return;

    std::thread([this]() {
        int delay = config_.reconnect_min_ms;
        while (!shutdown_ && !connected_) {
            std::fprintf(stderr, "[MQTT] 断线重连中... (%dms 后重试)\n", delay);
            if (on_status_) on_status_("reconnecting",
                std::to_string(delay) + "ms");

            std::this_thread::sleep_for(std::chrono::milliseconds(delay));

            if (shutdown_ || connected_) break;
            do_connect();

            // 指数退避
            delay = std::min(delay * 2, config_.reconnect_max_ms);

            // 等待本轮连接结果
            for (int i = 0; i < 100 && !connected_ && !shutdown_; i++)
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        reconnecting_ = false;
    }).detach();
}

void MqttClient::stop_reconnect()
{
    shutdown_ = true;
}

// ============================================================
// 断开
// ============================================================

void MqttClient::disconnect()
{
    stop_reconnect();
    if (!handle_ || !connected_) return;

    MQTTAsync_disconnectOptions opts = MQTTAsync_disconnectOptions_initializer;
    opts.onSuccess = c_on_disconnect;
    opts.context   = this;
    opts.timeout   = 3000;

    MQTTAsync_disconnect(handle_, &opts);
    std::this_thread::sleep_for(std::chrono::seconds(1));
}

// ============================================================
// 订阅
// ============================================================

bool MqttClient::subscribe(const std::vector<SubscribeEntry>& entries)
{
    if (!handle_ || entries.empty()) return false;

    std::vector<char*> topics_c;
    std::vector<int>   qos_vals;
    topics_c.reserve(entries.size());
    qos_vals.reserve(entries.size());

    for (const auto& e : entries) {
        topics_c.push_back(const_cast<char*>(e.topic.c_str()));
        qos_vals.push_back(e.qos);
    }

    MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
    int rc = MQTTAsync_subscribeMany(handle_, static_cast<int>(entries.size()),
                                     topics_c.data(), qos_vals.data(), &opts);
    if (rc != MQTTASYNC_SUCCESS) {
        std::fprintf(stderr, "[MQTT] 订阅失败, rc=%d\n", rc);
        return false;
    }
    for (const auto& e : entries)
        std::printf("[MQTT] 已订阅 %s (QoS=%d)\n", e.topic.c_str(), e.qos);
    return true;
}

// ============================================================
// 发布 (支持 v5 Topic Alias)
// ============================================================

int MqttClient::register_topic_alias(const std::string& topic)
{
    for (size_t i = 0; i < topic_aliases_.size(); i++) {
        if (topic_aliases_[i].empty()) {
            topic_aliases_[i] = topic;
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool MqttClient::publish(const std::string& topic, const void* payload,
                         size_t len, int qos, bool retained)
{
    if (!handle_) return false;

    MQTTAsync_message msg = MQTTAsync_message_initializer;
    msg.payload    = const_cast<void*>(payload);
    msg.payloadlen = static_cast<int>(len);
    msg.qos        = qos;
    msg.retained   = retained ? 1 : 0;

    // v5: 查找 Topic Alias 压缩带宽
    if (config_.mqtt_version == 5) {
        for (size_t i = 0; i < topic_aliases_.size(); i++) {
            if (topic_aliases_[i] == topic && !topic_aliases_[i].empty()) {
                MQTTProperty prop;
                prop.identifier = MQTTPROPERTY_CODE_TOPIC_ALIAS;
                prop.value.integer2 = static_cast<uint16_t>(i);
                MQTTProperties_add(&msg.properties, &prop);
                break;
            }
        }
    }

    MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
    int rc = MQTTAsync_sendMessage(handle_, topic.c_str(), &msg, &opts);
    return rc == MQTTASYNC_SUCCESS;
}

} // namespace mqtt_can
