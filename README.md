# 通信中间件学习

> 车载与机器人通讯中间件学习仓库

本仓库用于学习和对比车载/机器人领域主流通讯中间件，通过实践掌握各自的原理、适用场景和最佳实践。

## 中间件列表

| 中间件 | 类型 | 典型场景 | 语言 | Lab 数量 |
|--------|------|----------|------|----------|
| [DDS](./dds/) | 去中心化 Pub/Sub | 自动驾驶、军工、实时分布式系统 | C (CycloneDDS) | 11 |
| [ROS2](./ros2/) | Pub/Sub + Service + Action | 机器人操作系统核心通信层 | C++ (rclcpp) | 16 |
| [GDBus](./gdbus/) | RPC + 信号 | Linux 桌面/嵌入式系统服务间通信 | C++17 | 1 |
| [SOME/IP](./someip/) | 服务导向 (Service Discovery) | 车载以太网 ECU 间通信 | C++ (vsomeip) | 1 |
| [CANopen](./canopen/) | 基于 CAN 的应用层协议 | 机器人关节电机/传感器通信 | C++ | 1 |
| [EtherCAT](./ethercat/) | 工业实时以太网 | 机器人多轴同步 + 分布式时钟 | C | 1 |
| [MQTT](./mqtt/) | Broker Pub/Sub | IoT、车联网、遥测数据传输 | C (Paho) | 1 |
| [ZeroMQ](./zmq/) | 多种模式 (无Broker) | 进程间高性能通信、分布式系统 | C++ (cppzmq) | 1 |
| [共享内存](./shm/) | 内存映射 | 超低延迟 IPC、大块数据传输、零拷贝 | C (POSIX) | 1 |

## 目录结构

```
middleware/
├── dds/                       # DDS 学习 (11 Lab)
│   ├── vehicle/               #   9 个车载概念实验
│   ├── robot/                 #   2 个机器人系统 Demo
│   ├── idl/                   #   3 个 IDL 定义
│   ├── scripts/               #   一键启动 + 安全证书生成
│   └── README.md
│
├── ros2/                      # ROS2 学习 (16 Lab)
│   ├── topic_lab/             #   Pub/Sub + 自定义消息
│   ├── service_lab/           #   Service 请求/响应
│   ├── action_lab/            #   Action 异步任务
│   ├── qos_lab/               #   QoS 对比实验
│   ├── lifecycle_lab/         #   Lifecycle 生命周期
│   ├── composition_lab/       #   进程内 Composition
│   ├── param_lab/             #   动态参数
│   ├── launch_lab/            #   Launch 文件编排
│   ├── tf2_lab/               #   TF2 坐标变换
│   ├── rosbag2_lab/           #   Rosbag2 录制/回放
│   ├── loan_lab/              #   Loan Message 零拷贝
│   ├── multimachine_lab/      #   多机分布式通信
│   ├── dds_vendor_lab/        #   DDS 供应商切换
│   ├── mqtt_lab/              #   MQTT 桥接
│   ├── dds_bridge/            #   DDS↔ROS2 桥接验证
│   ├── scripts/               #   一键启动菜单
│   └── README.md
│
├── gdbus/                     # GDBus/D-Bus 学习
│   ├── src/                   #   C++17 服务端+客户端
│   ├── interfaces/            #   D-Bus 接口 XML
│   ├── scripts/               #   代码生成器 + 构建脚本
│   └── README.md
│
├── someip/                    # SOME/IP 车载服务通信
├── canopen/                   # CANopen 应用层协议
├── ethercat/                  # EtherCAT 工业实时以太网
├── mqtt/                      # MQTT Broker 发布/订阅
├── zmq/                       # ZeroMQ 多模式通信
├── shm/                       # 共享内存 IPC
└── README.md
```

## 构建方式

```bash
# DDS
cd dds && mkdir -p build && cd build && cmake .. && make -j$(nproc)

# ROS2
cd ros2
source /opt/ros/humble/setup.bash
colcon build --packages-select topic_lab service_lab action_lab qos_lab \
  lifecycle_lab composition_lab param_lab launch_lab tf2_lab rosbag2_lab \
  loan_lab multimachine_lab dds_vendor_lab mqtt_lab dds_bridge
# 或: bash scripts/run_all_labs.sh → 15) build

# GDBus
cd gdbus && mkdir -p build && cd build && cmake .. && make -j$(nproc)

# SOME/IP
cd someip && mkdir -p build && cd build && cmake .. && make -j$(nproc)

# CANopen
cd canopen && mkdir -p build && cd build && cmake .. && make -j$(nproc)

# EtherCAT
cd ethercat && mkdir -p build && cd build && cmake .. && make -j$(nproc)

# MQTT
cd mqtt && mkdir -p build && cd build && cmake .. && make -j$(nproc)

# ZeroMQ
cd zmq && mkdir -p build && cd build && cmake .. && make -j$(nproc)

# 共享内存
cd shm && mkdir -p build && cd build && cmake .. && make -j$(nproc)
```

## 学习路线

1. **DDS (基础)** — 理解去中心化 Pub/Sub，QoS 策略，Listener 回调 → 为 ROS2 打基础
2. **ROS2 (进阶)** — 从 Topic/Service/Action 到 TF2/Rosbag2/Loan/Multi-machine/DDS Vendor
3. **GDBus (横向对比)** — 同是进程间通信，对比 D-Bus RPC 与 DDS Pub/Sub 的架构差异
4. **车载协议 (SOME/IP / CANopen / EtherCAT)** — 车载 ECU 和机器人关节电机的工业协议
5. **IoT (MQTT / ZeroMQ)** — 云-边-端通信链路
6. **性能极致 (共享内存)** — 核内零拷贝 IPC

## DDS ↔ ROS2 关系

```
DDS (CycloneDDS C API)          ROS2 (rclcpp C++ API)
╔═══════════════════╗          ╔══════════════════╗
║  DataWriter       ║  ─────→ ║  Publisher       ║
║  DataReader       ║  ←───── ║  Subscription    ║
║  Topic            ║  ─────→ ║  Topic           ║
║  QoS              ║  ─────→ ║  QoS             ║
║  Listener         ║  ─────→ ║  spin()          ║
║  Domain           ║  ─────→ ║  ROS_DOMAIN_ID   ║
╚═══════════════════╝          ╚══════════════════╝
        ↑                              ↑
        └──── dds_bridge ──────────────┘
          (同进程验证 DDS=ROS2 底层)
```

## 环境要求

- CMake 3.10+
- GCC/G++ (C11/C++17)
- ROS2 Humble (`/opt/ros/humble/setup.bash`)
- CycloneDDS (`cyclonedds-dev`)
- 各子目录 README 中包含详细的依赖安装说明

## 许可证

MIT License
