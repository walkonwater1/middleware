# 共享内存 — POSIX Shared Memory

> 最快的 IPC 方式：多进程直接读写同一块物理内存，零内核拷贝，~ns 级延迟。

## 文件结构

```
shm/
├── include/
│   ├── shm_cxx.hpp       # C++17 RAII 封装 (ShmHandle / ShmMapper / Shm<T> / ShmRingBuf)
│   └── ringbuf.h         # 无锁环形缓冲区 (C/C++ 双兼容, SPSC, 原子操作)
├── demo/
│   ├── demo_basic.cpp    # 基本读写 — 车辆传感器数据共享
│   └── demo_ringbuf.cpp  # 无锁环形缓冲区 — 机器人关节指令队列
├── scripts/
│   └── run_demo.sh       # 一键编译 + 运行
├── CMakeLists.txt
└── README.md
```

## 为什么需要共享内存？

```
传统 IPC (管道/消息队列):              共享内存:
 Process A → [拷贝] → 内核 → [拷贝]     Process A ──读/写──→ 共享内存 ←──读/写── Process B
            → Process B
 延迟: ~μs 级                           延迟: ~ns 级 (零拷贝, 绕过内核)
```

- **零拷贝**: 数据不经内核中转，直接读写物理内存页
- **超低延迟**: 适合实时系统 (自动驾驶传感器、机器人控制指令)
- **大带宽**: 适合大块数据 (图像、点云)
- **持久性**: 不随进程退出而消失 (`/dev/shm` 下可见)

## shm_cxx.hpp 设计

原生的 `shm_open/mmap` 需要手动管理 4 个资源，任何一个泄漏都会导致内存泄漏或 `/dev/shm` 残留：

```c
// ❌ 原生 C API — 6 个步骤, 4 个资源
int fd = shm_open("/x", O_CREAT|O_RDWR, 0666);
ftruncate(fd, size);
void *p = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
close(fd);
// ... 使用 p ...
munmap(p, size);       // 容易忘
shm_unlink("/x");      // 容易忘
```

```cpp
// ✅ shm_cxx.hpp — 2 行, 零泄漏
shm::Shm<Sensor> shm("/x", shm::Create);
shm->speed = 120.0;
// 析构自动: munmap → close → shm_unlink
```

### 三层抽象

| 类 | 管理资源 | RAII 行为 |
|----|---------|----------|
| `ShmHandle` | `fd` + 名字 | `shm_open` → 析构 `close` + `shm_unlink`(仅 owner) |
| `ShmMapper` | 指针 + 大小 | `mmap` → 析构 `munmap` |
| `Shm<T>` | Handle + Mapper | 持有以上两者, 提供 `operator->` / `operator*` |
| `ShmRingBuf<T, N>` | Handle + Mapper + ringbuf | 无锁 SPSC 队列 `write()` / `read()` / `count()` |

**关键设计：**
- **Create vs Open**: `shm::Create` 创建内核对象（析构 `shm_unlink`）；`shm::Open` 仅打开（析构只 `close`）
- **Move-only**: 禁止拷贝，允许移动，像 `std::unique_ptr` 一样确定所有权
- **异常安全**: 构造失败自动回滚，不残留半初始化资源

## 构建 & 运行

```bash
# 一键编译 + 运行
bash scripts/run_demo.sh           # basic demo (车辆传感器)
bash scripts/run_demo.sh ringbuf   # ringbuf demo (关节指令队列)
bash scripts/run_demo.sh build     # 仅编译
bash scripts/run_demo.sh clean     # 清理

# 或手动分步:
cd build && cmake .. && make -j$(nproc)

# Demo 1: 基本读写
./build/demo_basic writer    # 终端 1 — 写入端
./build/demo_basic reader    # 终端 2 — 读取端

# Demo 2: 无锁环形缓冲区
./build/demo_ringbuf producer   # 终端 1 — 20Hz 写入关节轨迹
./build/demo_ringbuf consumer   # 终端 2 — 读取并模拟执行
```

## API 速查

### Shm<T> — 类型安全共享内存

```cpp
#include "include/shm_cxx.hpp"

struct Sensor {
    double speed;
    double temperature;
};

// 创建 (写入端)
shm::Shm<Sensor> shm("/sensor", shm::Create);  // shm_open + ftruncate + mmap
shm->speed = 120.5;                             // 直接写入共享内存!

// 打开 (读取端 — 另一个进程)
shm::Shm<Sensor> shm("/sensor", shm::Open);     // shm_open + mmap
printf("speed=%.1f\n", shm->speed);             // 直接读取!
// 析构: munmap + close (不 unlink, 不是 owner)
```

### ShmRingBuf<T, N> — 无锁环形缓冲区

```cpp
#include "include/shm_cxx.hpp"

struct JointCmd { uint64_t seq; double pos[6]; };

// 生产者
shm::ShmRingBuf<JointCmd, 256> q("/joints", shm::Create);
q.write(cmd);              // 返回 0 成功, -1 缓冲区满
q.count();                 // 当前队列长度

// 消费者 (另一个进程)
shm::ShmRingBuf<JointCmd, 256> q("/joints", shm::Open);
JointCmd cmd;
while (q.read(&cmd) == 0) { process(cmd); }
```

## 与其他中间件结合

| 场景 | 方案 |
|------|------|
| ROS2 Loan Message | 发布者直接在共享内存中构造消息, 订阅者零拷贝读取 |
| DDS Zero Copy | CycloneDDS + iceoryx 共享内存传输大块数据 |
| MQTT 大 Payload | MQTT 传输元数据 + shm key, 实际数据走共享内存 |
| 自动驾驶 | 摄像头/激光雷达点云通过共享内存跨进程传输 |

## 进阶方向

- [ ] 无锁队列 CAS 原理 — `ringbuf.h` 源码解读
- [ ] SPSC → MPMC 多写多读扩展
- [ ] 共享内存 + DDS/ROS2 零拷贝集成
- [ ] Huge Page (2MB/1GB) 性能影响
- [ ] `memfd_create` 匿名共享内存
- [ ] 跨机器 RDMA
