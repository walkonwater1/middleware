/**
 * MQTT 发布者 Demo (C) — 模拟车载传感器数据上报
 *
 * 依赖: Eclipse Paho MQTT C Client (libpaho-mqtt3as)
 * 安装: sudo apt install libpaho-mqtt-dev
 *
 * 发布主题:
 *   - vehicle/speed   : 车速 (km/h)
 *   - vehicle/temp    : 发动机温度 (°C)
 *   - vehicle/gps     : GPS 坐标 (lat, lon)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include <MQTTAsync.h>

/* ============================================================
 * 配置
 * ============================================================ */
#define BROKER_ADDRESS  "tcp://test.mosquitto.org:1883"
#define CLIENT_ID       "vehicle-publisher-c-001"
#define QOS             1
#define KEEPALIVE       60
#define PUBLISH_PERIOD  1  /* 1Hz */

static volatile int running = 1;

/* ============================================================
 * 信号处理
 * ============================================================ */
static void signal_handler(int sig)
{
    (void)sig;
    running = 0;
}

/* ============================================================
 * MQTT 异步回调
 * ============================================================ */
static void on_connect(void *context, MQTTAsync_successData *response)
{
    (void)context;
    (void)response;
    printf("[OK] 已连接到 Broker: %s\n", BROKER_ADDRESS);
}

static void on_connect_failure(void *context, MQTTAsync_failureData *response)
{
    (void)context;
    printf("[ERR] 连接失败, rc=%d: %s\n",
           response ? response->code : -1,
           response && response->message ? response->message : "unknown");
    running = 0;
}

static void on_publish_success(void *context, MQTTAsync_successData *response)
{
    (void)context;
    (void)response;
    /* 可选: 打印发送确认 */
}

static void on_publish_failure(void *context, MQTTAsync_failureData *response)
{
    (void)context;
    printf("[ERR] 发布失败: %s\n",
           response && response->message ? response->message : "unknown");
}

/* ============================================================
 * 生成模拟数据
 * ============================================================ */
static double generate_speed(void)
{
    return (double)(rand() % 12000) / 100.0;  /* 0.00 ~ 120.00 km/h */
}

static double generate_temperature(void)
{
    return 70.0 + (double)(rand() % 4000) / 100.0;  /* 70.00 ~ 110.00 °C */
}

static void generate_gps(double *lat, double *lon)
{
    *lat = 22.53 + (double)(rand() % 4000 - 2000) / 100000.0;
    *lon = 113.93 + (double)(rand() % 4000 - 2000) / 100000.0;
}

/* ============================================================
 * 主逻辑
 * ============================================================ */
int main(void)
{
    MQTTAsync client;
    MQTTAsync_connectOptions conn_opts = MQTTAsync_connectOptions_initializer;
    MQTTAsync_responseOptions pub_opts  = MQTTAsync_responseOptions_initializer;
    int rc;

    srand((unsigned int)time(NULL));
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* 创建客户端 */
    rc = MQTTAsync_create(&client, BROKER_ADDRESS, CLIENT_ID,
                          MQTTCLIENT_PERSISTENCE_NONE, NULL);
    if (rc != MQTTASYNC_SUCCESS) {
        fprintf(stderr, "[ERR] 创建客户端失败, rc=%d\n", rc);
        return 1;
    }

    /* 设置回调 */
    MQTTAsync_setCallbacks(client, NULL, NULL, NULL, NULL);

    /* 设置连接回调 */
    conn_opts.onSuccess = on_connect;
    conn_opts.onFailure = on_connect_failure;
    conn_opts.keepAliveInterval = KEEPALIVE;
    conn_opts.cleansession = 1;

    /* 连接 */
    printf("正在连接 %s ...\n", BROKER_ADDRESS);
    rc = MQTTAsync_connect(client, &conn_opts);
    if (rc != MQTTASYNC_SUCCESS) {
        fprintf(stderr, "[ERR] 发起连接失败, rc=%d\n", rc);
        MQTTAsync_destroy(&client);
        return 1;
    }

    /* 等待连接完成 (简单同步等待) */
    sleep(1);

    /* 设置发布回调 */
    pub_opts.onSuccess = on_publish_success;
    pub_opts.onFailure = on_publish_failure;

    printf("开始发布传感器数据 (1Hz)...\n");
    printf("按 Ctrl+C 退出\n\n");

    /* 循环发布 */
    while (running) {
        char payload[256];
        MQTTAsync_message msg = MQTTAsync_message_initializer;

        /* --- 发布车速 --- */
        snprintf(payload, sizeof(payload),
                 "{\"vehicle_id\":\"V001\",\"value\":%.1f,\"unit\":\"km/h\",\"ts\":%ld}",
                 generate_speed(), (long)time(NULL));
        msg.payload   = payload;
        msg.payloadlen = (int)strlen(payload);
        msg.qos       = QOS;
        msg.retained  = 0;

        rc = MQTTAsync_sendMessage(client, "vehicle/speed", &msg, &pub_opts);
        if (rc == MQTTASYNC_SUCCESS)
            printf("[PUB] vehicle/speed: %s\n", payload);

        /* --- 发布温度 --- */
        snprintf(payload, sizeof(payload),
                 "{\"vehicle_id\":\"V001\",\"value\":%.1f,\"unit\":\"celsius\",\"ts\":%ld}",
                 generate_temperature(), (long)time(NULL));
        msg.payload   = payload;
        msg.payloadlen = (int)strlen(payload);

        rc = MQTTAsync_sendMessage(client, "vehicle/temp", &msg, &pub_opts);
        if (rc == MQTTASYNC_SUCCESS)
            printf("[PUB] vehicle/temp:  %s\n", payload);

        /* --- 发布 GPS --- */
        double lat, lon;
        generate_gps(&lat, &lon);
        snprintf(payload, sizeof(payload),
                 "{\"vehicle_id\":\"V001\",\"lat\":%.6f,\"lon\":%.6f,\"ts\":%ld}",
                 lat, lon, (long)time(NULL));
        msg.payload   = payload;
        msg.payloadlen = (int)strlen(payload);

        rc = MQTTAsync_sendMessage(client, "vehicle/gps", &msg, &pub_opts);
        if (rc == MQTTASYNC_SUCCESS)
            printf("[PUB] vehicle/gps:   %s\n", payload);

        printf("---\n");
        sleep(PUBLISH_PERIOD);
    }

    printf("\n[EXIT] 停止发布\n");

    /* 断开连接 */
    MQTTAsync_disconnectOptions disc_opts = MQTTAsync_disconnectOptions_initializer;
    disc_opts.timeout = 3000;
    MQTTAsync_disconnect(client, &disc_opts);
    sleep(1);

    MQTTAsync_destroy(&client);
    return 0;
}
