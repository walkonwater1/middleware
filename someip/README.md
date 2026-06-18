# SOME/IP (vsomeip) 学习

SOME/IP (Scalable service-Oriented MiddlewarE over IP) 是 AUTOSAR 定义的车载以太网通信协议，vsomeip 是 COVESA (原 GENIVI) 的 C++ 参考实现，广泛用于量产车载系统。

## SOME/IP 核心概念

| 概念 | 说明 |
|------|------|
| Service | 服务实例，由 Service ID + Instance ID 唯一标识 |
| Event | 发布/订阅事件，服务端主动推送 |
| Method | 请求/响应 RPC，类似远程函数调用 |
| Field | 属性 = Getter + Setter + 变更通知(Event) |
| Eventgroup | 事件分组，客户端按组订阅 |
| SOME/IP-TP | 大报文分段传输协议 |
| SD (Service Discovery) | 服务发现协议，自动发现网络中可用的服务 |

## vsomeip 架构

```
┌──────────────────────────────────────┐
│          Application Layer           │
│   (service.cpp / client.cpp)         │
├──────────────────────────────────────┤
│         vsomeip 库 (libvsomeip3)     │
│   ┌──────────┐  ┌─────────────────┐  │
│   │  Routing  │  │  Service        │  │
│   │  Manager  │  │  Discovery      │  │
│   └──────────┘  └─────────────────┘  │
├──────────────────────────────────────┤
│         TCP / UDP (SOME/IP-TP)       │
└──────────────────────────────────────┘
```

## SOME/IP 与其他中间件对比

| 特性 | SOME/IP | DDS | MQTT |
|------|---------|-----|------|
| 标准来源 | AUTOSAR | OMG | OASIS |
| 主要场景 | 车载 ECU | 军工/机器人/自动驾驶 | IoT |
| 序列化 | SOME/IP (二进制) | CDR / XCDR | 用户自定义 |
| 服务发现 | SD (组播) | SEDP / SPDP | 无(依赖Broker) |
| 传输协议 | TCP/UDP | UDP (默认) | TCP |
| 实时性 | 软实时 | 硬实时 (QoS) | 非实时 |

## 运行环境

```bash
# 安装 vsomeip (推荐从源码编译)
git clone https://github.com/COVESA/vsomeip.git
cd vsomeip
mkdir build && cd build
cmake -DENABLE_SIGNAL_HANDLING=1 ..
make
sudo make install
sudo ldconfig
```

## 构建 & 运行

```bash
mkdir build && cd build
cmake ..
make

# ===== 启动服务 =====
# 终端1: 启动 vsomeip 服务 (提供车辆数据)
VSOMEIP_CONFIGURATION=../vsomeip-service.json ./vehicle_service

# ===== 启动客户端 =====
# 终端2: 启动客户端 (订阅事件 + 调用方法)
VSOMEIP_CONFIGURATION=../vsomeip-client.json ./vehicle_client
```

> `VSOMEIP_CONFIGURATION` 指向 JSON 配置文件，定义 Service ID、Instance ID、端口等。

## Demo 说明

| 文件 | 功能 |
|------|------|
| `vehicle_service.cpp` | 服务端: 提供车速/车门状态事件, 车窗控制方法 |
| `vehicle_client.cpp` | 客户端: 订阅事件 + 周期性调用远程方法 |
| `vsomeip-service.json` | 服务端路由配置 (Service Discovery 参数) |
| `vsomeip-client.json` | 客户端路由配置 |
| `CMakeLists.txt` | CMake 构建脚本 |

## 关键 API (vsomeip C++)

```cpp
#include <vsomeip/vsomeip.hpp>

// 获取运行时
auto runtime = vsomeip::runtime::get();

// 创建应用
auto app = runtime->create_application("name");

// 初始化
app->init();

// === 服务端 ===
app->offer_service(SERVICE_ID, INSTANCE_ID);
app->register_message_handler(SERVICE_ID, INSTANCE_ID, METHOD_ID,
                              std::bind(&on_method_call, ...));
app->notify(SERVICE_ID, INSTANCE_ID, EVENT_ID, payload);

// === 客户端 ===
app->register_availability_handler(SERVICE_ID, INSTANCE_ID,
                                    std::bind(&on_available, ...));
app->request_service(SERVICE_ID, INSTANCE_ID);
app->request_event(SERVICE_ID, INSTANCE_ID, EVENT_ID, {EVENTGROUP_ID});
app->register_message_handler(SERVICE_ID, INSTANCE_ID, EVENT_ID,
                              std::bind(&on_event, ...));
app->request_service(SERVICE_ID, INSTANCE_ID);

// 运行
app->start();
```

## 进阶学习方向

- [ ] SOME/IP-SD 服务发现流程 (FindService → OfferService → Subscribe)
- [ ] SOME/IP-TP 大报文分段机制
- [ ] E2E (End-to-End) 安全保护
- [ ] vsomeip 与 DDS 的车载场景横向对比
- [ ] 多种传输协议性能对比 (TCP vs UDP, unicast vs multicast)
- [ ] SOME/IP 与 AUTOSAR Adaptive 平台的集成
- [ ] 车载 SOA (Service-Oriented Architecture) 实战
