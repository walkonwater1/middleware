# MQTT 学习

MQTT (Message Queuing Telemetry Transport) 是一种轻量级的发布/订阅消息传输协议，广泛应用于 IoT、车联网等场景。

## MQTT 核心概念

- **Broker**: 消息代理服务器，负责接收和转发消息
- **Topic**: 消息主题，发布者向主题发送消息，订阅者订阅主题接收消息
- **QoS**: 服务质量等级 (0: 最多一次, 1: 至少一次, 2: 恰好一次)
- **Retained Message**: 保留消息，新订阅者会收到最后一条保留消息
- **Last Will**: 遗嘱消息，客户端异常断开时自动发送

---

## 项目结构

```
mqtt/
├── CMakeLists.txt                    # 顶层构建脚本 (C++17)
├── README.md
│
├── cmake/
│   └── FindPahoMqttC.cmake           # Find 模块 (third-lib 优先)
│
├── include/
│   └── mqtt_can/                     # 公共头文件 (C++ 接口)
│       ├── can_database.hpp
│       ├── can_simulator.hpp
│       ├── can_parser.hpp
│       └── mqtt_client.hpp
│
├── src/
│   ├── lib/                          # 库源码 → libmqtt_can.a
│   │   ├── can_database.cpp
│   │   ├── can_simulator.cpp
│   │   ├── can_parser.cpp
│   │   └── mqtt_client.cpp
│   ├── mqtt_can_publisher.cpp
│   └── mqtt_can_subscriber.cpp
│
├── demo/                             # 简单 Demo (C)
│   ├── publisher.c
│   └── subscriber.c
│
├── proto/
│   └── vehicle_can.proto
│
├── scripts/
│   ├── build_deps.sh                 # 交叉编译第三方库 → third-lib/
│   └── generate_proto.sh
│
└── third-lib/                        # 预置第三方库产物 (无需板子安装)
    ├── paho-mqtt/
    │   ├── include/                  #   MQTTAsync.h ...
    │   └── lib/                      #   libpaho-mqtt3as.a
    └── protobuf-c/
        ├── include/                  #   protobuf-c.h ...
        └── lib/                      #   libprotobuf-c.a
```

---

## 方式一：使用 third-lib (推荐 — 无需在板子上装依赖)

> 在 x86_64 构建机上交叉编译，产物放入 `third-lib/`，板子零依赖运行。

### 1. 在构建机上交叉编译第三方库

```bash
# 安装交叉工具链 (以 aarch64 为例)
sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu

# 一键编译 paho-mqtt + protobuf-c → third-lib/
bash scripts/build_deps.sh aarch64
```

产物自动放入 `third-lib/`:
```
third-lib/
├── paho-mqtt/{include/, lib/libpaho-mqtt3as.a}
└── protobuf-c/{include/, lib/libprotobuf-c.a}
```

### 2. 交叉编译本项目

```bash
mkdir build && cd build
cmake .. \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
    -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
    -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++
make -j$(nproc)

# 拷贝到板子 (只复制可执行文件，不需要任何 .so)
scp mqtt_can_publisher mqtt_can_subscriber user@board:/home/user/
```

### 3. 在 ARM 板子上直接运行

```bash
# 无需 apt install 任何东西！
./mqtt_can_publisher
./mqtt_can_subscriber
```

---

## 方式二：板子上直接安装依赖

```bash
sudo apt install libpaho-mqtt-dev protobuf-c-compiler libprotobuf-c-dev
mkdir build && cd build && cmake .. && make -j$(nproc)
```

---

## 构建 & 运行

### 本地编译 (x86_64 / ARM 板载)

```bash
# 配置 & 构建
mkdir build && cd build
cmake ..
make -j$(nproc)

# 终端1: 发布 CAN 数据
./mqtt_can_publisher [vehicle_id]

# 终端2: 订阅并解码展示
./mqtt_can_subscriber
```

### ARM 交叉编译

```bash
# 以 aarch64 为例，需提前安装交叉工具链:
# sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu

mkdir build_arm && cd build_arm
cmake .. \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
    -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
    -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++
make -j$(nproc)

# 将可执行文件拷贝到 ARM 板子上运行
scp mqtt_can_publisher mqtt_can_subscriber user@board:/home/user/
```

> 默认使用 `test.mosquitto.org` 公共测试 broker，无需本地部署。

### 简单 Demo (原始版本)

```bash
mkdir build && cd build
cmake ..
make

# 终端1: 发布者
./publisher

# 终端2: 订阅者
./subscriber
```

### CMake 选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `BUILD_CAN_DEMO` | ON | 构建 CAN+MQTT+Protobuf 程序 |
| `BUILD_SIMPLE_DEMO` | ON | 构建简单 JSON Demo |

```bash
cmake .. -DBUILD_CAN_DEMO=OFF
```

---

## 数据流架构

```
┌──────────────┐    原始CAN帧     ┌──────────────┐
│ CanSimulator │ ───────────────→ │  CanParser   │
│ (CAN总线模拟) │  .can_id, .data │ (信号解析器)  │
└──────────────┘                  └──────┬───────┘
                                         │
                          物理信号 float  │
                                         ▼
                                  ┌──────────────┐
                                  │  Protobuf    │
                                  │  (protobuf-c)│
                                  └──────┬───────┘
                                         │
                              二进制 bytes  │
                                         ▼
                                  ┌──────────────┐
                                  │  MQTT Broker │
                                  │(test.mosquitto.org)
                                  └──┬───┬───┬───┘
                                     │   │   │
                         vehicle/can/│   │   │vehicle/can/json
                              /raw   │   │   │
                                     ▼   ▼   ▼
                                  ┌──────────────┐
                                  │  Subscriber  │
                                  │ (protobuf解码)│
                                  └──────────────┘
```

---

## 模拟的 CAN 信号

| CAN ID | 报文名 | 周期 | 信号 | 范围 | 单位 |
|--------|--------|------|------|------|------|
| 0x100 | PowerTrain | 10ms | EngineSpeed | 0~16000 | rpm |
| | | | VehicleSpeed | 0~500 | km/h |
| | | | BrakeSwitch | 0/1/2 | — |
| 0x200 | ThermalElectric | 100ms | CoolantTemp | -40~215 | °C |
| | | | BatteryVoltage | 0~65 | V |
| | | | FuelLevel | 0~100 | % |
| 0x300 | DriverControl | 20ms | ThrottlePosition | 0~100 | % |
| | | | GearPosition | P/R/N/D/S | — |
| 0x400 | Steering | 10ms | SteeringAngle | -780~780 | deg |
| | | | SteeringRate | 0~1016 | deg/s |
| 0x500 | Instrument | 100ms | Odometer | 0~1e6 | km |

---

## MQTT Topic 定义

| Topic | 格式 | 说明 |
|-------|------|------|
| `vehicle/can/raw` | Protobuf `CanFrame` | 原始 CAN 帧 (二进制) |
| `vehicle/can/signals` | Protobuf `VehicleSignals` | 解析后的物理信号 (二进制) |
| `vehicle/can/json` | JSON | 解析后的信号 (人类可读, 调试用) |

---

## 核心模块 API

### CanDatabase — CAN 信号数据库

```cpp
#include "mqtt_can/can_database.hpp"

using namespace mqtt_can;

// 初始化默认数据库 (5帧12信号)
CanDatabase::instance().init_default();
CanDatabase::instance().print();

// 查找
const CanMessage* msg = CanDatabase::instance().find_message(0x100);
const CanSignal* sig = CanDatabase::instance().find_signal("VehicleSpeed");

// 解析
uint64_t raw = extract_bits(data, sig->start_bit, sig->length, ByteOrder::Intel);
float speed = sig->decode(raw);
```

### CanSimulator — CAN 总线模拟器

```cpp
#include "mqtt_can/can_simulator.hpp"

CanSimulator sim(/*cold_start=*/false);   // false=行驶状态, true=冷启动

auto frames = sim.tick(8);                // 按周期获取帧列表

const auto& state = sim.state();
std::cout << state.vehicle_speed << " km/h\n";
```

### CanParser — CAN 帧解析器

```cpp
#include "mqtt_can/can_parser.hpp"

CanParser parser("VIN_LX_TEST_001");

parser.feed(frame);

auto speed = parser.get("VehicleSpeed");
if (speed) {
    std::printf("车速: %.1f km/h\n", *speed);
}

parser.print_snapshot();
```

### MqttClient — MQTT RAII 封装

```cpp
#include "mqtt_can/mqtt_client.hpp"

MqttClient::Config cfg;
cfg.broker_address = "tcp://localhost:1883";
cfg.client_id = "my-client";

MqttClient mqtt(cfg);
mqtt.set_on_message([](const std::string& topic, const uint8_t* data, size_t len) {
    std::printf("[%s] %zu bytes\n", topic.c_str(), len);
});

mqtt.connect();
mqtt.subscribe({"vehicle/can/signals", "vehicle/can/json"});
mqtt.publish("vehicle/test", "hello", 5);

// ... 保持运行 ...

mqtt.disconnect(); // RAII 自动清理
```

---

## 关键 API (Paho MQTT C)

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

---

## 进阶学习方向

- [x] CAN 数据模拟 + 信号解析 (DBC-like)
- [x] Protobuf 序列化上传 vs JSON
- [ ] QoS 等级对延迟/可靠性的影响
- [ ] MQTT v5 新特性 (Session Expiry, Topic Alias 等)
- [ ] TLS/SSL 安全通信
- [ ] 大规模 Topic 下的性能测试
- [ ] 对接真实 CAN 硬件 (socketcan / PCAN / Kvaser)
- [ ] MQTT Bridge 跨 broker 通信
- [ ] 数据落盘 + 回放 (CAN 原始帧存储)
