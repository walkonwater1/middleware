# EtherCAT — 工业实时以太网现场总线

> 机器人高精度多轴同步控制的标准总线

## 定位：从总线到中间件

EtherCAT 本身是 **数据链路层协议**，但其上层协议栈实现了丰富的中间件抽象：

| 层 | 协议/功能 | 中间件类比 |
|----|----------|-----------|
| **CoE (CANopen over EtherCAT)** | 对象字典 + PDO/SDO | 完全等价于 CANopen 的 OD 模型 |
| **FoE (File over EtherCAT)** | 固件升级 | 远程文件传输 |
| **EoE (Ethernet over EtherCAT)** | TCP/IP 隧道 | 标准网络栈复用 |
| **SoE (Sercos over EtherCAT)** | IDN 参数访问 | 伺服驱动专用配置 |
| **DC (Distributed Clock)** | 分布式时钟同步 | 纳秒级多节点同步 |

## 核心概念

### 帧结构
```
Ethernet Frame
├─ MAC Header (DA=广播, SA=主站, Type=0x88A4)
├─ EtherCAT Header (长度 + 类型)
├─ Datagram 1 (寻址 + 命令 + 数据)
├─ Datagram 2
├─ ...
└─ FCS (CRC32)
```

### "飞读飞写" (On-the-Fly)
每个从站不存储整个帧，而是边接收边处理：
- 读命令: 在数据流经时向指定位置插入数据
- 写命令: 从指定位置取出数据并更新硬件

### 状态机 (ESM — EtherCAT State Machine)
```
Init → Pre-Operational → Safe-Operational → Operational
  ↑         ↓                ↓                  ↓
  └─────────┴────────────────┴──────────────────┘
                   (可回退)
```

| 状态 | 邮箱 (CoE) | PDO | DC | 说明 |
|------|-----------|-----|----|------|
| Init | ✗ | ✗ | ✗ | 仅寄存器访问 |
| Pre-Op | ✓ | ✗ | ✗ | 邮箱配置 |
| Safe-Op | ✓ | 输入 | ✗ | 可读过程数据 |
| Op | ✓ | ✓ | ✓ | 全速运行 |

### 分布式时钟 (DC)
- 第1个支持 DC 的从站作为参考时钟
- 其他从站计算与参考时钟的偏移
- 通过发送 ARMW 命令补偿时钟漂移
- 典型同步精度: **< 100ns**

## 本 Demo 架构

```
┌──────────────────────────────────────────────┐
│           ethercat_master.c                  │
│  模拟 SOEM 风格 API:                          │
│  - ec_init()   扫描从站                       │
│  - ec_config() 配置 PDO 映射 + FMMU + SM     │
│  - ec_dcsync() 分布式时钟同步                 │
│  - ec_send_processdata()  发送输出/接收输入    │
│  - ec_receive_processdata()                  │
│  - CoE SDO 读写                              │
└──────────┬───────────────────────────────────┘
           │  (共享内存模拟 ESC)
┌──────────┴───────────────────────────────────┐
│    ethercat_slave.c  (虚拟从站 #1)           │
│  - ESC 寄存器: DL状态/AL控制/WKC/SM/FMMU     │
│  - 过程数据: 模拟 3 轴位置 (I32×3)            │
│  - DC: 时间戳 + 漂移补偿                      │
│  - CoE: 1C12/1C13 (SM分配), 1600/1A00 (PDO)  │
└──────────────────────────────────────────────┘

周期: 1kHz (1ms), 3 个伺服轴
每个轴: 目标位置(I32) + 实际位置(I32) × 3 轴 = 24 字节输入 + 12 字节输出
```

## 编译运行

```bash
# 1. 依赖
sudo apt install cmake gcc

# 2. 编译
cd ethercat && mkdir build && cd build && cmake .. && make

# 3. 运行（两个终端）
./ethercat_slave    # 终端1: 启动虚拟从站
./ethercat_master   # 终端2: 启动主站
```

## 预期输出

```
[MASTER] Scanning bus...
[MASTER] Found 1 slave(s)
[MASTER] Slave #1: Vendor=0x00000002 Product=0x0652 (EL6521)
[MASTER] Configuring PDO mapping...
[MASTER]   TxPDO 0x1A00: 3 entries, 12 bytes
[MASTER]   RxPDO 0x1600: 3 entries, 12 bytes
[MASTER] DC sync config done (cycle=1000000 ns)
[MASTER] State change: INIT → PREOP → SAFEOP → OP

[MASTER] | cycle | axis0_target | axis0_actual | axis1_target | axis1_actual | dc_diff(ns) |
[MASTER] |   100 |         1024 |         1024 |         2048 |         2048 |         +52 |
[MASTER] |   200 |         1124 |         1124 |         1948 |         1948 |         -31 |
...
[SLAVE#1] Status: OP | WKC=3 | DC drift=+12ns
```

## 与其它中间件的结合场景

| 场景 | 组合 | 说明 |
|------|------|------|
| 机器人控制 | EtherCAT → ROS2 | ros2_control + EtherCAT 硬件接口 |
| 整车架构 | EtherCAT ← CANopen | CoE 对象字典复用 CANopen 模型 |
| 传感器融合 | EtherCAT → DDS | 高频传感器数据进入去中心化数据总线 |
| 跨域通信 | EtherCAT → SOME/IP | 车内实时域 ↔ 信息域网关 |

## 关键文件

| 文件 | 说明 |
|------|------|
| `ethercat.h` | ESC 寄存器定义、状态机、PDO 结构、DC 配置 |
| `ethercat_slave.c` | 虚拟从站：ESC 模拟 / PDO 数据 / DC 时钟 |
| `ethercat_master.c` | SOEM 风格主站：扫描 / 配置 / 同步 / 周期交换 |

## 进阶学习方向

- [ ] ESC 寄存器手册（Beckhoff ET1100 / ET1200）精读
- [ ] SOEM 源码分析：`ecx_send_processdata()` 内部实现
- [ ] 分布式时钟 (DC) 漂移补偿算法
- [ ] FMMU (Fieldbus Memory Management Unit) 映射原理
- [ ] SyncManager 配置：Buffered vs Mailbox 模式
- [ ] CoE 协议的 SDO 信息区与分段传输
- [ ] 多轴轨迹规划 + EtherCAT 同步插补
- [ ] IgH EtherCAT Master 对比分析
- [ ] 与 Linux PREEMPT_RT / Xenomai 实时内核配合

## 参考资源

- Beckhoff ETG.1000 — EtherCAT 技术规范
- [SOEM](https://github.com/OpenEtherCATsociety/SOEM) — Simple Open EtherCAT Master
- [IgH EtherCAT Master](https://etherlab.org/en/ethercat/) — Linux 主站实现
- Beckhoff Application Note: DC Distributed Clocks
