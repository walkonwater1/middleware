/**
 * lockfree_queue.h — 无锁有界队列 (Multi-Producer, Multi-Consumer)
 *
 * 基于 Vyukov 风格槽位 turn 计数器 + CAS, 无需互斥锁/信号量.
 * 同时兼容 C11 (<stdatomic.h>) 和 C++17 (<atomic>).
 *
 * 适用场景: 多个写入线程 + 多个读取线程.
 *
 * 与 ringbuf.h 的对比:
 *   - ringbuf.h : SPSC, load/store only, 吞吐量最高
 *   - 本文件   : MPMC, CAS + spin, 灵活性最高
 *
 * 参考: Dmitry Vyukov, "Bounded MPMC queue" (1024cores.net)
 */

#ifndef LOCKFREE_QUEUE_H
#define LOCKFREE_QUEUE_H

#include <stdint.h>
#include <string.h>
#include <stddef.h>

#ifdef __cplusplus
  #include <atomic>
  #define LFQ_ATOMIC(T)  std::atomic<T>
  using lfq_atomic_u64 = std::atomic<uint64_t>;
#else
  #include <stdatomic.h>
  #define LFQ_ATOMIC(T)  _Atomic T
  typedef _Atomic uint64_t lfq_atomic_u64;
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 常量
 * ============================================================ */
#define LFQ_CACHE_LINE    64
#define LFQ_PAD_SIZE      (LFQ_CACHE_LINE - sizeof(lfq_atomic_u64))

/* ============================================================
 * 无锁队列控制结构 (放置在共享内存头部)
 *
 *   head / tail 各占一条缓存行, 避免生产者-消费者伪共享.
 * ============================================================ */
typedef struct {
    lfq_atomic_u64 head;
    char           pad1[LFQ_PAD_SIZE];  /* 缓存行填充 */
    lfq_atomic_u64 tail;
    char           pad2[LFQ_PAD_SIZE];  /* 缓存行填充 */
    uint64_t       capacity;            /* 必须为 2 的幂 */
    uint64_t       elem_size;
    uint64_t       mask;                /* capacity - 1 */
    char           cells[];             /* 柔性数组: 槽位区 */
} lfq_ctrl_t;

/* ============================================================
 * 内部辅助: 定位槽位的 turn 和 data
 * ============================================================ */
static inline size_t lfq_cell_stride(const lfq_ctrl_t *ctrl)
{
    return sizeof(lfq_atomic_u64) + ctrl->elem_size;
}

static inline lfq_atomic_u64* lfq_cell_turn(lfq_ctrl_t *ctrl, uint64_t idx)
{
    return (lfq_atomic_u64*)(ctrl->cells + idx * lfq_cell_stride(ctrl));
}

static inline void* lfq_cell_data(lfq_ctrl_t *ctrl, uint64_t idx)
{
    return ctrl->cells + idx * lfq_cell_stride(ctrl) + sizeof(lfq_atomic_u64);
}

/* ============================================================
 * 初始化 (调用者负责将 ctrl 放在共享内存中)
 *
 *   turn[i] = i  起始 lap 号
 *   head = tail = 0
 * ============================================================ */
static inline void lfq_init(lfq_ctrl_t *ctrl,
                             uint64_t capacity,
                             uint64_t elem_size)
{
#ifdef __cplusplus
    ctrl->head.store(0, std::memory_order_relaxed);
    ctrl->tail.store(0, std::memory_order_relaxed);
#else
    atomic_store_explicit(&ctrl->head, 0, memory_order_relaxed);
    atomic_store_explicit(&ctrl->tail, 0, memory_order_relaxed);
#endif
    ctrl->capacity  = capacity;
    ctrl->elem_size = elem_size;
    ctrl->mask      = capacity - 1;

    /* 初始化每个槽位的 turn = i */
    for (uint64_t i = 0; i < capacity; i++) {
#ifdef __cplusplus
        lfq_cell_turn(ctrl, i)->store(i, std::memory_order_relaxed);
#else
        atomic_store_explicit(lfq_cell_turn(ctrl, i), i, memory_order_relaxed);
#endif
    }
}

/* ============================================================
 * 总大小计算
 * ============================================================ */
static inline size_t lfq_total_size(uint64_t capacity, uint64_t elem_size)
{
    return sizeof(lfq_ctrl_t)
           + capacity * (sizeof(lfq_atomic_u64) + elem_size);
}

/* ============================================================
 * 入队 (任意线程可调用)
 *
 *   1. CAS 争抢 tail 索引 (multi-producer)
 *   2. 自旋等待 turn[slot] == tail (上一个消费者释放)
 *   3. memcpy 数据 → turn[slot] = tail + 1
 *
 *   返回 0 成功, -1 队列满
 * ============================================================ */
static inline int lfq_enqueue(lfq_ctrl_t *ctrl, const void *elem)
{
    uint64_t tail, head, idx;
    lfq_atomic_u64 *turn;

    for (;;) {
#ifdef __cplusplus
        tail = ctrl->tail.load(std::memory_order_relaxed);
        head = ctrl->head.load(std::memory_order_acquire);
#else
        tail = atomic_load_explicit(&ctrl->tail, memory_order_relaxed);
        head = atomic_load_explicit(&ctrl->head, memory_order_acquire);
#endif

        if (tail - head >= ctrl->capacity) {
            return -1;  /* 队列满 */
        }

#ifdef __cplusplus
        if (ctrl->tail.compare_exchange_weak(tail, tail + 1,
                std::memory_order_acq_rel,
                std::memory_order_relaxed))
#else
        if (atomic_compare_exchange_weak_explicit(
                &ctrl->tail, &tail, tail + 1,
                memory_order_acq_rel,
                memory_order_relaxed))
#endif
        {
            break;  /* 成功占用槽位 tail */
        }
        /* CAS 失败 → 其他生产者抢占, 重试 */
    }

    idx  = tail & ctrl->mask;
    turn = lfq_cell_turn(ctrl, idx);

    /* 自旋等待槽位被消费者释放: turn[slot] == tail */
    while (1) {
#ifdef __cplusplus
        uint64_t turn_val = turn->load(std::memory_order_acquire);
#else
        uint64_t turn_val = atomic_load_explicit(turn, memory_order_acquire);
#endif
        if (turn_val == tail) break;
        /* 短暂自旋 — 消费者正在读取, 很快会释放 */
    }

    /* 写入数据 */
    memcpy(lfq_cell_data(ctrl, idx), elem, ctrl->elem_size);

    /* 通知消费者: 数据就绪 */
#ifdef __cplusplus
    turn->store(tail + 1, std::memory_order_release);
#else
    atomic_store_explicit(turn, tail + 1, memory_order_release);
#endif
    return 0;
}

/* ============================================================
 * 出队 (任意线程可调用)
 *
 *   1. CAS 争抢 head 索引 (multi-consumer)
 *   2. 自旋等待 turn[slot] == head + 1 (生产者写入完成)
 *   3. memcpy 数据 → turn[slot] = head + capacity
 *
 *   返回 0 成功, -1 队列空
 * ============================================================ */
static inline int lfq_dequeue(lfq_ctrl_t *ctrl, void *elem)
{
    uint64_t head, tail, idx;
    lfq_atomic_u64 *turn;

    for (;;) {
#ifdef __cplusplus
        head = ctrl->head.load(std::memory_order_relaxed);
        tail = ctrl->tail.load(std::memory_order_acquire);
#else
        head = atomic_load_explicit(&ctrl->head, memory_order_relaxed);
        tail = atomic_load_explicit(&ctrl->tail, memory_order_acquire);
#endif

        if (head >= tail) {
            return -1;  /* 队列空 */
        }

#ifdef __cplusplus
        if (ctrl->head.compare_exchange_weak(head, head + 1,
                std::memory_order_acq_rel,
                std::memory_order_relaxed))
#else
        if (atomic_compare_exchange_weak_explicit(
                &ctrl->head, &head, head + 1,
                memory_order_acq_rel,
                memory_order_relaxed))
#endif
        {
            break;  /* 成功占用槽位 head */
        }
        /* CAS 失败 → 其他消费者抢占, 重试 */
    }

    idx  = head & ctrl->mask;
    turn = lfq_cell_turn(ctrl, idx);

    /* 自旋等待生产者写入完成: turn[slot] == head + 1 */
    while (1) {
#ifdef __cplusplus
        uint64_t turn_val = turn->load(std::memory_order_acquire);
#else
        uint64_t turn_val = atomic_load_explicit(turn, memory_order_acquire);
#endif
        if (turn_val == head + 1) break;
        /* 短暂自旋 — 生产者正在写入, 很快会完成 */
    }

    /* 读取数据 */
    memcpy(elem, lfq_cell_data(ctrl, idx), ctrl->elem_size);

    /* 通知生产者: 槽位可复用 */
#ifdef __cplusplus
    turn->store(head + ctrl->capacity, std::memory_order_release);
#else
    atomic_store_explicit(turn, head + ctrl->capacity, memory_order_release);
#endif
    return 0;
}

/* ============================================================
 * 查询近似元素数量 (快照, 非原子一致)
 * ============================================================ */
static inline uint64_t lfq_count(const lfq_ctrl_t *ctrl)
{
#ifdef __cplusplus
    uint64_t t = ctrl->tail.load(std::memory_order_acquire);
    uint64_t h = ctrl->head.load(std::memory_order_acquire);
#else
    uint64_t t = atomic_load_explicit(
        (lfq_atomic_u64*)&ctrl->tail, memory_order_acquire); // NOLINT
    uint64_t h = atomic_load_explicit(
        (lfq_atomic_u64*)&ctrl->head, memory_order_acquire); // NOLINT
#endif
    return (t >= h) ? (t - h) : 0;
}

#ifdef __cplusplus
}
#endif

#endif /* LOCKFREE_QUEUE_H */
