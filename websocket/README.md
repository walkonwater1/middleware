# WebSocket — 全双工实时 Web 通信

> 基于 TCP 的全双工通信协议，`ws://` / `wss://`，浏览器原生支持

## 动机

MQTT 解决了设备→云端的数据上报，但当你需要在 **Web 仪表盘** 或 **移动端 HMI** 上实时监控机器人状态时，WebSocket 是浏览器直接可用的唯一双向通道。不需要轮询，不需要 Long Polling — TCP 连接建立后双向自由收发。

典型场景：
- 机器人遥测 Web 面板（本 Demo）
- 云端指令下发（浏览器 → 机器人）
- 实时日志推流
- 协同操控（多人同时连接）

```
┌──────────────────────────────────────────┐
│       WebSocket 通信模型 (ws://)          │
│                                          │
│  Browser / CLI Client                    │
│  ┌────────────────────────────────┐      │
│  │ client.cpp                     │      │
│  │  ws://localhost:9002           │      │
│  │  → status / echo / cmd:xxx     │      │
│  │  ← JSON 遥测 (1s 间隔)         │      │
│  └───────┬────────────────────────┘      │
│          │  TCP (Upgrade: HTTP → WS)     │
│          │  Frames: Text / Binary        │
│          │  Control: Ping/Pong/Close     │
│  ┌───────▼────────────────────────┐      │
│  │ server.cpp                     │      │
│  │  • 多客户端并发管理             │      │
│  │  • 命令分发 (status/echo/cmd)  │      │
│  │  • 1s 定时广播遥测 JSON         │      │
│  │  • WebSocket Ping/Pong 心跳     │      │
│  └────────────────────────────────┘      │
│          │                               │
│  ┌───────▼────────────────────────┐      │
│  │ RobotSimulator                 │      │
│  │  • 6 轴关节 (sin 运动轨迹)     │      │
│  │  • 电池/CPU/内存               │      │
│  └────────────────────────────────┘      │
└──────────────────────────────────────────┘
```

## 传输帧格式

```
┌─┬─┬─┬─┬─┬─┬─┬─┐   ┌─┬─┬─┬─┬─┬─┬─┬─┐
│F│R│R│R│  Opcode  │M│     Payload    │
│I│S│S│S│  (4bit)  │A│     Length     │
│N│V│V│V│          │S│               │
└─┴─┴─┴─┴─┴─┴─┴─┘   └─┴─┴─┴─┴─┴─┴─┴─┘
  Text(0x1) / Binary(0x2) / Close(0x8) / Ping(0x9) / Pong(0xA)
```

## 快速开始

```bash
# 安装依赖 (仅首次)
sudo apt-get install -y libwebsocketpp-dev libboost-system-dev

# 一键编译 + 运行 (服务端后台 + 客户端前台)
bash scripts/run_demo.sh

# 或手动分步:
bash scripts/run_demo.sh server    # 终端 1 — 服务端
bash scripts/run_demo.sh client    # 终端 2 — 客户端
bash scripts/run_demo.sh build     # 仅编译

# 自定义端口:
WS_PORT=8080 bash scripts/run_demo.sh

# 浏览器测试 (服务端启动后):
# 打开 DevTools Console 输入:
#   ws = new WebSocket('ws://localhost:9002')
#   ws.onmessage = e => console.log(JSON.parse(e.data))
#   ws.send('status')
```

## 手动构建

```bash
cd websocket && mkdir -p build && cd build
cmake .. && make -j$(nproc)

## 运行

```bash
# 终端 1 — 启动 Server
./build/ws-server

# 终端 2 — 运行 CLI Client
./build/ws-client

# 或者用浏览器测试 (打开 DevTools → Console)
# 连上后直接在 Console 里输入:
#   ws = new WebSocket('ws://localhost:9002')
#   ws.onmessage = e => console.log(JSON.parse(e.data))
#   ws.send('status')
```

**预期输出:**

Server 端打印每个客户端的连接/断开事件和收到的命令。Client 端自动演示：

1. 收到欢迎消息
2. 发送 `status` → 获取即时状态
3. 发送 `echo` → 验证双向通信
4. 发送 `cmd:self_check` → 命令下发
5. 持续接收 1s 间隔的遥测 JSON 广播

## 协议命令

客户端发送文本帧 (Text)，服务端分发：

| 命令 | 响应 |
|------|------|
| `status` | 即时机器人状态 (6 轴关节 + 电池 + CPU/内存) |
| `echo` | 双向通信确认 |
| `cmd:<command>` | 命令确认 (模拟 OTA/自检/重启) |
| 其他 | 帮助信息（可用命令列表） |

服务端广播 (Text JSON):

```json
{
  "type": "telemetry",
  "battery": 84.99,
  "cpu": 35.23,
  "mem": 42.17,
  "joints": [
    {"name": "base", "position": 0.123, "velocity": 0.51, "torque": 0.22, "temperature": 35.0},
    ...
  ],
  "timestamp": 1719744000123,
  "connections": 2
}
```

## 与其他中间件对比

| 维度 | WebSocket | MQTT | gRPC | DDS |
|------|-----------|------|------|-----|
| 协议层 | L7 (HTTP Upgrade) | L7 (TCP) | L7 (HTTP/2) | L4+ (UDP) |
| 通信模式 | 全双工 | Pub/Sub | RPC+Stream | Pub/Sub |
| 浏览器支持 | ✅ 原生 | ❌ (需 bridge) | ❌ (需 grpc-web) | ❌ |
| QoS | 无(协议层) | QoS 0/1/2 | 无 | 20+ 策略 |
| Broker/Brokerless | 无 Broker | 有 Broker | 无 (点对点) | 无 (P2P) |
| 适用场景 | Web 实时面板 | IoT 弱网 | 微服务 RPC | 实时数据分发 |

## 进阶学习方向

- [ ] WSS (TLS 加密): 自签名证书 + 安全传输
- [ ] 二进制帧: Protobuf 编码替代 JSON (减少 60% 流量)
- [ ] JavaScript 前端: HTML 遥测仪表盘 (Canvas 关节动画)
- [ ] 断线重连 + 指数退避
- [ ] 百万连接: epoll + 多线程模型调优
- [ ] WebSocket ↔ MQTT Bridge (Web 面板 → 设备)
- [ ] WebSocket vs SSE (Server-Sent Events): 双向 vs 单向推送
- [ ] 与 Socket.IO 对比: 原生 WS vs 有上层协议的库

## 参考资料

- [RFC 6455 — The WebSocket Protocol](https://datatracker.ietf.org/doc/html/rfc6455)
- [websocketpp 文档](https://docs.websocketpp.org/)
- [MDN WebSocket API](https://developer.mozilla.org/en-US/docs/Web/API/WebSocket)
