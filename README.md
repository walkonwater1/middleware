# 通信中间件学习

> 车载与机器人通讯中间件学习仓库 — 11 种中间件覆盖从底层 IPC 到云端 IoT、从 Web 实时通信到云原生 RPC 的完整通信技术栈

## 动机

车载和机器人系统需要多种通信机制各司其职：关节电机通过实时总线同步、ECU 间通过服务发现协商、传感器数据通过零拷贝共享、远程状态通过 MQTT 上报云端。本仓库通过可运行的 Demo 逐一实践每种中间件，理解其设计取舍和适用边界。

## 中间件全景

### 实时总线 (硬实时、确定性)

| 中间件 | 定位 | 确定性 | 典型带宽 |
|--------|------|--------|---------|
| **[EtherCAT](./ethercat/)** | 工业实时以太网，多轴同步 | μs 级 DC 时钟 | 100 Mbps |
| **[CANopen](./canopen/)** | 基于 CAN 的应用层协议 | ms 级 | 1 Mbps |

**EtherCAT** — "总线即中间件"。帧在从站间"飞读飞写"，主站 1kHz 扫描 3 轴伺服，分布式时钟 <100ns 同步。CoE (CANopen over EtherCAT) 让 CANopen 对象字典运行在 EtherCAT 物理层上。

**CANopen** — 机器人关节的"领域语言"。PDO 循环推送电机状态、SDO 读写配置参数、NMT 管理从机生命周期、Heartbeat + EMCY 监控健康。虚拟 CAN 总线演示，无需真实硬件。

### 车载 SOA (服务导向)

| 中间件 | 定位 | 通信模式 | 传输层 |
|--------|------|---------|--------|
| **[SOME/IP](./someip/)** | AUTOSAR 车载以太网 | Service Discovery + RPC | UDP/TCP |
| **[DDS](./dds/)** | 去中心化实时数据分发 | Pub/Sub (P2P, 无 Broker) | UDP 组播 |

**SOME/IP** — AUTOSAR Adaptive 平台标准。Service Discovery 动态发现、Request/Response RPC 远程调用、Fire&Forget 事件推送。演示车载服务端发布车门/车速事件、提供车窗控制方法、客户端自动发现并订阅。

**DDS** — OMG 国际标准，ROS2 底层。11 个 Lab 从 Pub/Sub 入门到 QoS/Listener/Security/Benchmark 全覆盖。去中心化无 Broker，SPDP/SEDP 自动发现，20+ QoS 策略组合。

### 云原生 RPC

| 中间件 | 定位 | 通信模式 | 传输层 |
|--------|------|---------|--------|
| **[gRPC](./grpc/)** | 高性能 RPC 框架 | Unary + Client/Server/Bidi Stream | HTTP/2 (TCP) |

**gRPC** — Google 开源的云原生 RPC 标准。IDL (Protobuf) 定义接口 → 自动生成类型安全的 Client/Server 桩代码 → HTTP/2 多路复用 + 双向流。演示 Unary RPC (一问一答)、Server Streaming (遥测推送)、Client Streaming (进度上报) 三种模式，6 轴机械臂状态模拟。

### 机器人中间件

| 中间件 | 定位 | 通信模式 | 语言 |
|--------|------|---------|------|
| **[ROS2](./ros2/)** | 机器人操作系统通信层 | Pub/Sub + Service + Action | C++ (rclcpp) |

**ROS2** — 建立在 DDS 之上的机器人集成框架。16 个 Lab 覆盖 Topic/Service/Action/QoS，延伸到 TF2 坐标变换、Rosbag2 录制回放、Loan Message 零拷贝、多机自动发现、DDS 供应商切换、MQTT 桥接。`dds_bridge` 验证 ROS2 底层即 DDS。

### 进程间通信 (IPC)

| 中间件 | 定位 | 延迟 | 拷贝次数 |
|--------|------|------|---------|
| **[共享内存](./shm/)** | POSIX 共享内存映射 | ~ns 级 | 0 (零拷贝) |
| **[ZeroMQ](./zmq/)** | 高性能异步消息库 | ~μs 级 | 1 |
| **[GDBus](./gdbus/)** | Linux 桌面/嵌入式 IPC | ~μs 级 | 1 |

**共享内存** — IPC 延迟下限。`mmap` 映射同一物理页，A 进程写入 B 进程立即可见。信号量保护 + 无锁环形缓冲 SPMC 队列。这是 ROS2 Loan Message 和 iceoryx 的底层原理。

**ZeroMQ** — "不是消息队列，是没有 Broker 的消息库"。REQ/REP 车辆诊断 RPC、PUB/SUB 机器人传感器广播。4 种模式，无需中心节点。

**GDBus** — Linux 系统总线的 IPC 标准。XML 接口定义 → gdbus-codegen + Python 代码生成 → C++17 RAII 封装。机器人状态管理服务 + 监控终端，方法/信号/属性齐全。

### 车联网 / IoT (云-边-端)

| 中间件 | 定位 | 通信模式 | 传输层 |
|--------|------|---------|--------|
| **[MQTT](./mqtt/)** | IoT 标准消息协议 | Pub/Sub (有 Broker) | TCP |

**MQTT** — 车载终端到云端的桥梁。模拟 5 路 CAN 信号 → Protobuf 序列化 → MQTT 发布，支持 MQTT v5 和 GB/T 32960 国标电动车远程监控协议。aarch64 交叉编译支持车载嵌入式部署。

### Web 实时通信

| 中间件 | 定位 | 通信模式 | 传输层 |
|--------|------|---------|--------|
| **[WebSocket](./websocket/)** | 全双工实时 Web 通信 | 双向 (Text/Binary Frame) | TCP (HTTP Upgrade) |

**WebSocket** — 浏览器到后端唯一的原生双向通道。TCP 连接经 HTTP Upgrade 后自由收发，无需轮询。演示多客户端并发管理、1s 广播遥测 JSON、命令分发 (status/echo/cmd)、Ping/Pong 心跳。适用于机器人的 Web 仪表盘和 HMI 实时监控。

## 选型决策矩阵

| 场景 | 推荐中间件 | 原因 |
|------|-----------|------|
| 机器人关节多轴同步控制 | EtherCAT / CANopen | 硬实时、分布式时钟、确定性延迟 |
| ECU 间服务调用 (车门/车窗) | SOME/IP | Service Discovery、AUTOSAR 标准 |
| 自动驾驶传感器数据分发 | DDS (ROS2) | 去中心化、QoS 可配、RTPS 实时 |
| 机器人上层算法集成 | ROS2 | TF2/rosbag2/Lifecycle 完整工具链 |
| 激光雷达点云进程间传输 | 共享内存 | 零拷贝、GB/s 级吞吐 |
| 微服务间异步消息 | ZeroMQ | 无 Broker、灵活拓扑 |
| Linux 系统服务管理 | GDBus (D-Bus) | systemd/NetworkManager 原生协议 |
| ECU 间远程方法调用 (诊断/自检) | gRPC | IDL 类型安全、HTTP/2 多路复用、双向流 |
| 机器人 Web 仪表盘实时监控 | WebSocket | 浏览器原生支持、全双工、低开销帧 |
| 车辆远程监控/OTA | MQTT | 穿透防火墙、低带宽、QoS 分级 |

## 目录结构

```
middleware/
├── ethercat/                   # EtherCAT 工业实时以太网 (1 kHz, 3 轴)
├── canopen/                    # CANopen 应用层协议 (PDO/SDO/NMT/EMCY)
├── someip/                     # SOME/IP 车载 SOA (vsomeip)
├── dds/                        # DDS 去中心化 Pub/Sub (11 Lab)
├── grpc/                       # gRPC 高性能 RPC (3 模式: Unary/Server Stream/Client Stream)
├── ros2/                       # ROS2 机器人集成框架 (14 Lab)
├── shm/                        # 共享内存 IPC (零拷贝)
├── zmq/                        # ZeroMQ 高性能消息库 (2 模式)
├── gdbus/                      # GDBus/D-Bus Linux IPC
├── websocket/                  # WebSocket 全双工 Web 通信 (多客户端广播 + 命令分发)
├── mqtt/                       # MQTT IoT 协议 (CAN→Protobuf→MQTT)
└── README.md
```

## 学习路线

按通信层次从底层到上层逐步推进：

1. **IPC 基础** → `shm/` (零拷贝原理) → `zmq/` (多模式消息) → `gdbus/` (系统 IPC)
2. **实时总线** → `canopen/` (CAN 应用层) → `ethercat/` (工业以太网)
3. **车载 SOA** → `someip/` (AUTOSAR 服务发现) → `dds/` (去中心化 Pub/Sub)
4. **机器人集成** → `ros2/` (DDS 之上的完整框架，14 Lab)
5. **云边通信** → `mqtt/` (CAN→云端全链路) → `websocket/` (Web 实时遥测)
6. **微服务 RPC** → `grpc/` (IDL+HTTP/2 多模式 RPC)

## 构建速查

所有子项目均使用 CMake 构建：

```bash
# EtherCAT         依赖: librt, libpthread
cd ethercat && mkdir -p build && cd build && cmake .. && make -j$(nproc)

# CANopen          依赖: 无 (需 can-utils + vcan 内核模块)
cd canopen && mkdir -p build && cd build && cmake .. && make -j$(nproc)

# SOME/IP          依赖: libvsomeip3-dev
cd someip && mkdir -p build && cd build && cmake .. && make -j$(nproc)

# DDS              依赖: cyclonedds-dev
cd dds && mkdir -p build && cd build && cmake .. && make -j$(nproc)

# ROS2             依赖: /opt/ros/humble/setup.bash
cd ros2 && source /opt/ros/humble/setup.bash
colcon build --packages-select topic_lab service_lab action_lab qos_lab \
  lifecycle_lab composition_lab param_lab launch_lab tf2_lab rosbag2_lab \
  loan_lab multimachine_lab dds_vendor_lab mqtt_lab dds_bridge

# 共享内存         依赖: librt
cd shm && mkdir -p build && cd build && cmake .. && make -j$(nproc)

# ZeroMQ           依赖: libzmq3-dev, cppzmq
cd zmq && mkdir -p build && cd build && cmake .. && make -j$(nproc)

# GDBus            依赖: libglib2.0-dev, gdbus-codegen, python3
cd gdbus && mkdir -p build && cd build && cmake .. && make -j$(nproc)

# MQTT             依赖: libpaho-mqtt-dev, protobuf-c-compiler
cd mqtt && mkdir -p build && cd build && cmake .. && make -j$(nproc)

# gRPC             依赖: libgrpc++-dev, protobuf-compiler-grpc
cd grpc && mkdir -p build && cd build && cmake .. && make -j$(nproc)

# WebSocket        依赖: libwebsocketpp-dev, libboost-system-dev, libssl-dev
cd websocket && mkdir -p build && cd build && cmake .. && make -j$(nproc)
```

## 环境要求

- CMake 3.10+
- GCC/G++ (C11/C++17)
- ROS2 Humble (仅 `ros2/` 需要)
- 各子目录 README 中包含详细依赖说明和运行命令

## 许可证

MIT License
