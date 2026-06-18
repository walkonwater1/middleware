# MQTT 学习

MQTT (Message Queuing Telemetry Transport) 是一种轻量级的发布/订阅消息传输协议，广泛应用于 IoT、车联网等场景。

## MQTT 核心概念

- **Broker**: 消息代理服务器，负责接收和转发消息
- **Topic**: 消息主题，发布者向主题发送消息，订阅者订阅主题接收消息
- **QoS**: 服务质量等级 (0: 最多一次, 1: 至少一次, 2: 恰好一次)
- **Retained Message**: 保留消息，新订阅者会收到最后一条保留消息
- **Last Will**: 遗嘱消息，客户端异常断开时自动发送

## 运行环境

```bash
# 安装 Eclipse Paho MQTT C Client
sudo apt install libpaho-mqtt-dev

# 启动 Mosquitto broker (可选, 也可使用公共测试 broker)
# sudo apt install mosquitto mosquitto-clients
# sudo systemctl start mosquitto
```

## 构建 & 运行

```bash
mkdir build && cd build
cmake ..
make

# 终端1: 启动订阅者
./subscriber

# 终端2: 启动发布者
./publisher
```

> 默认使用 `test.mosquitto.org` 公共测试 broker，无需本地部署。

## Demo 说明

| 文件 | 功能 |
|------|------|
| `publisher.c` | 定时发布模拟的车辆传感器数据 (速度、温度、GPS) |
| `subscriber.c` | 订阅所有传感器主题并打印接收到的数据 |
| `CMakeLists.txt` | CMake 构建脚本, 通过 pkg-config 查找 paho-mqtt |

## 关键 API

```c
// 创建客户端 (异步模式)
MQTTAsync_create(&client, address, client_id, MQTTCLIENT_PERSISTENCE_NONE, NULL);

// 设置消息回调
MQTTAsync_setCallbacks(client, NULL, NULL, on_message, NULL);

// 连接
MQTTAsync_connect(client, &conn_opts);

// 发布
MQTTAsync_sendMessage(client, topic, &msg, &pub_opts);

// 订阅
MQTTAsync_subscribeMany(client, count, topics, qos_array, &sub_opts);
```

## 进阶学习方向

- [ ] QoS 等级对延迟/可靠性的影响
- [ ] 同步 API (MQTTAsync vs MQTTClient) 区别
- [ ] MQTT Bridge 跨 broker 通信
- [ ] TLS/SSL 安全通信
- [ ] 大规模 Topic 下的性能测试
- [ ] MQTT v5 新特性 (Session Expiry, Topic Alias 等)
