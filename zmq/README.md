# ZeroMQ 学习

ZeroMQ (ØMQ) 是一个高性能异步消息库，提供了多种通信模式的"积木"，可以直接在进程间、线程间进行通信，**无需中心化 Broker**。

## ZMQ 核心通信模式

| 模式 | 说明 | 典型场景 |
|------|------|----------|
| REQ/REP | 请求-响应 (同步) | RPC 调用、任务分发 |
| PUB/SUB | 发布-订阅 | 数据广播、日志收集 |
| PUSH/PULL | 推-拉 (管道) | 并行任务处理、负载均衡 |
| ROUTER/DEALER | 路由-经销商 (异步) | 高级异步通信 |
| PAIR | 点对点 | 线程间通信 |

## 运行环境

```bash
# 安装 libzmq 和 cppzmq
sudo apt install libzmq3-dev

# cppzmq 是 header-only, 从 GitHub 获取
git clone https://github.com/zeromq/cppzmq.git
sudo cp cppzmq/zmq.hpp /usr/local/include/
sudo cp cppzmq/zmq_addon.hpp /usr/local/include/
```

## 构建 & 运行

```bash
mkdir build && cd build
cmake ..
make

# ===== REQ/REP 模式 =====
# 终端1: 启动诊断服务端
./req_rep_server

# 终端2: 启动诊断客户端
./req_rep_client

# ===== PUB/SUB 模式 =====
# 终端1: 启动订阅者 (可开多个)
./pub_sub_subscriber

# 终端2: 启动发布者
./pub_sub_publisher
```

## Demo 说明

| 文件 | 模式 | 功能 |
|------|------|------|
| `req_rep/server.cpp` | REQ/REP | 车辆诊断服务: 接收请求返回诊断数据 |
| `req_rep/client.cpp` | REQ/REP | 客户端: 批量查询组件诊断信息 |
| `pub_sub/publisher.cpp` | PUB/SUB | 机器人传感器: 关节角度/IMU/电池 |
| `pub_sub/subscriber.cpp` | PUB/SUB | 订阅者: 前缀匹配订阅 |

## 关键 API (cppzmq)

```cpp
// 创建上下文
zmq::context_t context(1);

// 创建 socket
zmq::socket_t socket(context, ZMQ_REP);  // ZMQ_REQ, ZMQ_PUB, ZMQ_SUB ...

// 绑定/连接
socket.bind("tcp://*:5555");
socket.connect("tcp://localhost:5555");

// 发送/接收 (多帧)
socket.send(zmq::buffer("topic"), zmq::send_flags::sndmore);
socket.send(zmq::buffer(payload), zmq::send_flags::none);

zmq::message_t msg;
socket.recv(msg);
```

## 进阶学习方向

- [ ] 各种模式的延迟/吞吐量对比
- [ ] ZMQ 的零拷贝与消息分帧机制
- [ ] 高水位标记 (HWM) 与背压控制
- [ ] Curve 加密安全通信
- [ ] ZMQ 与 DDS/MQTT 在机器人场景下的性能对比
