# Communication Middleware Learning

> 车载与机器人通讯中间件学习仓库

本仓库用于学习和对比车载/机器人领域主流通讯中间件，通过实践掌握各自的原理、适用场景和最佳实践。

## 中间件列表

| 中间件 | 类型 | 典型场景 | 语言 |
|--------|------|----------|------|
| [MQTT](./mqtt/) | 发布/订阅 (Broker) | IoT、车联网、遥测数据传输 | C |
| [ZeroMQ](./zmq/) | 多种模式 (无Broker) | 进程间高性能通信、分布式系统 | C++ |
| [GDBus](./gdbus/) | RPC / 信号 | Linux 桌面/嵌入式系统服务间通信 | C |
| [ROS2](./ros2/) | 发布/订阅 + 服务 | 机器人操作系统核心通信层 | C++ |
| [DDS](./dds/) | 发布/订阅 (去中心化) | 自动驾驶、军工、实时分布式系统 | C |
| [SOME/IP](./someip/) | 服务导向 (Service Discovery) | 车载以太网 ECU 间通信 | C++ |
| [CANopen](./canopen/) | 基于 CAN 的应用层协议 | 机器人关节电机/传感器通信 | C++ |
| [EtherCAT](./ethercat/) | 工业实时以太网 | 机器人多轴同步 + 分布式时钟 | C |
| [共享内存](./shm/) | 共享内存映射 | 超低延迟IPC、大块数据传输、零拷贝 | C |

## 目录结构

```
communication-middleware-learning/
├── mqtt/           # MQTT 发布/订阅示例 (C + Paho)
├── zmq/            # ZeroMQ 多模式示例 (C++ + cppzmq)
├── gdbus/          # GDBus 服务端/客户端示例 (C + GLib)
├── ros2/           # ROS2 发布者/订阅者 (C++)
├── dds/            # DDS 发布/订阅示例 (C + CycloneDDS)
├── someip/         # SOME/IP 车载服务通信 (C++ + vsomeip)
├── canopen/        # CANopen 应用层协议 (C++ + 虚拟CAN)
├── ethercat/       # EtherCAT 工业实时以太网 (C + 虚拟ESC)
├── shm/            # 共享内存 IPC (C + POSIX shm + 信号量)
└── README.md
```

## 构建方式

所有子项目均使用 **CMake** 构建（GDBus 使用 Makefile）：

```bash
# MQTT (C)
cd mqtt && mkdir build && cd build && cmake .. && make

# ZMQ (C++)
cd zmq && mkdir build && cd build && cmake .. && make

# GDBus (C)
cd gdbus && make

# ROS2 (C++)
cd ros2/cpp_pubsub && colcon build

# DDS (C)
cd dds && mkdir build && cd build && cmake .. && make

# SOME/IP (C++)
cd someip && mkdir build && cd build && cmake .. && make

# CANopen (C++)
cd canopen && mkdir build && cd build && cmake .. && make

# EtherCAT (C)
cd ethercat && mkdir build && cd build && cmake .. && make

# 共享内存 (C)
cd shm && mkdir build && cd build && cmake .. && make
```

## 学习路线

1. **基础入门** — 运行各文件夹中的基础 demo，理解每种中间件的核心通信模式
2. **性能对比** — 对延迟、吞吐量、CPU 占用进行横向对比测试
3. **可靠性分析** — 研究 QoS 策略、断线重连、消息持久化等机制
4. **源码剖析** — 深入阅读各中间件的核心实现
5. **实战优化** — 结合车载/机器人真实场景进行调优

## 环境要求

- CMake 3.10+
- GCC/G++ (C11/C++17)
- ROS2 Humble
- 各子目录 README 中包含详细的依赖安装说明

## 许可证

MIT License
