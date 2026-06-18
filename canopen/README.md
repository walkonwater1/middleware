# CANopen — 基于 CAN 总线的应用层中间件

> 机器人关节电机 / 传感器最常用的通信协议栈

## 核心概念

CANopen 是 CAN 之上的应用层协议，提供了类似中间件的抽象：

| 抽象 | 类比 | 说明 |
|------|------|------|
| **PDO** (过程数据对象) | Pub/Sub Event | 生产者-消费者模式，最多 8 字节，实时推送 |
| **SDO** (服务数据对象) | RPC Method | 请求-响应模式，读写对象字典，配置参数 |
| **NMT** (网络管理) | Service Discovery | 状态机控制（启动/停止/复位），心跳检测 |
| **EMCY** (紧急报文) | Error Event | 设备异常时主动上报故障码 |
| **对象字典 (OD)** | 数据模型 | 每个设备的标准化参数表（类似 DDS 的 Topic Schema） |

## NMT 状态机

```
        上电/复位
           ↓
       Initialization
           ↓
       Pre-Operational  ←→  Stopped
           ↓
       Operational
```

| 状态 | PDO | SDO | NMT | 说明 |
|------|-----|-----|-----|------|
| Stopped | ✗ | ✗ | ✓ | 不可通信 |
| Pre-Operational | ✗ | ✓ | ✓ | 只允许配置 |
| Operational | ✓ | ✓ | ✓ | 正常工作 |

## COB-ID 分配（CAN-ID 结构）

```
11-bit CAN-ID = Function Code (4 bit) + Node-ID (7 bit)

PDO1(tx): 0x180 + Node-ID
PDO1(rx): 0x200 + Node-ID
PDO2(tx): 0x280 + Node-ID
PDO2(rx): 0x300 + Node-ID
SDO(tx):  0x580 + Node-ID
SDO(rx):  0x600 + Node-ID
NMT:      0x000        (broadcast)
Heartbeat:0x700 + Node-ID
EMCY:     0x080 + Node-ID
```

## 本 Demo 架构

```
┌─────────────────────────────────────────┐
│            canopen_master               │
│  - NMT 命令 (启停、复位)                 │
│  - SDO 读写 (配置电机参数)               │
│  - PDO 接收 (位置/速度/电流)             │
│  - 心跳监控                             │
└──────────┬──────────────────────────────┘
           │  (virtual CAN / vcan0)
┌──────────┴──────────────────────────────┐
│        canopen_node (Slave #1)          │
│  - 对象字典 (位置/速度/转矩/电流)         │
│  - TPDO1: 位置+速度 (1ms 周期)          │
│  - TPDO2: 转矩+电流 (10ms 周期)         │
│  - RPDO1: 目标位置+控制字               │
│  - Heartbeat: 100ms                     │
│  - EMCY: 过流/过温告警                  │
└─────────────────────────────────────────┘
```

## 编译运行

```bash
# 1. 安装依赖
sudo apt install can-utils         # vcan 虚拟 CAN 接口

# 2. 创建虚拟 CAN
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0

# 3. 编译
cd canopen && mkdir build && cd build && cmake .. && make

# 4. 运行（两个终端）
./canopen_node     # 终端1: 启动从站
./canopen_master   # 终端2: 启动主站
```

## PDODemo 运行结果示例

```
[MASTER] NMT → START node #1
[NODE  ] State → OPERATIONAL
[NODE  ] TPDO1 → position:1024 velocity:300
[MASTER] PDO1  ← node#1 pos=1024 vel=300
[MASTER] SDO write → node#1 [0x607A] = 2048 (target pos)
[NODE  ] RPDO1 ← target=2048 ctrl=0x0F
[NODE  ] EMCY → node#1: Over-temperature(0x4001)
```

## 关键文件

| 文件 | 说明 |
|------|------|
| `canopen.hpp` | 对象字典 / PDO 映射 / NMT 状态机定义 |
| `canopen_node.cpp` | 从站实现：PDO 生产、Heartbeat、EMCY |
| `canopen_master.cpp` | 主站实现：NMT 命令、SDO 客户端、PDO 消费 |

## CANopen 在机器人中的应用

| 组件 | COB-ID 用途 | PDO 内容 |
|------|-----------|----------|
| 关节电机 | PDO1/2 | 位置、速度、转矩、电流 |
| IMU | PDO1 | 加速度 xyz、角速度 xyz |
| 编码器 | PDO1 | 多圈位置、转速 |
| 力传感器 | PDO1 | 6 维力/力矩 |
| 急停开关 | EMCY + PDO | 安全回路状态 |

## 进阶学习方向

- [ ] CiA 301（应用层与通讯框架）核心协议阅读
- [ ] CiA 402（驱动器与运动控制）—— 机器人关节最常用
- [ ] PDO 映射优化：利用 8 字节极限打包
- [ ] SYNC 同步帧的 PDO 触发机制
- [ ] 多轴同步：利用 SYNC + PDO 实现分布式时钟
- [ ] CAN 转 ROS2 (ros2_canopen) 桥接实践
- [ ] CAN FD 适配：突破 8 字节限制，支持大数据 PDO
- [ ] 与 SOME/IP 的对比：车载 CAN ↔ 以太网网关设计

## 参考资源

- CiA 301 v4.2.0 — CANopen Application Layer and Communication Profile
- CiA 402 — Device Profile for Drives and Motion Control
- [CANopenNode](https://github.com/CANopenNode/CANopenNode) — 开源 CANopen 协议栈
