# GDBus 学习 (C++17 + XML 驱动代码生成)

基于 GLib/GIO 的 D-Bus 实现，以 **机器人系统** 为 demo，核心开发模式：**接口定义 (XML) → 代码自动生成 → 只写业务逻辑**。

## Demo 架构

```
┌──────────────────────────────────────────────────────────────┐
│  demo_service.cpp (机器人服务端)                              │
│  ┌─────────────────┐  ┌────────────────┐  ┌───────────────┐ │
│  │ RobotSimulator   │  │ DemoRobotServer│  │ gdbus_cxx     │ │
│  │ • 状态机         │  │ (auto-gen)     │  │ • MainLoop    │ │
│  │ • 任务调度       │  │ • 方法回调     │  │ • BusName     │ │
│  │ • 健康指标       │  │ • 信号发射     │  │               │ │
│  └─────────────────┘  └────────────────┘  └───────────────┘ │
└──────────────────────────────────────────────────────────────┘
                          │ D-Bus (session bus)
                          ▼
┌──────────────────────────────────────────────────────────────┐
│  demo_client.cpp (机器人监控终端)                              │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │ DemoRobotClient (auto-gen)                               │ │
│  │ • GetRobotStatus()   查询完整状态                         │ │
│  │ • ExecuteTask()      远程下发任务                         │ │
│  │ • EmergencyStop()    紧急停止                             │ │
│  │ • StateChanged 信号  订阅状态变更                         │ │
│  │ • TaskProgress 信号  订阅任务进度                         │ │
│  │ • 属性轮询: State / BatteryLevel / Emotion / CpuTemp     │ │
│  └─────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────┘
```

## 机器人状态机

对应 RobotStateManager 状态机层:

```
Uninitialized → Setup → SelfCheck → Idle ↔ Working
                              ↓         ↓
                         LowPower  ManualControl
                              ↓         ↓
                         Fault → EmergencyStop → Terminated
```

## 开发工作流

```
interfaces/com.example.Robot.xml       ← ★ 增删改查只改这里
          │
          ├── gdbus-codegen ──→ generated/robot-dbus.h/.c       (C skeleton/proxy)
          │
          └── gen_cpp_bindings.py ──→ generated/robot_bindings.hpp/.cpp  (C++ wrapper)
          │
          ▼
src/demo_service.cpp   ← 只写业务回调
src/demo_client.cpp    ← 只写结果处理
```

## 快速开始

```bash
# 安装依赖 (一次性)
sudo apt install libglib2.0-dev cmake g++ python3 -y

# 编译
cd gdbus && mkdir -p build && cd build
cmake .. && make -j$(nproc)

# 运行 (需要 dbus-run-session)
dbus-run-session -- bash -c '
./gdbus-demo-service &
sleep 2
timeout 35 ./gdbus-demo-client
kill %1 2>/dev/null
'
```

## 如何添加新接口

假设要在 `com.example.Robot` 里新增 `SetSpeed` 方法：

**1. 编辑 XML** [interfaces/com.example.Robot.xml](interfaces/com.example.Robot.xml)：

```xml
<method name="SetSpeed">
  <arg type="d" name="speed" direction="in"/>
</method>
```

**2. 重新编译**：

```bash
cd build && cmake .. && make -j$(nproc)
```

**3. 自动生成的 C++ API** (无需手写)：

服务端多了：
```cpp
skel.set_set_speed_handler([](double speed) {
    // 你的业务逻辑
});
```

客户端多了：
```cpp
proxy->set_speed_async(1.5,  // 调用远程方法
    []() { /* 成功 */ },
    [](const std::string& err) { /* 失败 */ });
```

同样的流程适用于增删信号和属性。

## 项目结构

```
gdbus/
├── interfaces/
│   └── com.example.Robot.xml            ← ★ 接口定义文件
├── scripts/
│   ├── gen_cpp_bindings.py              ← XML → C++ 代码生成器
│   ├── build_deps.sh                    ← GLib 交叉编译
│   └── run_demo.sh                      ← 一键运行
├── include/
│   └── gdbus_cxx.hpp                    ← RAII 基础 (MainLoop, BusName)
├── src/
│   ├── gdbus_cxx.cpp                    ← RAII 实现
│   ├── demo_service.cpp                 ← 服务端 (机器人模拟)
│   └── demo_client.cpp                  ← 客户端 (远程监控)
└── build/generated/                     ← cmake 自动生成 (不提交 git)
    ├── robot-dbus.h / .c                ← gdbus-codegen 产物
    └── robot_bindings.hpp / .cpp        ← gen_cpp_bindings.py 产物
```

## C++17 特性

| 特性 | 使用位置 |
|------|---------|
| `std::function` | 回调处理 |
| 结构化绑定 | `auto& [id, state, battery, temp] = result;` |
| `std::atomic` | 线程安全状态 (RobotSimulator) |
| `std::unique_ptr` | GObject 生命周期管理 |
| Lambda | 定时器 + 回调 |
| RAII | MainLoop, BusName |

## D-Bus 接口一览

### Methods

| 方法 | 输入 | 输出 | 说明 |
|------|------|------|------|
| `GetRobotStatus` | — | robot_id, state, battery, cpu_temp | 查询完整状态 |
| `ExecuteTask` | task_type, task_name | task_id, success | 下发任务 |
| `CancelTask` | task_id | success | 取消任务 |
| `EmergencyStop` | — | success | 紧急停止 |

### Signals

| 信号 | 参数 | 说明 |
|------|------|------|
| `StateChanged` | old_state, new_state | 状态转移通知 |
| `TaskProgress` | task_id, progress, status | 任务进度更新 |
| `BatteryLow` | level | 低电量告警 |

### Properties (只读)

| 属性 | 类型 | 说明 |
|------|------|------|
| `State` | string | 当前机器人状态 |
| `BatteryLevel` | double | 电量 0.0-100.0 |
| `Emotion` | string | 当前表情 |
| `CpuTemp` | double | CPU 温度 |

## 交叉编译 (ARM)

```bash
bash scripts/build_deps.sh aarch64
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain_aarch64.cmake
make -j$(nproc)
```
