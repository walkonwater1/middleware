/**
 * MQTT 订阅者 Demo (C) — 订阅车载传感器数据
 *
 * 依赖: Eclipse Paho MQTT C Client (libpaho-mqtt3as)
 * 安装: sudo apt install libpaho-mqtt-dev
 *
 * 订阅主题:
 *   - vehicle/speed   : 车速
 *   - vehicle/temp    : 发动机温度
 *   - vehicle/gps     : GPS 坐标
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include <MQTTAsync.h>

/* ============================================================
 * 配置
 * ============================================================ */
#define BROKER_ADDRESS  "tcp://test.mosquitto.org:1883"
#define CLIENT_ID       "vehicle-subscriber-c-001"
#define QOS             1
#define KEEPALIVE       60
#define TOPIC_COUNT     3

static volatile int running = 1;

static const char *topics[TOPIC_COUNT] = {
    "vehicle/speed",
    "vehicle/temp",
    "vehicle/gps",
};

/* ============================================================
 * 信号处理
 * ============================================================ */
static void signal_handler(int sig)
{
    (void)sig;
    running = 0;
}

/* ============================================================
 * JSON 简单解析辅助
 * ============================================================ */
static int json_find_number(const char *json, const char *key, double *val)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *p = strstr(json, search);
    if (!p) return -1;
    p += strlen(search);
    *val = strtod(p, NULL);
    return 0;
}

static int json_find_string(const char *json, const char *key,
                             char *out, size_t out_size)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":\"", key);
    const char *p = strstr(json, search);
    if (!p) return -1;
    p += strlen(search);
    const char *end = strchr(p, '"');
    if (!end) return -1;
    size_t len = (size_t)(end - p);
    if (len >= out_size) len = out_size - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return 0;
}

/* ============================================================
 * MQTT 回调
 * ============================================================ */
static int on_message(void *context, char *topic_name, int topic_len,
                      MQTTAsync_message *message)
{
    (void)context;
    (void)topic_len;

    char *payload = (char *)message->payload;
    size_t plen   = message->payloadlen;

    if (payload && plen > 0) {
        char buf[512];
        size_t copy_len = plen < sizeof(buf) - 1 ? plen : sizeof(buf) - 1;
        memcpy(buf, payload, copy_len);
        buf[copy_len] = '\0';

        double value;
        char unit[32];

        if (strstr(topic_name, "speed")) {
            if (json_find_number(buf, "value", &value) == 0) {
                json_find_string(buf, "unit", unit, sizeof(unit));
                printf("[RECV] 车速: %.1f %s\n", value, unit);
            }
        } else if (strstr(topic_name, "temp")) {
            if (json_find_number(buf, "value", &value) == 0) {
                json_find_string(buf, "unit", unit, sizeof(unit));
                printf("[RECV] 温度: %.1f %s\n", value, unit);
            }
        } else if (strstr(topic_name, "gps")) {
            double lat, lon;
            if (json_find_number(buf, "lat", &lat) == 0 &&
                json_find_number(buf, "lon", &lon) == 0) {
                printf("[RECV] GPS: (%.6f, %.6f)\n", lat, lon);
            }
        } else {
            printf("[RECV] %s: %s\n", topic_name, buf);
        }
    }

    MQTTAsync_freeMessage(&message);
    MQTTAsync_free(topic_name);
    return 1;
}

static void on_connect(void *context, MQTTAsync_successData *response)
{
    (void)context;
    (void)response;
    printf("[OK] 已连接到 Broker: %s\n", BROKER_ADDRESS);
}

static void on_connect_failure(void *context, MQTTAsync_failureData *response)
{
    (void)context;
    printf("[ERR] 连接失败: %s\n",
           response && response->message ? response->message : "unknown");
    running = 0;
}

static void on_subscribe(void *context, MQTTAsync_successData *response)
{
    (void)context;
    (void)response;
    printf("[SUB] 订阅成功\n");
}

static void on_subscribe_failure(void *context, MQTTAsync_failureData *response)
{
    (void)context;
    printf("[ERR] 订阅失败: %s\n",
           response && response->message ? response->message : "unknown");
}

static void on_disconnect(void *context, MQTTAsync_successData *response)
{
    (void)context;
    (void)response;
    printf("[OK] 已断开连接\n");
}

/* ============================================================
 * 主逻辑
 * ============================================================ */
int main(void)
{
    MQTTAsync client;
    MQTTAsync_connectOptions conn_opts = MQTTAsync_connectOptions_initializer;
    MQTTAsync_responseOptions sub_opts  = MQTTAsync_responseOptions_initializer;
    int rc;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    rc = MQTTAsync_create(&client, BROKER_ADDRESS, CLIENT_ID,
                          MQTTCLIENT_PERSISTENCE_NONE, NULL);
    if (rc != MQTTASYNC_SUCCESS) {
        fprintf(stderr, "[ERR] 创建客户端失败, rc=%d\n", rc);
        return 1;
    }

    /* 设置消息回调 */
    MQTTAsync_setCallbacks(client, NULL, NULL, on_message, NULL);

    /* 连接选项 */
    conn_opts.onSuccess = on_connect;
    conn_opts.onFailure = on_connect_failure;
    conn_opts.keepAliveInterval = KEEPALIVE;
    conn_opts.cleansession = 1;

    printf("正在连接 %s ...\n", BROKER_ADDRESS);
    rc = MQTTAsync_connect(client, &conn_opts);
    if (rc != MQTTASYNC_SUCCESS) {
        fprintf(stderr, "[ERR] 发起连接失败, rc=%d\n", rc);
        MQTTAsync_destroy(&client);
        return 1;
    }

    sleep(1);  /* 等待连接 */

    /* 订阅所有主题 */
    sub_opts.onSuccess = on_subscribe;
    sub_opts.onFailure = on_subscribe_failure;

    int sub_qos[TOPIC_COUNT] = {QOS, QOS, QOS};
    rc = MQTTAsync_subscribeMany(client, TOPIC_COUNT, topics, sub_qos, &sub_opts);
    if (rc != MQTTASYNC_SUCCESS) {
        fprintf(stderr, "[ERR] 发起订阅失败, rc=%d\n", rc);
    }

    printf("正在监听 %s ...\n", BROKER_ADDRESS);
    printf("按 Ctrl+C 退出\n\n");

    /* 保持运行 */
    while (running) {
        usleep(100000);  /* 100ms */
    }

    printf("\n[EXIT] 停止订阅\n");

    /* 取消订阅 */
    MQTTAsync_unsubscribeMany(client, TOPIC_COUNT, topics, NULL);

    /* 断开 */
    MQTTAsync_disconnectOptions disc_opts = MQTTAsync_disconnectOptions_initializer;
    disc_opts.onSuccess = on_disconnect;
    disc_opts.timeout = 3000;
    MQTTAsync_disconnect(client, &disc_opts);
    sleep(1);

    MQTTAsync_destroy(&client);
    return 0;
}
