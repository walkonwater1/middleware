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

## 运行环境

```bash
# Ubuntu 22.04 安装 ROS2 Humble
# 参考: https://docs.ros.org/en/humble/Installation.html

# 安装后设置环境
source /opt/ros/humble/setup.bash
```

## 构建 & 运行

```bash
cd cpp_pubsub

# 构建
colcon build --packages-select ros2_demo_pubsub
source install/setup.bash

# 终端1: 启动发布者
ros2 run ros2_demo_pubsub talker

# 终端2: 启动订阅者
ros2 run ros2_demo_pubsub listener
```

## Demo 说明

| 文件 | 功能 |
|------|------|
| `src/publisher.cpp` | 发布者: 发布 String 消息到 `/chatter` 主题 (2Hz), 含计数和时间戳 |
| `src/subscriber.cpp` | 订阅者: 订阅 `/chatter` 主题, 计算端到端延迟 |
| `CMakeLists.txt` | ROS2 ament_cmake 构建脚本 |
| `package.xml` | ROS2 包描述文件 |

## 关键 API (rclcpp)

```cpp
// 创建节点
class Talker : public rclcpp::Node
{
    Talker() : Node("talker") {
        publisher_ = this->create_publisher<std_msgs::msg::String>("/chatter", 10);
        timer_ = this->create_wall_timer(500ms, std::bind(&Talker::callback, this));
    }
};

// 发布消息
auto msg = std_msgs::msg::String();
msg.data = "Hello";
publisher_->publish(msg);

// 订阅消息
subscription_ = this->create_subscription<std_msgs::msg::String>(
    "/chatter", 10, std::bind(&Listener::callback, this, _1));
```

## ROS2 命令行工具速查

```bash
ros2 node list            # 列出所有节点
ros2 topic list           # 列出所有主题
ros2 topic echo /topic    # 查看主题数据
ros2 topic hz /topic      # 查看发布频率
ros2 topic info /topic    # 查看主题信息
ros2 service list         # 列出所有服务
```

## 进阶学习方向

- [ ] QoS 策略对机器人控制的影响
- [ ] 自定义消息类型 (.msg, .srv, .action)
- [ ] 零拷贝传输 (Loan Message)
- [ ] DDS 供应商切换 (FastDDS / CycloneDDS)
- [ ] 多机分布式通信配置
- [ ] ROS2 与 MQTT/DDS Bridge 集成
