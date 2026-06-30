# SOME/IP (vsomeip) 学习

SOME/IP (Scalable service-Oriented MiddlewarE over IP) 是 AUTOSAR 定义的车载以太网通信协议，vsomeip 是 COVESA (原 GENIVI) 的 C++ 参考实现，广泛用于量产车载系统。

## 文件结构

```
someip/
├── CMakeLists.txt
├── README.md
├── include/
│   └── vehicle_someip.hpp        # 共享头文件 (ID 常量 + 序列化 + 数据结构)
├── vehicle_service.cpp           # 服务端: Event + Method + Field
├── vehicle_client.cpp            # 客户端: 订阅 + 调用 + Get/Set
├── vsomeip-service.json          # 服务端路由配置
├── vsomeip-client.json           # 客户端路由配置
├── scripts/
│   └── run_demo.sh               # 一键编译运行
└── build/                        # CMake 构建产物
```

## SOME/IP 核心概念

```
┌──────────────────────────────────────────────────────────────┐
│                  SOME/IP 三种通信模式                          │
├──────────┬──────────┬─────────────┬──────────────────────────┤
│   模式   │  方向    │   机制       │  Demo 示例               │
├──────────┼──────────┼─────────────┼──────────────────────────┤
│  Method  │ Req/Rep  │ RPC 远程调用  │ 车窗控制 (0x0001)        │
│  Event   │ Pub/Sub  │ 发布/订阅     │ 车速(0x8001)/车门(0x8002)│
│  Field   │ Get/Set  │ 属性访问+RPC  │ 空调温度 (0x8003 Notify) │
│          │ Notify   │              │ Get(0x0002)/Set(0x0003) │
└──────────┴──────────┴─────────────┴──────────────────────────┘
```

### Method (请求/响应)
```
 Client                         Service
   │                              │
   │─── window_control(50%) ─────→│  Request
   │←── response: OK, pos=50% ────│  Response
```
**特点**: 同步 RPC, 客户端调用, 服务端执行并返回结果。

### Event (发布/订阅)
```
                   Service
                      │
            ┌─────────┼─────────┐
            ↓         ↓         ↓
         Client1   Client2   Client3
       (收到车速)  (收到车门)  (全都收到)
```
**特点**: 服务端主动推送, 客户端通过 Eventgroup 订阅, 基于 SD 自动发现。

### Field (属性 — Get/Set/Notifier 三位一体)
```
 Client                         Service
   │                              │
   │─── GET climate_temp ────────→│  Getter (Method)
   │←── 22.5°C ─────────────────│
   │                              │
   │─── SET climate_temp=26°C ──→│  Setter (Method)
   │←── OK, 26°C ───────────────│
   │                              │
   │←── NOTIFY climate=26°C ────│  Notifier (Event) → 所有订阅者
```
**特点**: SOME/IP 独有模式, Getter/Setter 是 Method, 变更后通过 Event 通知所有订阅者。

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
# 一键编译 + 运行
bash scripts/run_demo.sh both           # 服务端后台 + 客户端前台
bash scripts/run_demo.sh service         # 仅服务端
bash scripts/run_demo.sh client          # 仅客户端
bash scripts/run_demo.sh build           # 仅编译
bash scripts/run_demo.sh clean           # 清理

# 或手动分步:
cd build && cmake .. && make -j$(nproc)

# 手动多终端:
# 终端1: 服务端
VSOMEIP_CONFIGURATION=../vsomeip-service.json ./vehicle_service

# 终端2: 客户端
VSOMEIP_CONFIGURATION=../vsomeip-client.json ./vehicle_client
```

## Demo 说明

| 文件 | 功能 |
|------|------|
| `include/vehicle_someip.hpp` | 共享头文件: ID 常量 / 字节序转换 / 序列化辅助 |
| `vehicle_service.cpp` | 服务端: 2 Event + 1 Method + 1 Field |
| `vehicle_client.cpp` | 客户端: 订阅事件 + 调用方法 + Field Get/Set |
| `vsomeip-service.json` | 服务端路由配置 (Service Discovery 参数) |
| `vsomeip-client.json` | 客户端路由配置 |
| `scripts/run_demo.sh` | 一键编译运行脚本 |

## 关键 API (vsomeip C++)

```cpp
#include "include/vehicle_someip.hpp"
using namespace vehicle;

// 获取运行时
auto runtime = vsomeip::runtime::get();

// 创建应用
auto app = runtime->create_application("name");

// 初始化
app->init();

// === 服务端 ===
app->offer_service(SERVICE_ID, INSTANCE_ID);

// 注册 Method 处理器
app->register_message_handler(SERVICE_ID, INSTANCE_ID, METHOD_WINDOW,
    std::bind(&on_method, ...));

// 推送 Event
app->notify(SERVICE_ID, INSTANCE_ID, EVENT_SPEED, serialize_speed(80.0));

// === 客户端 ===
// 服务发现
app->register_availability_handler(SERVICE_ID, INSTANCE_ID,
    std::bind(&on_available, ...));
app->request_service(SERVICE_ID, INSTANCE_ID);

// 订阅事件
app->request_event(SERVICE_ID, INSTANCE_ID, EVENT_SPEED, {EVENTGROUP_VEHICLE});
app->subscribe(SERVICE_ID, INSTANCE_ID, EVENTGROUP_VEHICLE);

// 调用 Method
auto request = vsomeip::runtime::get()->create_request();
request->set_service(SERVICE_ID);
request->set_instance(INSTANCE_ID);
request->set_method(METHOD_WINDOW);
request->set_payload(serialize_window(50));
app->send(request);

// 运行
app->start();
```

## 进阶学习方向

- [x] SOME/IP 三种通信模式 (Method / Event / Field)
- [ ] SOME/IP-SD 服务发现流程 (FindService → OfferService → Subscribe)
- [ ] SOME/IP-TP 大报文分段机制
- [ ] E2E (End-to-End) 安全保护
- [ ] vsomeip 与 DDS 的车载场景横向对比
- [ ] 多种传输协议性能对比 (TCP vs UDP, unicast vs multicast)
- [ ] SOME/IP 与 AUTOSAR Adaptive 平台的集成
- [ ] 车载 SOA (Service-Oriented Architecture) 实战
