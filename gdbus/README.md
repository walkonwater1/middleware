# GDBus 学习 (C++17)

基于 GLib/GIO 的 D-Bus 实现，广泛应用于 Linux 桌面环境和嵌入式系统。

本 Demo 使用 **C++17** 实现了 RAII 封装和现代化的异步编程模型。

## D-Bus 核心概念

| 概念 | 说明 | 示例 |
|------|------|------|
| Bus Name | 服务在总线上的唯一名称 | `com.example.VehicleService` |
| Object Path | 对象路径 | `/com/example/Vehicle` |
| Interface | 接口定义 (方法、信号、属性) | `com.example.Vehicle` |
| Method Call | 远程方法调用 (RPC) | `GetVehicleInfo() → (sdd)` |
| Signal | 广播信号 | `SpeedChanged(d)` |
| Property | 可读写属性 | `Speed` (read, double) |

## C++17 设计亮点

| 特性 | 说明 |
|------|------|
| **RAII 资源管理** | `std::unique_ptr` + 自定义 Deleter 自动释放 GLib 资源 |
| **Lambda 回调** | 用 `std::function` 替代 C 函数指针 + `void*` |
| **结构化绑定** | `auto [id, speed, odo] = info;` 解构返回结果 |
| **std::atomic** | 线程安全的速度/里程表状态 |
| **std::chrono** | 类型安全的时间间隔表示 |
| **移动语义** | `BusName`, `SignalSubscription` 支持 move-only 语义 |

## 项目结构

```
gdbus/
├── CMakeLists.txt                  # CMake 构建 (C++17)
├── README.md
├── Makefile                        # 原有 C 版 Makefile (保留)
├── cmake/
│   ├── FindGLIB.cmake              # 优先查找 third-lib/ 的 CMake 模块
│   ├── toolchain_aarch64.cmake     # ARM64 交叉编译工具链
│   └── toolchain_armhf.cmake       # ARM32 交叉编译工具链
├── scripts/
│   └── build_deps.sh               # 交叉编译依赖库到 third-lib/
├── third-lib/glib/                 # (build_deps.sh 产物) 预编译的 GLib 静态库
│   ├── include/
│   │   ├── glib-2.0/               # GLib + GIO + GObject 头文件
│   │   └── glibconfig/             # glibconfig.h (平台生成)
│   └── lib/
│       ├── libglib-2.0.a
│       ├── libgio-2.0.a
│       ├── libgobject-2.0.a
│       ├── libgmodule-2.0.a
│       ├── libffi.a
│       └── libpcre2-8.a
├── include/
│   └── gdbus_cxx.hpp               # RAII 封装库 (GMainLoop, GVariant, BusName, ...)
└── src/
    ├── gdbus_cxx.cpp               # RAII 封装库实现 (C→C++ 回调桥接)
    ├── vehicle_service.hpp/cpp     # VehicleService — D-Bus 服务端
    ├── vehicle_client.hpp/cpp      # VehicleClient — D-Bus 客户端代理
    ├── demo_service.cpp            # 服务端入口
    └── demo_client.cpp             # 客户端入口
```

### 核心类说明

#### `gdbus_cxx` 命名空间 (RAII 封装)

| 类 | 封装 | 功能 |
|----|------|------|
| `MainLoop` | `GMainLoop` | RAII 事件循环 |
| `Variant` | `GVariant` | GVariant 所有权 + 工厂方法 |
| `VariantBuilder` | `GVariantBuilder` | RAII 构建器 |
| `BusName` | `g_bus_own_name` | RAII 总线名所有权 (move-only) |
| `SignalSubscription` | `g_dbus_connection_signal_subscribe` | RAII 信号订阅 (move-only) |

#### `VehicleService` — 服务端

```cpp
VehicleService svc;
svc.start();          // 获取总线名, 注册对象, 启动速度模拟
// ... 主循环运行 ...
svc.stop();           // 或析构自动清理
```

- 方法: `GetVehicleInfo()` → `(s)vehicle_id, (d)speed, (d)odometer`
- 信号: `SpeedChanged(d new_speed)` (每2秒)
- 属性: `Speed` (read, double)
- 同时发出 `PropertiesChanged` 信号

#### `VehicleClient` — 客户端

```cpp
VehicleClient client;

// 设置回调 (C++17 lambda)
client.on_speed_changed = [](double s) { /* 处理速度变化 */ };
client.on_info_received = [](const VehicleInfo& info) {
    auto& [id, speed, odo] = info;  // 结构化绑定
};
client.on_error = [](const std::string& err) { /* 错误处理 */ };

client.connect();  // 异步连接
```

## 运行环境

### 开发机 (编译用)

```bash
# Ubuntu / Debian — 安装编译工具
sudo apt install cmake make g++ build-essential

# 如果是交叉编译 ARM，额外安装交叉编译器:
sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu  # ARM64
sudo apt install gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf # ARM32
```

### 目标板 (运行用)

```bash
# 需要 D-Bus daemon，Ubuntu 桌面版默认已装
# Server 版或容器环境需要手动安装:
sudo apt install dbus libglib2.0-0

# 检查 session bus 是否运行
echo $DBUS_SESSION_BUS_ADDRESS
# 若为空, 用 dbus-run-session 包裹运行:
dbus-run-session ./gdbus-demo-service
```

## 快速运行 (一键脚本)

```bash
# 首次使用: 编译并运行 (自动处理 D-Bus 环境)
bash scripts/run_demo.sh build

# 后续只需:
bash scripts/run_demo.sh              # 服务端+客户端一起跑
bash scripts/run_demo.sh service      # 终端1: 仅服务端
bash scripts/run_demo.sh client       # 终端2: 仅客户端
bash scripts/run_demo.sh clean        # 清理
```

脚本会自动检测并启动 D-Bus session bus（如果未运行），退出时自动清理。

## 编译方式

### 方式一: 本地编译 (x86_64, 依赖系统 GLib)

适用于在开发机上直接编译运行：

```bash
# 安装 GLib 开发包
sudo apt install libglib2.0-dev meson ninja-build

cd gdbus
mkdir -p build && cd build
cmake ..
make -j$(nproc)

# 运行
./gdbus-demo-service       # 终端1
./gdbus-demo-client        # 终端2
```

### 方式二: 使用 third-lib (不依赖板端开发包)

先将 GLib 编译为静态库放入 `third-lib/`，编译时静态链接，运行时无需 `libglib2.0-dev`：

```bash
# Step 1: 编译依赖到 third-lib/
bash scripts/build_deps.sh              # x86_64
bash scripts/build_deps.sh aarch64      # ARM64 交叉编译
bash scripts/build_deps.sh armhf        # ARM32 交叉编译

# Step 2: CMake 会自动优先查找 third-lib/
cd gdbus
mkdir -p build && cd build

# 本地编译
cmake .. && make -j$(nproc)

# ARM64 交叉编译 (需配合 toolchain 文件)
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain_aarch64.cmake
make -j$(nproc)

# ARM32 交叉编译
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain_armhf.cmake
make -j$(nproc)

# Step 3: 拷贝到目标板
scp gdbus-demo-service gdbus-demo-client user@board:/tmp/
```

### 构建依赖脚本说明

`build_deps.sh` 自动下载并编译：

| 组件 | 版本 | 用途 |
|------|------|------|
| libffi | 3.4.6 | GObject 类型系统依赖 |
| pcre2 | 10.43 | GLib 正则表达式 |
| GLib | 2.76.6 | GIO/GDBus 核心库 |

编译产物全部放入 `third-lib/glib/`，CMake 的 `FindGLIB.cmake` 优先查找该目录，找不到再回退到系统 pkg-config。

## Demo 运行流程

```
终端1 (Service)                    终端2 (Client)
─────────────                      ─────────────
服务启动                           客户端启动
获取总线名                         连接 Session Bus
注册对象                           订阅 SpeedChanged 信号
启动速度模拟 (2s)                  调用 GetVehicleInfo()
  │                                  │
  ├─SpeedChanged(55.2)──→           ← 收到信号
  ├─PropertiesChanged   →           ← 收到属性变更
  │                                  │
  │                              读取 Speed 属性
  │                              (每5秒轮询)
  ...                              ...
```

## C vs C++17 对比

| 方面 | C 版本 (原版) | C++17 版本 |
|------|--------------|------------|
| 回调机制 | 函数指针 + `void* user_data` | `std::function` + lambda |
| 资源管理 | 手动 `g_free`/`g_object_unref` | RAII `unique_ptr` 自动释放 |
| 数据封装 | 全局静态变量 | 类成员 + `std::atomic` |
| 错误处理 | 手动检查 `GError*` | `ErrorPtr` RAII + 错误回调 |
| 信号订阅 | 手动管理 ID | `SignalSubscription` RAII |
| 线程安全 | 无保护 | `std::atomic<double>` |
| 构建系统 | Makefile | CMake |

## 核心 API 速查

```cpp
// === 服务端 ===
// 获取总线名 (RAII)
gdbus_cxx::BusName bus;
bus.own(G_BUS_TYPE_SESSION, "com.example.Service",
    [](GDBusConnection* conn, const std::string& name) {
        // 注册对象...
    },
    [](GDBusConnection* conn, const std::string& name) {
        // 名字丢失...
    });

// 注册对象
g_dbus_connection_register_object(conn, path, iface_info, &vtable, this, nullptr, &err);

// 发出信号
g_dbus_connection_emit_signal(conn, nullptr, path, iface, name, params, &err);

// === 客户端 ===
// 连接 Session Bus
gdbus_cxx::connect_session_bus([](GDBusConnection* conn, const std::string& err) { ... });

// 异步方法调用
gdbus_cxx::async_call(conn, bus_name, path, iface, method, params, reply_type,
    [](GVariant* result, const std::string& err) { ... });

// 订阅信号
gdbus_cxx::SignalSubscription sub;
sub.subscribe(conn, sender, iface, signal, path,
    [](GDBusConnection*, ...) { ... });
```

## 进阶学习方向

- [ ] D-Bus 自省 (Introspection) — 动态发现接口
- [ ] System Bus 权限策略配置 (`/etc/dbus-1/system.d/`)
- [ ] 方法调用超时与错误处理 (`G_DBUS_ERROR_*`)
- [ ] 属性变更通知 (PropertiesChanged) — 已在本 Demo 中实现
- [ ] GDBus 与 QtDbus 的互操作性
- [ ] 使用 `gdbus-codegen` 从 XML 生成 C++ 绑定代码
- [ ] D-Bus 安全模型 (SELinux/AppArmor 集成)
