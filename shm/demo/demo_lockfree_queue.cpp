/**
 * demo_lockfree_queue.cpp — 使用 ShmLockFreeQueue<T> 实现多生产者多消费者任务分发
 *
 * 场景: 多个传感器 (producers) 并发写入数据 → 多个处理器 (consumers) 并发取出执行
 *
 * 与 demo_ringbuf.cpp 的区别:
 *   - demo_ringbuf.cpp          : SPSC, 1 producer + 1 consumer
 *   - demo_lockfree_queue.cpp   : MPMC, N producers + M consumers (可配置)
 *
 * 用法:
 *   ./demo_lockfree_queue producer --threads=4      # 4 个生产者进程/线程
 *   ./demo_lockfree_queue consumer --threads=2      # 2 个消费者进程/线程
 *   ./demo_lockfree_queue both --producers=4 --consumers=2  # 单进程测试
 */

#include <iostream>
#include <iomanip>
#include <cstring>
#include <csignal>
#include <chrono>
#include <thread>
#include <vector>
#include <sstream>
#include <atomic>
#include <cmath>

#include "shm_cxx.hpp"

using namespace shm;

// ============================================================
// 数据结构
// ============================================================
struct WorkItem {
    uint64_t sequence     = 0;     // 8 bytes
    uint64_t timestamp_us = 0;     // 8 bytes
    int      producer_id  = -1;    // 4 bytes
    char     payload[44]  = {};    // 44 bytes → total = 64 bytes
};

// 编译期验证 (缓存行对齐)
static_assert(sizeof(WorkItem) == 64, "WorkItem should be 64 bytes (one cache line)");

constexpr const char* SHM_NAME = "/lfq_demo_queue";
constexpr size_t      QUEUE_CAPACITY = 1024;

// ============================================================
// 信号处理
// ============================================================
static std::atomic<bool> running{true};
void on_signal(int) { running = false; }

// ============================================================
// 命令行参数解析
// ============================================================
struct Args {
    std::string mode;           // "producer", "consumer", "both"
    int  producers = 2;
    int  consumers = 2;

    static Args parse(int argc, char* argv[]) {
        Args a;
        if (argc < 2) {
            print_usage(argv[0]);
            exit(1);
        }
        a.mode = argv[1];

        // 解析 --key=value 参数
        for (int i = 2; i < argc; i++) {
            std::string arg = argv[i];
            if (arg.rfind("--", 0) != 0) continue;

            auto eq = arg.find('=');
            if (eq == std::string::npos) continue;

            std::string key = arg.substr(2, eq - 2);
            std::string val = arg.substr(eq + 1);

            if (key == "threads") {
                a.producers = a.consumers = std::stoi(val);
            } else if (key == "producers") {
                a.producers = std::stoi(val);
            } else if (key == "consumers") {
                a.consumers = std::stoi(val);
            }
        }

        // 校验 mode
        if (a.mode != "producer" && a.mode != "consumer" && a.mode != "both") {
            std::cerr << "未知模式: " << a.mode << "\n";
            print_usage(argv[0]);
            exit(1);
        }

        return a;
    }

    static void print_usage(const char* prog) {
        std::cerr << "用法:\n"
                  << "  多进程:\n"
                  << "    " << prog << " producer --threads=N    # N 个生产者线程\n"
                  << "    " << prog << " consumer --threads=N    # N 个消费者线程\n"
                  << "  单进程:\n"
                  << "    " << prog << " both --producers=N --consumers=M\n"
                  << "\n"
                  << "  --threads=N  producer/consumer 模式的线程数 (默认 2)\n"
                  << "  --producers=N  both 模式的生产者数 (默认 2)\n"
                  << "  --consumers=M  both 模式的消费者数 (默认 2)\n";
    }
};

// ============================================================
// 统计
// ============================================================
struct ThreadStats {
    std::atomic<uint64_t> produced{0};    // 成功入队数
    std::atomic<uint64_t> dropped{0};     // 队列满丢弃数
    std::atomic<uint64_t> consumed{0};    // 成功出队数
    std::atomic<uint64_t> empty_poll{0};  // 队列空轮询次数

    // 线程安全合并 (原子快照, 近似值)
    void merge(const ThreadStats& other) {
        produced   += other.produced.load(std::memory_order_relaxed);
        dropped    += other.dropped.load(std::memory_order_relaxed);
        consumed   += other.consumed.load(std::memory_order_relaxed);
        empty_poll += other.empty_poll.load(std::memory_order_relaxed);
    }
};

// ============================================================
// Producer 线程
// ============================================================
static void producer_thread(ShmLockFreeQueue<WorkItem, QUEUE_CAPACITY>* queue,
                            int tid, ThreadStats* stats)
{
    uint64_t seq = 0;

    while (running) {
        WorkItem item;
        item.sequence    = ++seq;
        item.producer_id = tid;

        using namespace std::chrono;
        item.timestamp_us = duration_cast<microseconds>(
            system_clock::now().time_since_epoch()).count();

        snprintf(item.payload, sizeof(item.payload),
                 "P%d-S%lu", tid, seq);

        if (queue->enqueue(item) == 0) {
            stats->produced.fetch_add(1, std::memory_order_relaxed);
        } else {
            stats->dropped.fetch_add(1, std::memory_order_relaxed);
            // 队列满, 短暂休眠后重试
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }

        // 限速: ~10K items/s per thread (可调节压力)
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
}

// ============================================================
// Consumer 线程
// ============================================================
static void consumer_thread(ShmLockFreeQueue<WorkItem, QUEUE_CAPACITY>* queue,
                            int tid, ThreadStats* stats)
{
    WorkItem item;

    while (running) {
        if (queue->dequeue(&item) == 0) {
            stats->consumed.fetch_add(1, std::memory_order_relaxed);

            // 模拟处理耗时 (让队列有机会积累)
            std::this_thread::sleep_for(std::chrono::microseconds(80));

        } else {
            stats->empty_poll.fetch_add(1, std::memory_order_relaxed);
            // 队列空, 等待数据
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    }
}

// ============================================================
// Producer 模式 — N 个生产者线程 (写入共享内存队列)
// ============================================================
static int run_producer(const Args& args) {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    ShmLockFreeQueue<WorkItem, QUEUE_CAPACITY> queue(SHM_NAME, Create);

    std::cout << "[PRODUCER] 无锁 MPMC 队列已创建: " << SHM_NAME << "\n"
              << "[PRODUCER] 容量: " << QUEUE_CAPACITY << " 条  |  每条 "
              << sizeof(WorkItem) << " bytes\n"
              << "[PRODUCER] 总大小: "
              << lfq_total_size(QUEUE_CAPACITY, sizeof(WorkItem)) << " bytes\n"
              << "[PRODUCER] 启动 " << args.producers
              << " 个生产者线程 (Ctrl-C 退出)...\n"
              << std::endl;

    std::vector<std::thread> threads;
    std::vector<ThreadStats> stats(args.producers);

    for (int i = 0; i < args.producers; i++) {
        threads.emplace_back(producer_thread, &queue, i, &stats[i]);
    }

    // 每秒打印统计
    while (running) {
        auto start = std::chrono::steady_clock::now();

        ThreadStats total;
        for (auto& s : stats) total.merge(s);

        // 清零每线程计数 (保留累计用另一个变量)
        std::cout << std::fixed << std::setprecision(0)
                  << "[PROD] total_enqueued=" << total.produced
                  << " | dropped=" << total.dropped
                  << " | depth=" << queue.count()
                  << "/" << QUEUE_CAPACITY
                  << std::endl;

        std::this_thread::sleep_until(start + std::chrono::seconds(1));
    }

    for (auto& t : threads) t.join();

    // 最终统计
    ThreadStats final_total;
    for (auto& s : stats) final_total.merge(s);
    std::cout << "\n[PRODUCER] 停止."
              << " 入队=" << final_total.produced
              << ", 丢弃=" << final_total.dropped
              << std::endl;
    return 0;
}

// ============================================================
// Consumer 模式 — M 个消费者线程 (从共享内存队列读取)
// ============================================================
static int run_consumer(const Args& args) {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    ShmLockFreeQueue<WorkItem, QUEUE_CAPACITY> queue(SHM_NAME, Open);

    std::cout << "[CONSUMER] 已映射无锁 MPMC 队列: " << SHM_NAME << "\n"
              << "[CONSUMER] 启动 " << args.consumers
              << " 个消费者线程 (Ctrl-C 退出)...\n"
              << std::endl;

    std::vector<std::thread> threads;
    std::vector<ThreadStats> stats(args.consumers);

    for (int i = 0; i < args.consumers; i++) {
        threads.emplace_back(consumer_thread, &queue, i, &stats[i]);
    }

    // 每秒打印统计
    while (running) {
        auto start = std::chrono::steady_clock::now();

        ThreadStats total;
        for (auto& s : stats) total.merge(s);

        std::cout << std::fixed << std::setprecision(0)
                  << "[CONS] total_dequeued=" << total.consumed
                  << " | empty_polls=" << total.empty_poll
                  << " | depth=" << queue.count()
                  << "/" << QUEUE_CAPACITY
                  << std::endl;

        std::this_thread::sleep_until(start + std::chrono::seconds(1));
    }

    for (auto& t : threads) t.join();

    ThreadStats final_total;
    for (auto& s : stats) final_total.merge(s);
    std::cout << "\n[CONSUMER] 停止."
              << " 出队=" << final_total.consumed
              << ", 空轮询=" << final_total.empty_poll
              << std::endl;
    return 0;
}

// ============================================================
// Both 模式 — 单进程内 N 生产者 + M 消费者
// ============================================================
static int run_both(const Args& args) {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    ShmLockFreeQueue<WorkItem, QUEUE_CAPACITY> queue(SHM_NAME, Create);

    std::cout << "[BOTH] 无锁 MPMC 队列: " << SHM_NAME << "\n"
              << "[BOTH] 容量: " << QUEUE_CAPACITY
              << "  |  " << args.producers << " producers + "
              << args.consumers << " consumers\n"
              << "[BOTH] Ctrl-C 退出...\n"
              << std::endl;

    std::vector<std::thread> p_threads;
    std::vector<ThreadStats> p_stats(args.producers);
    std::vector<std::thread> c_threads;
    std::vector<ThreadStats> c_stats(args.consumers);

    // 先启动消费者, 再启动生产者
    for (int i = 0; i < args.consumers; i++) {
        c_threads.emplace_back(consumer_thread, &queue, i, &c_stats[i]);
    }
    for (int i = 0; i < args.producers; i++) {
        p_threads.emplace_back(producer_thread, &queue, i, &p_stats[i]);
    }

    // 每秒打印统计
    while (running) {
        auto start = std::chrono::steady_clock::now();

        ThreadStats pt, ct;
        for (auto& s : p_stats) pt.merge(s);
        for (auto& s : c_stats) ct.merge(s);

        // 计算速率
        static uint64_t prev_prod = 0, prev_cons = 0;
        uint64_t prod_rate = pt.produced - prev_prod;
        uint64_t cons_rate = ct.consumed - prev_cons;
        prev_prod = pt.produced;
        prev_cons = ct.consumed;

        std::cout << std::fixed << std::setprecision(0)
                  << "[BOTH] produce=" << pt.produced
                  << " (+" << prod_rate << "/s)"
                  << " | consume=" << ct.consumed
                  << " (+" << cons_rate << "/s)"
                  << " | drop=" << pt.dropped
                  << " | depth=" << queue.count()
                  << "/" << QUEUE_CAPACITY
                  << std::endl;

        std::this_thread::sleep_until(start + std::chrono::seconds(1));
    }

    for (auto& t : p_threads) t.join();
    for (auto& t : c_threads) t.join();

    // 最终统计
    ThreadStats pt, ct;
    for (auto& s : p_stats) pt.merge(s);
    for (auto& s : c_stats) ct.merge(s);

    std::cout << "\n[BOTH] 停止.\n"
              << "  生产者: enqueued=" << pt.produced
              << ", dropped=" << pt.dropped << "\n"
              << "  消费者: dequeued=" << ct.consumed
              << ", empty_polls=" << ct.empty_poll << "\n"
              << "  队列残留: " << queue.count() << " 条\n";
    return 0;
}

// ============================================================
// Main
// ============================================================
int main(int argc, char* argv[]) {
    Args args = Args::parse(argc, argv);

    try {
        if (args.mode == "producer")
            return run_producer(args);
        else if (args.mode == "consumer")
            return run_consumer(args);
        else
            return run_both(args);
    } catch (const shm::Error& e) {
        std::cerr << "[FATAL] " << e.what() << std::endl;

        // 给出友好提示: 如果打开失败, 可能是还没创建
        if (args.mode == "consumer" &&
            std::string(e.what()).find("shm_open(OPEN)") != std::string::npos) {
            std::cerr << "[HINT] 请先启动 producer, 再启动 consumer.\n";
        }
        return 1;
    }
}
