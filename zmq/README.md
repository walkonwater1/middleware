# ZeroMQ — 高性能异步消息库

ZeroMQ (ØMQ) 是一个高性能异步消息库，提供了多种通信模式的"积木"，可以直接在进程间、线程间进行通信，**无需中心化 Broker**。

## 文件结构

```
zmq/
├── CMakeLists.txt
├── README.md
├── req_rep/
│   ├── server.cpp                # REQ/REP 服务端 — 车辆诊断
│   └── client.cpp                # REQ/REP 客户端
├── pub_sub/
│   ├── publisher.cpp             # PUB/SUB 发布者 — 传感器数据广播
│   └── subscriber.cpp            # PUB/SUB 订阅者 — 前缀匹配过滤
├── push_pull/
│   ├── producer.cpp              # PUSH/PULL 生产者 — 任务分发
│   └── worker.cpp                # PUSH/PULL 工作节点 — round-robin 负载均衡
├── router_dealer/
│   ├── server.cpp                # ROUTER/DEALER 服务端 — 异步多客户端诊断
│   └── client.cpp                # ROUTER/DEALER 客户端 — 批量异步查询
├── pair/
│   ├── pair_node_a.cpp           # PAIR 独占对 — 双向状态同步
│   └── pair_node_b.cpp           # PAIR 独占对 — 双向状态同步
├── scripts/
│   └── run_demo.sh               # 一键编译 + 运行所有模式
└── build/                        # CMake 构建产物
```

## ZMQ 5 种核心通信模式

```
┌──────────────────────────────────────────────────────────────┐
│                      ZMQ 通信模式总览                          │
├────────────┬──────────┬──────────┬────────────┬──────────────┤
│   模式     │  拓扑    │  同步性  │  典型场景   │  Demo        │
├────────────┼──────────┼──────────┼────────────┼──────────────┤
│ REQ/REP    │ 1→1      │ 同步锁步 │ RPC 调用    │ 车辆诊断      │
│ PUB/SUB    │ 1→N      │ 异步单向 │ 数据广播    │ 传感器数据    │
│ PUSH/PULL  │ 1→N      │ 异步单向 │ 管道/负载均衡│ 任务分发     │
│ ROUTER/    │ N↔1      │ 异步双向 │ 高级异步通信 │ 并发诊断     │
│  DEALER    │          │          │            │              │
│ PAIR       │ 1↔1      │ 异步双向 │ 独占通道    │ 状态同步     │
└────────────┴──────────┴──────────┴────────────┴──────────────┘
```

### REQ/REP — 请求-响应

```
 Client                  Server
   │                       │
   │──── diag_query ──────→│  send (阻塞直到收到回复)
   │←─── 诊断结果 ─────────│  recv → 处理 → send
   │                       │
   │──── diag_query ──────→│  严格交替: send→recv→send→recv
   │←─── 诊断结果 ─────────│
```

**特点**: 同步锁步, 请求-回复必须交替, 不可连续发送多个请求。

### PUB/SUB — 发布-订阅

```
                     ┌──────────┐
              ┌─────→│Subscriber│ (订阅 "robot/joints")
              │      └──────────┘
 ┌────────┐   │      ┌──────────┐
 │Publisher│───┼─────→│Subscriber│ (订阅 "robot/imu")
 └────────┘   │      └──────────┘
              │      ┌──────────┐
              └─────→│Subscriber│ (订阅 "" = 全部)
                     └──────────┘
```

**特点**: 多播, 话题前缀过滤, 订阅者只收到匹配的消息。

### PUSH/PULL — 管道 (负载均衡)

```
                     ┌──────┐
              ┌─────→│Worker│ (处理 1,3,5,7...)
              │      └──────┘
 ┌────────┐   │
 │Producer│───┤      ┌──────┐
 └────────┘   │      │Worker│ (处理 2,4,6,8...)
              └─────→└──────┘
              ZMQ 自动 round-robin 分发
```

**特点**: 自动负载均衡, 多 Worker 并行处理, 无单点瓶颈。

### ROUTER/DEALER — 异步请求-响应

```
 Client(DEALER)            Server(ROUTER)
      │                        │
      │── req1 ───────────────→│
      │── req2 ───────────────→│  连续发送, 不阻塞!
      │── req3 ───────────────→│
      │←── resp2 ──────────────│  响应顺序可不同
      │←── resp1 ──────────────│
      │←── resp3 ──────────────│
```

**特点**: 无 REQ/REP 锁步限制, 支持多客户端并发, identity 路由。

### PAIR — 独占对

```
 Node A ←──────────→ Node B
        双向独占通道
    (一对一, 不可多对多)
```

**特点**: 最简单的一对一双向管道, 适合 inproc 线程间通信。

## 运行环境

```bash
# 安装 libzmq
sudo apt install libzmq3-dev

# cppzmq 是 header-only, 从 GitHub 获取
git clone https://github.com/zeromq/cppzmq.git
sudo cp cppzmq/zmq.hpp /usr/local/include/
sudo cp cppzmq/zmq_addon.hpp /usr/local/include/
```

## 构建 & 运行

```bash
# 一键编译 + 运行
bash scripts/run_demo.sh all              # 依次运行所有 5 种模式
bash scripts/run_demo.sh req_rep          # REQ/REP 请求-响应
bash scripts/run_demo.sh pub_sub          # PUB/SUB 发布-订阅
bash scripts/run_demo.sh push_pull        # PUSH/PULL 管道
bash scripts/run_demo.sh router_dealer    # ROUTER/DEALER 异步
bash scripts/run_demo.sh pair             # PAIR 独占对
bash scripts/run_demo.sh build            # 仅编译
bash scripts/run_demo.sh clean            # 清理

# 或手动分步:
cd build && cmake .. && make -j$(nproc)

# 手动多终端示例
./build/push_pull_producer &               # 终端1: 任务分发
./build/push_pull_worker --id=A &          # 终端2: Worker A
./build/push_pull_worker --id=B &          # 终端3: Worker B
```

## 关键 API (cppzmq)

```cpp
// 创建上下文和 socket
zmq::context_t context(1);
zmq::socket_t socket(context, ZMQ_REQ);  // ZMQ_REP, PUB, SUB, PUSH, PULL, ROUTER, DEALER, PAIR

// 绑定/连接
socket.bind("tcp://*:5555");              // 服务端
socket.connect("tcp://localhost:5555");   // 客户端

// 发送 (支持多帧)
socket.send(zmq::buffer("frame1"), zmq::send_flags::sndmore);
socket.send(zmq::buffer("frame2"), zmq::send_flags::none);

// 接收
zmq::message_t msg;
socket.recv(msg);
std::string data(static_cast<char*>(msg.data()), msg.size());

// PUB/SUB 订阅过滤
socket.set(zmq::sockopt::subscribe, "robot/");  // 前缀匹配
socket.set(zmq::sockopt::subscribe, "");        // 订阅全部

// ROUTER/DEALER identity
socket.set(zmq::sockopt::routing_id, "client-id");
```

## 模式选择指南

| 需求 | 推荐模式 | 原因 |
|------|---------|------|
| 简单 RPC 调用 (1客户端) | REQ/REP | 最简单, 自动锁步 |
| 数据广播 (多订阅者) | PUB/SUB | 多播, 话题过滤 |
| 并行任务处理 | PUSH/PULL | ZMQ 自动负载均衡 |
| 异步 RPC (多客户端) | ROUTER/DEALER | 无锁步限制, 并发 |
| 线程间通信 | PAIR | 独占, 零配置 |
| 进程间双向管道 | PAIR | 简单可靠 |

## 与其他中间件对比

| 特性 | ZMQ | gRPC | DDS | MQTT |
|------|-----|------|-----|------|
| 无 Broker | ✅ | ✅ | ✅ | ❌ |
| 多模式 | 5 种 | 4 种(RPC) | Pub/Sub | Pub/Sub |
| 序列化 | 自定义 | Protobuf | IDL | 自定义 |
| QoS | 尽力 | 尽力 | 丰富 | 3 级 |
| 复杂度 | 中 | 高 | 高 | 低 |
| 适用 | 通用消息 | 微服务 | 实时分布式 | IoT |

## 进阶学习方向

- [x] 5 种核心通信模式 Demo
- [ ] ZMQ 零拷贝与消息分帧机制
- [ ] 高水位标记 (HWM) 与背压控制
- [ ] Curve 加密安全通信
- [ ] ZMQ 代理模式 (Proxy) — PUB/SUB + PUSH/PULL 组合
- [ ] ZMQ 与 DDS/MQTT/gRPC 在机器人场景下的性能对比
- [ ] `inproc://` 和 `ipc://` 传输的性能差异
- [ ] 多帧消息与 `zmq_msg_send` 零拷贝
