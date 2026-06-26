# DDS Vendor Lab — RMW 抽象层说明

## 什么是 RMW?

RMW (ROS Middleware) 是 ROS2 中 rclcpp 和 DDS 之间的抽象层。

```
应用程序
    ↓
rclcpp (ROS2 客户端库 — 统一的 C++ API)
    ↓
rmw (ROS Middleware 抽象层 — 统一的 C 接口)
    ↓
┌──────────┬──────────────┬──────────────┐
│ FastDDS  │ CycloneDDS   │ Connext(RTI) │
│ (eProsima)│ (Eclipse)   │ (商业)        │
└──────────┴──────────────┴──────────────┘
```

## 主要 DDS 供应商

| 供应商 | RMW 包 | 特点 |
|--------|--------|------|
| **FastDDS** | `rmw_fastrtps_cpp` | ROS2 Humble 默认, 功能完整, 社区活跃 |
| **CycloneDDS** | `rmw_cyclonedds_cpp` | Eclipse 项目, 轻量, 发现快, 内存占用低 |
| **Connext** | `rmw_connextdds` | RTI 公司, 商业级, 功能安全认证 |

## 切换方式

```bash
# 临时切换 — 同一节点代码, 不同 DDS 底层
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
ros2 run dds_vendor_lab vendor_test_node --ros-args -p mode:=pub

# 永久切换 (写入 ~/.bashrc)
echo "export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp" >> ~/.bashrc
```

## 线兼容性

所有 RTPS-compliant DDS 供应商在 **同一 ROS2 网络** 中可互操作:

```bash
# Machine A: FastDDS publisher
ros2 run dds_vendor_lab vendor_test_node --ros-args -p mode:=pub

# Machine B: CycloneDDS subscriber
RMW_IMPLEMENTATION=rmw_cyclonedds_cpp ros2 run dds_vendor_lab vendor_test_node --ros-args -p mode:=sub

# ✅ 两者可以通信! — DDS RTPS wire protocol 保证了互操作性
```

## 诊断命令

```bash
ros2 doctor          # ROS2 环境诊断
ros2 wtf             # 详细故障排除
ros2 topic list      # 查看发现的 topic
ros2 node info <n>   # 查看节点详情
```

## 与 DDS 原生的对应

| DDS 原生 | ROS2 |
|----------|------|
| 链接 `libddsc.so` (CycloneDDS) | `RMW_IMPLEMENTATION=rmw_cyclonedds_cpp` |
| 链接 `libfastrtps.so` (FastDDS) | `RMW_IMPLEMENTATION=rmw_fastrtps_cpp` |
| DDS Domain ID | `ROS_DOMAIN_ID` |
| RTPS wire 兼容 | 跨 RMW 供应商互操作 |
