/**
 * ringbuf.h — 无锁环形缓冲区 (Single Producer, Single Consumer)
 *
 * 基于原子操作, 无需信号量/互斥锁。
 * 同时兼容 C11 (<stdatomic.h>) 和 C++17 (<atomic>)。
 *
 * 适用场景: 一个写入线程 + 一个读取线程。
 */

#ifndef RINGBUF_H
#define RINGBUF_H

#include <stdint.h>
#include <string.h>
#include <stddef.h>

#ifdef __cplusplus
  #include <atomic>
  #define RINGBUF_ATOMIC(T)  std::atomic<T>
  using ringbuf_atomic_u64 = std::atomic<uint64_t>;
#else
  #include <stdatomic.h>
  #define RINGBUF_ATOMIC(T)  _Atomic T
  typedef atomic_uint_fast64_t ringbuf_atomic_u64;
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 环形缓冲区控制结构 (放置在共享内存头部)
 * ============================================================ */
#define RINGBUF_CAPACITY_DEFAULT 256  /* 默认容量 (必须是2的幂) */

typedef struct {
    ringbuf_atomic_u64 write_index;
    ringbuf_atomic_u64 read_index;
    uint64_t           capacity;
    uint64_t           elem_size;
    char               data[];    /* 柔性数组: 数据区 */
} ringbuf_ctrl_t;

/* ============================================================
 * 初始化 (调用者负责将 ctrl 放在共享内存中)
 * ============================================================ */
static inline void ringbuf_init(ringbuf_ctrl_t *ctrl,
                                 uint64_t capacity,
                                 uint64_t elem_size)
{
#ifdef __cplusplus
    ctrl->write_index.store(0, std::memory_order_relaxed);
    ctrl->read_index.store(0, std::memory_order_relaxed);
#else
    atomic_store_explicit(&ctrl->write_index, 0, memory_order_relaxed);
    atomic_store_explicit(&ctrl->read_index,  0, memory_order_relaxed);
#endif
    ctrl->capacity  = capacity;
    ctrl->elem_size = elem_size;
}

/* ============================================================
 * 总大小计算
 * ============================================================ */
static inline size_t ringbuf_total_size(uint64_t capacity, uint64_t elem_size)
{
    return sizeof(ringbuf_ctrl_t) + capacity * elem_size;
}

/* ============================================================
 * 写入一个元素 (生产者调用)
 * 返回 0 成功, -1 缓冲区满
 * ============================================================ */
static inline int ringbuf_write(ringbuf_ctrl_t *ctrl, const void *elem)
{
#ifdef __cplusplus
    uint64_t w = ctrl->write_index.load(std::memory_order_relaxed);
    uint64_t r = ctrl->read_index.load(std::memory_order_acquire);
#else
    uint64_t w = atomic_load_explicit(&ctrl->write_index, memory_order_relaxed);
    uint64_t r = atomic_load_explicit(&ctrl->read_index,  memory_order_acquire);
#endif

    if (w - r >= ctrl->capacity) {
        return -1;  /* 缓冲区满 */
    }

    uint64_t idx = w % ctrl->capacity;
    memcpy(&ctrl->data[idx * ctrl->elem_size], elem, ctrl->elem_size);

#ifdef __cplusplus
    ctrl->write_index.store(w + 1, std::memory_order_release);
#else
    atomic_store_explicit(&ctrl->write_index, w + 1, memory_order_release);
#endif
    return 0;
}

/* ============================================================
 * 读取一个元素 (消费者调用)
 * 返回 0 成功, -1 缓冲区空
 * ============================================================ */
static inline int ringbuf_read(ringbuf_ctrl_t *ctrl, void *elem)
{
#ifdef __cplusplus
    uint64_t r = ctrl->read_index.load(std::memory_order_relaxed);
    uint64_t w = ctrl->write_index.load(std::memory_order_acquire);
#else
    uint64_t r = atomic_load_explicit(&ctrl->read_index,  memory_order_relaxed);
    uint64_t w = atomic_load_explicit(&ctrl->write_index, memory_order_acquire);
#endif

    if (r >= w) {
        return -1;  /* 缓冲区空 */
    }

    uint64_t idx = r % ctrl->capacity;
    memcpy(elem, &ctrl->data[idx * ctrl->elem_size], ctrl->elem_size);

#ifdef __cplusplus
    ctrl->read_index.store(r + 1, std::memory_order_release);
#else
    atomic_store_explicit(&ctrl->read_index, r + 1, memory_order_release);
#endif
    return 0;
}

/* ============================================================
 * 查询可用元素数量
 * ============================================================ */
static inline uint64_t ringbuf_count(const ringbuf_ctrl_t *ctrl)
{
#ifdef __cplusplus
    uint64_t w = ctrl->write_index.load(std::memory_order_acquire);
    uint64_t r = ctrl->read_index.load(std::memory_order_acquire);
#else
    uint64_t w = atomic_load_explicit(
        (ringbuf_atomic_u64*)&ctrl->write_index, memory_order_acquire); // NOLINT
    uint64_t r = atomic_load_explicit(
        (ringbuf_atomic_u64*)&ctrl->read_index, memory_order_acquire);  // NOLINT
#endif
    return w - r;
}

#ifdef __cplusplus
}
#endif

#endif /* RINGBUF_H */
