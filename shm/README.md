# 共享内存 学习

共享内存（Shared Memory）是最快的进程间通信（IPC）方式，多个进程直接读写同一块物理内存区域，无需数据拷贝。

## 共享内存核心概念

| 概念 | 说明 |
|------|------|
| shm_open | POSIX 共享内存 — 创建/打开命名共享内存对象 |
| mmap | 将共享内存对象映射到进程地址空间 |
| ftruncate | 设置共享内存大小 |
| semaphore | 信号量 — 保护共享内存的并发访问 |
| shm_unlink | 删除共享内存对象 |
| /dev/shm | Linux 共享内存文件系统挂载点 |

## 为什么需要共享内存？

```
传统 IPC (管道/消息队列):         共享内存:
 Process A → [拷贝] → 内核 → [拷贝] → Process B    Process A → [读写] → 共享内存 ← [读写] → Process B
 延迟: ~微秒级                                   延迟: ~纳秒级 (无拷贝)
```

- **零拷贝**: 数据无需经过内核中转
- **超低延迟**: 适合实时系统 (自动驾驶传感器数据、机器人控制指令)
- **大带宽**: 适合传输大块数据 (图像、点云)

## 运行环境

Linux 原生支持 POSIX 共享内存，无需额外安装。

## 构建 & 运行

```bash
mkdir build && cd build
cmake ..
make

# ===== 基础示例: 无锁单写单读 =====
# 终端1: 启动读取端
./shm_reader

# 终端2: 启动写入端
./shm_writer

# ===== 进阶示例: 信号量保护并发访问 =====
# 终端1: 启动消费者
./shm_sem_consumer

# 终端2: 启动生产者
./shm_sem_producer
```

## Demo 说明

| 文件 | 功能 |
|------|------|
| `shm_writer.c` | 写入端: 创建共享内存, 写入模拟车辆传感器数据 |
| `shm_reader.c` | 读取端: 打开共享内存, 读取并打印数据 |
| `shm_sem_producer.c` | 生产者: 信号量保护的环形缓冲区写入 |
| `shm_sem_consumer.c` | 消费者: 信号量保护的环形缓冲区读取 |
| `ringbuf.h` | 无锁环形缓冲区 (单写单读, 原子操作) |
| `CMakeLists.txt` | CMake 构建脚本, 链接 pthread 和 rt |

## 关键 API

```c
// 创建/打开共享内存
int fd = shm_open("/my_shm", O_CREAT | O_RDWR, 0666);

// 设置大小
ftruncate(fd, size);

// 映射到地址空间
void *addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

// 使用完后清理
munmap(addr, size);
close(fd);
shm_unlink("/my_shm");
```

## 共享内存 + 中间件的结合

| 场景 | 方案 |
|------|------|
| ROS2 Loan Message | 发布者直接在共享内存中构造消息, 订阅者零拷贝读取 |
| DDS Zero Copy | CycloneDDS 的 Iceoryx 集成, 共享内存传输大块数据 |
| MQTT 大 Payload | MQTT 传输元数据 + 共享内存 key, 实际数据走共享内存 |
| 自动驾驶 | 摄像头/激光雷达数据通过共享内存跨进程传输 |

## 进阶学习方向

- [ ] 无锁队列 (Lock-Free Queue) 实现与 CAS 原理
- [ ] 多写多读场景的一致性保证
- [ ] 共享内存与 DDS/ROS2 的零拷贝集成
- [ ] huge page 对共享内存性能的影响
- [ ] memfd_create 匿名共享内存
- [ ] 跨机器共享内存 (RDMA)
