# ROS2 学习

ROS2 (Robot Operating System 2) 是机器人领域的标准中间件，底层基于 DDS 实现，提供发布/订阅、服务调用、Action 等通信模式。

---

## ROS1 → ROS2 → 定制中间件 演进对比

### 为什么会有 ROS2 ?

| 维度 | ROS1 | ROS2 |
|------|------|------|
| **通信层** | 自研 TCPROS (基于 TCP) | 标准 DDS (基于 UDP 组播) |
| **拓扑结构** | 中心化 Master (单点故障) | 去中心化 P2P (无 Master) |
| **发现机制** | ROS Master 集中注册 | DDS SPDP/SEDP 自动发现 |
| **QoS** | 不支持 (尽最大努力) | 完善 QoS 策略 (Reliable/BestEffort/TransientLocal 等) |
| **实时性** | 毫秒级, 无确定性 | 微秒级, 可配置 Deadline/Liveliness |
| **安全性** | 无加密/认证 | DDS Security (TLS + ACL) |
| **跨平台** | 仅 Linux | Linux / Windows / macOS / QNX / VxWorks |
| **消息序列化** | 自定义格式 | CDR/XCDR 标准编码 |
| **多机通信** | 需手动配置 ROS_MASTER_URI | 同一 DDS Domain 自动发现 |
| **定位** | 学术/原型开发 | 产品级 / 可量产 |

**核心结论**: ROS1 是科研性质的工具（Master 挂掉整个系统瘫痪、无法选择可靠性级别、不支持真时实系统），ROS2 通过引入 DDS 解决了这些问题，使得 ROS 生态可以支撑量产项目。

---

### 大厂为什么仍自研中间件？

尽管 ROS2 已经成熟，但在汽车/工业控制领域，一线大厂仍投入大量资源自研中间件。原因如下：

| 需求 | ROS2 的限制 | 自研中间件的做法 |
|------|------------|----------------|
| **硬实时 (<1ms 确定性)** | DDS 依赖 UDP 和系统调度, 难以保证 μs 级确定性 | 共享内存 + 时间触发调度 + 中断级别精确控制 |
| **MCU 资源 (RAM <2MB)** | ROS2/DDS 栈需要 100MB+ RAM, 只能在 MPU 上运行 | 裁剪协议栈, 静态内存分配, 可在 Cortex-M 上运行 |
| **ISO 26262 ASIL 认证** | DDS 实现未经过功能安全认证 | 全链路按照 ASIL-B/D 开发, 每条代码可追溯 |
| **AUTOSAR 兼容** | ROS2 不是 AUTOSAR 标准组件 | 实现 AUTOSAR AP ara::com 接口, 与车辆其他 ECU 互通 |
| **数据主权** | 核心通信能力依赖第三方开源项目 | 完全自主可控, 不受开源维护者变更影响 |
| **确定性调度** | 无法精确控制消息的发送时刻和顺序 | 时间感知调度 (Time-Aware Scheduling), TSN 集成 |

**典型自研案例**:
- **Apex.OS / Apex.Middleware**: 基于 ROS2 接口但重写底层, 通过 ISO 26262 ASIL-D 认证
- **Fusa**: 功能安全相关的中间件层, 独立于 ROS2
- **车载 SOA (Some/IP + DDS)**: 自适应 AUTOSAR 平台上同时运行 Some/IP 和 DDS, 针对不同域使用不同协议

---

## ROS2 核心概念

| 概念 | 说明 |
|------|------|
| Node | 最小执行单元, 一个进程可包含多个 Node |
| Topic | 发布/订阅的主题, 有固定的消息类型 |
| Publisher | 发布者, 向 Topic 发送消息 |
| Subscriber | 订阅者, 从 Topic 接收消息 |
| Service | 同步的请求/响应 RPC |
| Action | 带反馈的长时间任务 (可取消) |
| QoS | 服务质量配置 (可靠性、持久性、历史深度等) |

---

## 项目结构

```
ros2/
├── README.md
├── cpp_pubsub/                      # 简单 String 消息 Demo (已有)
│
├── custom_msg/                      # ★ 1. 自定义消息 Topic
│   ├── msg/VehicleState.msg           ROS2 .msg ← 对应 DDS VehicleState.idl
│   └── src/
│       ├── vehicle_publisher.cpp      每 500ms 发布车辆状态
│       └── vehicle_subscriber.cpp     回调接收 + 计算延迟
│
├── service_lab/                     # ★ 2. Service 请求/响应
│   ├── srv/BatteryQuery.srv           ROS2 Service ← Req+Rep 双 Topic
│   └── src/
│       ├── battery_server.cpp         维护电池状态, 响应查询
│       └── battery_client.cpp         每 3s 查询, 异步回调
│
├── action_lab/                      # ★ 3. Action 异步任务
│   ├── action/NavigateTo.action       ROS2 Action ← 5 Topic 实现
│   └── src/
│       ├── navigate_server.cpp        模拟导航 5s, 每 200ms 反馈进度
│       └── navigate_client.cpp        发送目标 → 收反馈 → 收结果
│
├── qos_lab/                         # ★ 4. QoS 对比实验
│   └── src/
│       ├── qos_publisher.cpp          Reliable/BestEffort/TransientLocal
│       └── qos_subscriber.cpp         丢帧检测 + 延迟对比
│
└── dds_bridge/                      # DDS ↔ ROS2 双工桥接 (已有)
```

---

## DDS ↔ ROS2 桥接

ROS2 底层默认使用 CycloneDDS，意味着原生 DDS 程序和 ROS2 节点可以直接通信：

```
原生 DDS Publisher              DDS Domain 0
(dds/publisher.c)   ───→  Topic: "VehicleState"
                                    │
                             ┌──────┴──────┐
                             │ dds_ros2_bridge │
                             │  ↑ CycloneDDS C API
                             │  ↓ rclcpp        │
                             └──────┬──────┘
                                    │
                             ROS2 Topic: "/vehicle/state"
                                    │
                             ros2 topic echo /vehicle/state
```

### 验证方式

```bash
# 1. 编译 DDS (生成 VehicleState.h)
cd dds && mkdir build && cd build
cmake .. && make

# 2. 编译 ROS2
cd ros2
colcon build --packages-select dds_bridge
source install/setup.bash

# 3. 终端1: 原生 DDS 发布者
./dds/build/publisher

# 4. 终端2: 桥接节点
ros2 run dds_bridge dds_ros2_bridge

# 5. 终端3: 看到 DDS 数据出现在 ROS2!
ros2 topic echo /vehicle/state
```

**重点**: `dds_ros2_bridge` 在同一个进程里同时调用 CycloneDDS C API 和 rclcpp，证明了 ROS2 就是 DDS。

---

## 运行环境

```bash
# Ubuntu 22.04 安装 ROS2 Humble
source /opt/ros/humble/setup.bash
```

## 构建 & 运行 (cpp_pubsub)

```bash
cd cpp_pubsub
colcon build --packages-select ros2_demo_pubsub
source install/setup.bash

# 终端1: 发布者
ros2 run ros2_demo_pubsub talker

# 终端2: 订阅者
ros2 run ros2_demo_pubsub listener
```

## 关键 API (rclcpp)

```cpp
class Talker : public rclcpp::Node {
    Talker() : Node("talker") {
        publisher_ = this->create_publisher<MsgType>("/topic", 10);
        timer_ = this->create_wall_timer(500ms, std::bind(&Talker::cb, this));
    }
};
```

## ROS2 命令行速查

```bash
ros2 node list            # 列出所有节点
ros2 topic list           # 列出所有主题
ros2 topic echo /topic    # 查看主题数据
ros2 topic hz /topic      # 查看发布频率
ros2 topic info /topic    # 查看主题信息
ros2 service list         # 列出所有服务
```

## 构建 & 运行

### custom_msg
```bash
ros2 run custom_msg vehicle_publisher
ros2 run custom_msg vehicle_subscriber        # 另一个终端
```

### service_lab
```bash
ros2 run service_lab battery_server
ros2 run service_lab battery_client           # 另一个终端
ros2 service call /battery/query service_lab/srv/BatteryQuery  # CLI 查询
```

### action_lab
```bash
ros2 run action_lab navigate_server
ros2 run action_lab navigate_client           # 另一个终端
ros2 action send_goal /navigate action_lab/action/NavigateTo "{target_x: 20, target_y: 15}"
```

### qos_lab
```bash
# 实验 A: Reliable 不丢帧
ros2 run qos_lab qos_subscriber --ros-args -p qos_mode:=reliable
ros2 run qos_lab qos_publisher --ros-args -p qos_mode:=reliable

# 实验 B: BestEffort 可能丢帧
ros2 run qos_lab qos_subscriber --ros-args -p qos_mode:=besteffort
ros2 run qos_lab qos_publisher --ros-args -p qos_mode:=besteffort

# 实验 C: TransientLocal (先 pub 再 sub 也能拿到历史)
ros2 run qos_lab qos_publisher --ros-args -p qos_mode:=transient  # 先发
ros2 run qos_lab qos_subscriber --ros-args -p qos_mode:=transient  # 后接
```

## 进阶学习方向

- [x] DDS ↔ ROS2 桥接 (同进程 CycloneDDS + rclcpp)
- [x] 自定义消息类型 (.msg)
- [x] Service 请求/响应 (.srv)
- [x] Action 异步任务+反馈 (.action)
- [x] QoS 策略对比 (Reliable/BestEffort/TransientLocal)
- [ ] 零拷贝传输 (Loan Message)
- [ ] DDS 供应商切换 (FastDDS / CycloneDDS)
- [ ] 多机分布式通信配置
- [ ] ROS2 与 MQTT Bridge 集成
