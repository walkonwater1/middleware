# ROS2 学习

ROS2 (Robot Operating System 2) 是机器人领域的标准中间件，底层基于 DDS 实现，提供发布/订阅、服务调用、Action 等通信模式。

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
