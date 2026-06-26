# GDBus 学习 (C++17 + XML 驱动代码生成)

基于 GLib/GIO 的 D-Bus 实现，核心开发模式：**接口定义 (XML) → 代码自动生成 → 只写业务逻辑**。

## 开发工作流

```
interfaces/com.example.Vehicle.xml     ← ★ 增删改查只改这里
          │
          ├── gdbus-codegen ──→ generated/vehicle-dbus.h/.c       (C skeleton/proxy)
          │
          └── gen_cpp_bindings.py ──→ generated/vehicle_bindings.hpp/.cpp  (C++ wrapper)
          │
          ▼
src/demo_service.cpp   ← 只写业务回调
src/demo_client.cpp    ← 只写结果处理
```

## 快速开始

```bash
# 安装依赖 (一次性)
sudo apt install libglib2.0-dev cmake g++ python3 -y

# 编译运行
cd gdbus && bash scripts/run_demo.sh build
```

## 如何添加新接口

假设要在 `com.example.Vehicle` 里新增 `SetMaxSpeed` 方法：

**1. 编辑 XML** [interfaces/com.example.Vehicle.xml](interfaces/com.example.Vehicle.xml)：

```xml
<method name="SetMaxSpeed">
  <arg type="d" name="max_speed" direction="in"/>
</method>
```

**2. 重新编译**：

```bash
cd build && cmake .. && make -j$(nproc)
```

**3. 自动生成的 C++ API** (无需手写)：

服务端多了：
```cpp
skel.set_set_max_speed_handler([](double max_speed) {
    // 你的业务逻辑
});
```

客户端多了：
```cpp
proxy->set_max_speed_async(120.0,  // 调用远程方法
    []() { /* 成功 */ },
    [](const std::string& err) { /* 失败 */ });
```

同样的流程适用于增删信号和属性。

## 项目结构

```
gdbus/
├── interfaces/
│   └── com.example.Vehicle.xml         ← ★ 接口定义文件
├── scripts/
│   ├── gen_cpp_bindings.py             ← XML → C++ 代码生成器
│   ├── build_deps.sh                   ← GLib 交叉编译
│   └── run_demo.sh                     ← 一键运行
├── include/
│   └── gdbus_cxx.hpp                   ← RAII 基础 (MainLoop, BusName)
├── src/
│   ├── gdbus_cxx.cpp                   ← RAII 实现
│   ├── demo_service.cpp                ← 服务端 (业务逻辑)
│   └── demo_client.cpp                 ← 客户端 (业务逻辑)
└── build/generated/                    ← cmake 自动生成 (不提交 git)
    ├── vehicle-dbus.h / .c             ← gdbus-codegen 产物
    └── vehicle_bindings.hpp / .cpp     ← gen_cpp_bindings.py 产物
```

## C++17 特性

| 特性 | 使用位置 |
|------|---------|
| `std::function` | 回调处理 |
| 结构化绑定 | `auto& [id, speed, odo] = result;` |
| `std::atomic` | 线程安全状态 |
| `std::unique_ptr` | GObject 生命周期管理 |
| Lambda | 定时器 + 回调 |
| RAII | MainLoop, BusName |

## 交叉编译 (ARM)

```bash
bash scripts/build_deps.sh aarch64
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain_aarch64.cmake
make -j$(nproc)
```
