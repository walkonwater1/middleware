/**
 * shm_cxx.hpp — POSIX 共享内存 C++17 RAII 封装
 *
 * 原生的 shm_open/mmap 需要手动管理 4 个资源:
 *   fd → close → munmap → shm_unlink
 * 漏掉任何一个都会内存泄漏或残留 /dev/shm 文件。
 *
 * 本封装提供三层抽象, 把"容易写错"变成"很难写错":
 *
 *   层级        | 管理什么                      | RAII
 *   ───────────┼──────────────────────────────┼─────
 *   ShmHandle  | 内核对象 (shm_open/unlink)    | fd + 名字
 *   ShmMapper  | 地址映射 (mmap/munmap)        | 指针 + 大小
 *   Shm<T>     | 类型安全的数据视图            | 持有 Handle + Mapper
 *
 * 用法:
 *   // ===== 基本用法 =====
 *   #include "shm_cxx.hpp"
 *
 *   shm::Shm<SensorData> shm("/my_sensor", shm::Create);  // 创建
 *   shm->speed = 120.0;
 *
 *   shm::Shm<SensorData> shm("/my_sensor", shm::Open);    // 打开(只读)
 *   printf("speed=%.1f\n", shm->speed);
 *
 *   // ===== 环形缓冲区 =====
 *   shm::ShmRingBuf<JointCmd, 256> ring("/joint_cmds", shm::Create);
 *   ring.write(cmd);          // 生产者
 *   ring.read(&cmd);          // 消费者
 *
 * 编译: g++ -std=c++17 ... -lrt -lpthread
 */

#ifndef SHM_CXX_HPP
#define SHM_CXX_HPP

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdexcept>
#include <string>
#include <utility>
#include <atomic>

/* 复用 C 语言的 ringbuf (无锁 SPSC 队列) */
#include "ringbuf.h"
/* 无锁 MPMC 有界队列 */
#include "lockfree_queue.h"

namespace shm {

// ============================================================================
// 异常
// ============================================================================
class Error : public std::runtime_error {
public:
    explicit Error(const std::string& msg) : std::runtime_error(msg) {}
};

// ============================================================================
// Tag types — 区分"创建"还是"打开"
// ============================================================================
struct CreateTag {};   static constexpr CreateTag Create{};
struct OpenTag {};     static constexpr OpenTag   Open{};

// ============================================================================
// ShmHandle — 管理内核共享内存对象 (fd + 名字)
//
//   创建者: shm_open(O_CREAT|O_EXCL) → 析构时 shm_unlink
//   打开者: shm_open(O_RDWR)        → 析构时 close(fd) only
// ============================================================================
class ShmHandle {
public:
    // ---- 空状态 (延迟初始化) ----
    ShmHandle() = default;

    // ---- 创建 ----
    ShmHandle(const char* name, CreateTag, mode_t mode = 0666)
        : name_(name), owner_(true)
    {
        fd_ = ::shm_open(name, O_CREAT | O_RDWR | O_EXCL, mode);
        if (fd_ == -1)
            throw Error(std::string("shm_open(CREATE) '") + name + "': " + strerror(errno));
    }

    // ---- 打开 ----
    ShmHandle(const char* name, OpenTag, mode_t mode = 0666)
        : name_(name), owner_(false)
    {
        fd_ = ::shm_open(name, O_RDWR, mode);
        if (fd_ == -1)
            throw Error(std::string("shm_open(OPEN) '") + name + "': " + strerror(errno));
    }

    ~ShmHandle() noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
            if (owner_) ::shm_unlink(name_.c_str());
        }
    }

    // 禁止拷贝
    ShmHandle(const ShmHandle&) = delete;
    ShmHandle& operator=(const ShmHandle&) = delete;

    // 允许移动
    ShmHandle(ShmHandle&& other) noexcept
        : fd_(other.fd_), name_(std::move(other.name_)), owner_(other.owner_)
    {
        other.fd_ = -1;
        other.owner_ = false;
    }

    ShmHandle& operator=(ShmHandle&& other) noexcept {
        if (this != &other) {
            this->~ShmHandle();
            fd_    = other.fd_;
            name_  = std::move(other.name_);
            owner_ = other.owner_;
            other.fd_    = -1;
            other.owner_ = false;
        }
        return *this;
    }

    int  fd()     const { return fd_; }
    bool is_owner() const { return owner_; }
    const std::string& name() const { return name_; }

    void truncate(off_t size) {
        if (::ftruncate(fd_, size) == -1)
            throw Error(std::string("ftruncate: ") + strerror(errno));
    }

private:
    int         fd_ = -1;
    std::string name_;
    bool        owner_ = false;
};

// ============================================================================
// ShmMapper — 管理 mmap/munmap 映射
// ============================================================================
class ShmMapper {
public:
    // ---- 空状态 (延迟初始化) ----
    ShmMapper() = default;

    ShmMapper(int fd, size_t size, int prot = PROT_READ | PROT_WRITE)
        : size_(size)
    {
        addr_ = ::mmap(nullptr, size, prot, MAP_SHARED, fd, 0);
        if (addr_ == MAP_FAILED)
            throw Error(std::string("mmap: ") + strerror(errno));
    }

    ~ShmMapper() noexcept {
        if (addr_ && addr_ != MAP_FAILED)
            ::munmap(addr_, size_);
    }

    ShmMapper(const ShmMapper&) = delete;
    ShmMapper& operator=(const ShmMapper&) = delete;

    ShmMapper(ShmMapper&& other) noexcept
        : addr_(other.addr_), size_(other.size_)
    {
        other.addr_ = nullptr;
        other.size_ = 0;
    }

    ShmMapper& operator=(ShmMapper&& other) noexcept {
        if (this != &other) {
            this->~ShmMapper();
            addr_ = other.addr_; size_ = other.size_;
            other.addr_ = nullptr; other.size_ = 0;
        }
        return *this;
    }

    void* data()       { return addr_; }
    const void* data() const { return addr_; }
    size_t size() const     { return size_; }

private:
    void*  addr_ = nullptr;
    size_t size_ = 0;
};

// ============================================================================
// Shm<T> — 类型安全的共享内存视图
//
//   持有 ShmHandle + ShmMapper, 提供 operator-> / operator*
// ============================================================================
template<typename T>
class Shm {
public:
    // ---- 创建 ----
    Shm(const char* name, CreateTag tag, mode_t mode = 0666)
        : handle_(name, tag, mode)
    {
        handle_.truncate(sizeof(T));
        mapper_  = ShmMapper(handle_.fd(), sizeof(T));
        ptr_     = static_cast<T*>(mapper_.data());
        // 默认构造 (无状态的 POD 清零)
        new (ptr_) T{};
    }

    // ---- 打开已有 ----
    Shm(const char* name, OpenTag tag, mode_t mode = 0666)
        : handle_(name, tag, mode)
    {
        mapper_  = ShmMapper(handle_.fd(), sizeof(T));
        ptr_     = static_cast<T*>(mapper_.data());
    }

    // ---- 从已有的 Handle + Mapper 构造 (内部用) ----
    Shm(ShmHandle handle, ShmMapper mapper, T* ptr)
        : handle_(std::move(handle)), mapper_(std::move(mapper)), ptr_(ptr) {}

    // 禁止拷贝, 允许移动
    Shm(const Shm&) = delete;
    Shm& operator=(const Shm&) = delete;
    Shm(Shm&&) = default;
    Shm& operator=(Shm&&) = default;

    // ---- 类型安全访问 ----
    T*       operator->()       { return ptr_; }
    const T* operator->() const { return ptr_; }
    T&       operator*()        { return *ptr_; }
    const T& operator*()  const { return *ptr_; }
    T*       get()              { return ptr_; }

    bool is_owner() const { return handle_.is_owner(); }

private:
    ShmHandle handle_;
    ShmMapper mapper_;
    T*        ptr_ = nullptr;
};

// ============================================================================
// ShmRingBuf<T, Capacity> — 共享内存无锁环形缓冲区
//
//   基于 ringbuf.h 的原子操作 SPSC 队列, 整个控制结构+数据区放在共享内存中
//
//   用法:
//     // 生产者
//     ShmRingBuf<JointCmd, 256> ring("/robot_joint_cmds", Create);
//     ring.write(cmd);
//
//     // 消费者 (另一个进程)
//     ShmRingBuf<JointCmd, 256> ring("/robot_joint_cmds", Open);
//     JointCmd cmd;
//     while (ring.read(&cmd) == 0) { process(cmd); }
// ============================================================================
template<typename T, size_t Capacity>
class ShmRingBuf {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of 2 (required by ringbuf.h)");

public:
    using CtrlType = ringbuf_ctrl_t;

    // ---- 创建 ----
    ShmRingBuf(const char* name, CreateTag tag, mode_t mode = 0666) {
        size_t total = ringbuf_total_size(Capacity, sizeof(T));
        handle_ = ShmHandle(name, tag, mode);
        handle_.truncate(static_cast<off_t>(total));
        mapper_ = ShmMapper(handle_.fd(), total);
        ctrl_   = static_cast<CtrlType*>(mapper_.data());
        ringbuf_init(ctrl_, Capacity, sizeof(T));
        data_   = reinterpret_cast<T*>(ctrl_->data);
    }

    // ---- 打开已有 ----
    ShmRingBuf(const char* name, OpenTag tag, mode_t mode = 0666) {
        size_t total = ringbuf_total_size(Capacity, sizeof(T));
        handle_ = ShmHandle(name, tag, mode);
        mapper_ = ShmMapper(handle_.fd(), total);
        ctrl_   = static_cast<CtrlType*>(mapper_.data());
        data_   = reinterpret_cast<T*>(ctrl_->data);
    }

    ShmRingBuf(const ShmRingBuf&) = delete;
    ShmRingBuf& operator=(const ShmRingBuf&) = delete;
    ShmRingBuf(ShmRingBuf&&) = default;
    ShmRingBuf& operator=(ShmRingBuf&&) = default;

    // ---- 操作 ----
    int write(const T& elem) {
        return ringbuf_write(ctrl_, &elem);
    }

    int read(T* elem) {
        return ringbuf_read(ctrl_, elem);
    }

    uint64_t count() const {
        return ringbuf_count(ctrl_);
    }

    bool is_full()  const { return count() >= Capacity; }
    bool is_empty() const { return count() == 0; }

    T*       data_ptr()       { return data_; }
    CtrlType* ctrl()          { return ctrl_; }

private:
    ShmHandle  handle_;
    ShmMapper  mapper_;
    CtrlType*  ctrl_ = nullptr;
    T*         data_ = nullptr;
};

// ============================================================================
// ShmLockFreeQueue<T, Capacity> — 共享内存无锁 MPMC 有界队列
//
//   基于 lockfree_queue.h 的 CAS 槽位 turn 计数算法, 支持多生产者多消费者.
//   整个控制结构+数据区放在共享内存中.
//
//   与 ShmRingBuf 的区别:
//     - ShmRingBuf     : SPSC (单写单读), load/store only, 最高吞吐
//     - ShmLockFreeQueue: MPMC (多写多读), CAS + spin, 最灵活
//
//   用法:
//     // 多生产者 (多个进程/线程)
//     ShmLockFreeQueue<Task, 1024> q("/task_queue", Create);
//     q.enqueue(task);
//
//     // 多消费者 (多个进程/线程)
//     ShmLockFreeQueue<Task, 1024> q("/task_queue", Open);
//     Task t;
//     while (q.dequeue(&t) == 0) { process(t); }
// ============================================================================
template<typename T, size_t Capacity>
class ShmLockFreeQueue {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of 2 (required by lockfree_queue.h)");

public:
    using CtrlType = lfq_ctrl_t;

    // ---- 创建 ----
    ShmLockFreeQueue(const char* name, CreateTag tag, mode_t mode = 0666) {
        size_t total = lfq_total_size(Capacity, sizeof(T));
        handle_ = ShmHandle(name, tag, mode);
        handle_.truncate(static_cast<off_t>(total));
        mapper_ = ShmMapper(handle_.fd(), total);
        ctrl_   = static_cast<CtrlType*>(mapper_.data());
        lfq_init(ctrl_, Capacity, sizeof(T));
        data_   = reinterpret_cast<T*>(
            ctrl_->cells + sizeof(lfq_atomic_u64));  /* 第一个槽位数据区 */
    }

    // ---- 打开已有 ----
    ShmLockFreeQueue(const char* name, OpenTag tag, mode_t mode = 0666) {
        size_t total = lfq_total_size(Capacity, sizeof(T));
        handle_ = ShmHandle(name, tag, mode);
        mapper_ = ShmMapper(handle_.fd(), total);
        ctrl_   = static_cast<CtrlType*>(mapper_.data());
        data_   = reinterpret_cast<T*>(
            ctrl_->cells + sizeof(lfq_atomic_u64));
    }

    ShmLockFreeQueue(const ShmLockFreeQueue&) = delete;
    ShmLockFreeQueue& operator=(const ShmLockFreeQueue&) = delete;
    ShmLockFreeQueue(ShmLockFreeQueue&&) = default;
    ShmLockFreeQueue& operator=(ShmLockFreeQueue&&) = default;

    // ---- 操作 ----
    int enqueue(const T& elem) {
        return lfq_enqueue(ctrl_, &elem);
    }

    int dequeue(T* elem) {
        return lfq_dequeue(ctrl_, elem);
    }

    uint64_t count() const {
        return lfq_count(ctrl_);
    }

    bool is_full()  const { return count() >= Capacity; }
    bool is_empty() const { return count() == 0; }

    T*        data_ptr()       { return data_; }
    CtrlType* ctrl()           { return ctrl_; }

private:
    ShmHandle  handle_;
    ShmMapper  mapper_;
    CtrlType*  ctrl_ = nullptr;
    T*         data_ = nullptr;
};

} // namespace shm

#endif // SHM_CXX_HPP
