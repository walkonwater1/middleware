# GDBus 学习

GDBus 是基于 GLib 的 D-Bus 实现，广泛应用于 Linux 桌面环境和嵌入式系统。D-Bus 提供两种总线：

- **System Bus**: 系统级服务 (如硬件事件、网络管理)
- **Session Bus**: 用户会话级服务 (如桌面应用通信)

## GDBus 核心概念

| 概念 | 说明 |
|------|------|
| Bus Name | 服务在总线上的唯一名称 (如 `com.example.Service`) |
| Object Path | 对象路径 (如 `/com/example/Service`) |
| Interface | 接口定义, 包含方法、信号、属性 |
| Method Call | 远程方法调用 (RPC) |
| Signal | 广播信号 |
| Property | 可读写属性 |

## 运行环境

```bash
# Ubuntu / Debian
sudo apt install libglib2.0-dev

# macOS
brew install glib

# 检查 D-Bus 是否运行 (Linux 通常已运行)
ps aux | grep dbus-daemon
```

## 运行 Demo

```bash
# 编译
make

# 终端1: 启动服务 (需要 D-Bus session bus)
./gdbus-demo-service

# 终端2: 调用客户端
./gdbus-demo-client

# 清理
make clean
```

## Demo 说明

本 demo 模拟一个车辆信息服务:
- **Service**: 提供 `GetVehicleInfo` 方法, 发出 `SpeedChanged` 信号
- **Client**: 调用方法获取车辆信息, 监听速度变化信号

## 核心 API 速查

```c
// 创建连接
g_bus_own_name(G_BUS_TYPE_SESSION, name, flags,
               bus_acquired_cb, name_acquired_cb, name_lost_cb, ...);

// 注册对象 (服务端)
g_dbus_connection_register_object(connection, path, interface_info, vtable, ...);

// 调用方法 (客户端)
g_dbus_connection_call(connection, bus_name, object_path,
                       interface_name, method_name, parameters, reply_type,
                       flags, timeout, cancellable, callback, user_data);

// 订阅信号 (客户端)
g_dbus_connection_signal_subscribe(connection, sender, interface_name,
                                    signal_name, object_path, arg0,
                                    flags, callback, user_data, ...);
```

## 进阶学习方向

- [ ] D-Bus 自省 (Introspection) 机制
- [ ] System Bus 权限策略配置
- [ ] 方法调用超时与错误处理
- [ ] 属性变更通知 (PropertiesChanged)
- [ ] GDBus 与 QtDbus 的互操作性
