# DDS 学习

DDS (Data Distribution Service) 是 OMG 组织定义的去中心化实时数据分发标准，广泛用于自动驾驶、军工、航空等高可靠实时系统。ROS2 的底层通信也基于 DDS。

## DDS 核心概念

| 概念 | 说明 |
|------|------|
| Domain | 通信域, 同一域内的 Participant 才能通信 |
| Participant | 通信参与者, 一个进程通常创建一个 |
| Topic | 数据主题, 绑定一个数据类型 |
| Publisher | 发布者, 管理 DataWriter |
| Subscriber | 订阅者, 管理 DataReader |
| DataWriter | 实际写入数据的实体 |
| DataReader | 实际读取数据的实体 |
| QoS | 丰富的服务质量策略 (可靠性、持久性、截止时间等) |

## DDS 核心优势

- **去中心化**: 无 Broker, 通过自动发现 (DDSI-RTPS) 建立通信
- **丰富的 QoS**: 20+ 种策略, 精确控制实时行为
- **强类型**: IDL 定义数据类型, 编译时类型安全, 零序列化开销
- **高可靠性**: 支持可靠传输、历史数据重传

## 运行环境

```bash
# 安装 CycloneDDS C 库
sudo apt install cyclonedds-dev

# 确认 idlc 编译器可用
which idlc
```

## 构建 & 运行

```bash
mkdir build && cd build
cmake ..
make

# 终端1: 启动订阅者
./subscriber

# 终端2: 启动发布者
./publisher
```

构建时 CMake 会自动调用 `idlc` 将 `idl/HelloWorld.idl` 编译为 `VehicleState.h` 和 `VehicleState.c`。

## Demo 说明

| 文件 | 功能 |
|------|------|
| `idl/HelloWorld.idl` | IDL 类型定义 — 车辆状态 (速度/位置/电量/状态码) |
| `publisher.c` | 发布者: 定时发布模拟车辆状态, Reliable+TransientLocal QoS |
| `subscriber.c` | 订阅者: 接收并打印车辆状态, 含延迟计算和异常告警 |
| `CMakeLists.txt` | CMake 构建, 集成 idlc 代码生成步骤 |

## 关键 API (CycloneDDS C)

```c
// 创建 Participant
dds_entity_t pp = dds_create_participant(DOMAIN_ID, NULL, NULL);

// 从 IDL 生成的描述符创建 Topic
dds_entity_t tp = dds_create_topic(pp, &vehicle_VehicleState_desc,
                                    "VehicleState", NULL, NULL);

// 配置 QoS
dds_qos_t *qos = dds_create_qos();
dds_qset_reliability(qos, DDS_RELIABILITY_RELIABLE, DDS_SECS(1));
dds_qset_durability(qos, DDS_DURABILITY_TRANSIENT_LOCAL);

// 创建 Writer / Reader
dds_entity_t wr = dds_create_writer(pp, tp, qos, NULL);
dds_entity_t rd = dds_create_reader(pp, tp, qos, NULL);

// 写入 / 读取
dds_write(wr, &sample);
dds_read(rd, samples, infos, max_samples, max_samples);
```

## 主流 DDS 实现对比

| 实现 | 特点 | 许可证 |
|------|------|--------|
| CycloneDDS | 轻量、低延迟, ROS2 默认 | Eclipse / EPL-2.0 |
| Fast-DDS | 功能丰富, eProsima 维护 | Apache 2.0 |
| OpenDDS | C++ CORBA 风格, 支持 DDS Security | 自定义开源 |
| RTI Connext | 商业版, 功能最全, 认证最多 | 商业 / 社区版 |

## 进阶学习方向

- [ ] QoS 策略组合对通信行为的影响 (RELIABLE vs BEST_EFFORT, KEEP_LAST 深度)
- [ ] DDS Security 认证加密
- [ ] 多 Domain 隔离与桥接
- [ ] 延迟、吞吐量 benchmark (与 MQTT/ZMQ 对比)
- [ ] IDL 嵌套结构与 Key 字段的分实例管理
- [ ] Discovery 机制与组网行为分析
