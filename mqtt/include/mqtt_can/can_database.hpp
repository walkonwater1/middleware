/**
 * mqtt_can/can_database.hpp — CAN 信号数据库 (DBC-like)
 *
 * 模拟真实车载 DBC 文件中的信号定义。
 * 每条信号描述在 CAN 帧中的位布局、解析公式和物理含义。
 *
 * 物理值 = raw × scale + offset
 *
 * 定义了 5 个 CAN 帧 (0x100 ~ 0x500)，共 12 个物理信号。
 */

#ifndef MQTT_CAN_DATABASE_HPP
#define MQTT_CAN_DATABASE_HPP

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <array>

namespace mqtt_can {

// ============================================================
// 枚举
// ============================================================

/** 字节序 */
enum class ByteOrder : uint8_t {
    Intel    = 0,  // 小端 (主流车厂使用)
    Motorola = 1,  // 大端
};

// ============================================================
// 信号定义
// ============================================================

/** 单条 CAN 信号定义 */
struct CanSignal {
    std::string name;           // 信号名称
    uint8_t     start_bit = 0;  // 起始位
    uint8_t     length    = 0;  // 位长度
    float       scale     = 1.0f;
    float       offset    = 0.0f;
    float       min_value = 0.0f;
    float       max_value = 0.0f;
    std::string unit;           // 物理单位
    ByteOrder   byte_order = ByteOrder::Intel;
    bool        is_signed  = false;

    /** 解码: 原始值 → 物理值 */
    float decode(uint64_t raw) const;

    /** 编码: 物理值 → 原始值 */
    uint64_t encode(float physical) const;
};

/** CAN 报文定义 */
struct CanMessage {
    uint32_t              can_id     = 0;
    std::string           name;
    uint8_t               dlc        = 8;
    uint32_t              period_ms  = 100;
    std::vector<CanSignal> signals;

    /** 从原始 CAN 数据解析所有信号，返回 {信号名: 物理值} */
    std::vector<std::pair<std::string, float>> parse(const std::array<uint8_t, 8>& data) const;

    /** 将信号物理值编码为 8 字节 CAN 数据 */
    std::array<uint8_t, 8> encode(const std::vector<std::pair<std::string, float>>& values) const;
};

// ============================================================
// 位操作工具
// ============================================================

/**
 * 从 CAN 数据中按位提取信号原始值。
 */
uint64_t extract_bits(const std::array<uint8_t, 8>& data,
                      uint8_t start_bit, uint8_t length, ByteOrder order);

/**
 * 将整数值按位写入 CAN 数据 (extract_bits 的逆操作)。
 */
void pack_bits(std::array<uint8_t, 8>& data,
               uint8_t start_bit, uint8_t length,
               uint64_t value, ByteOrder order);

// ============================================================
// 信号数据库
// ============================================================

class CanDatabase {
public:
    static CanDatabase& instance();

    /** 注册一条 CAN 报文 */
    void register_message(CanMessage msg);

    /** 根据 CAN ID 查找报文定义 */
    const CanMessage* find_message(uint32_t can_id) const;

    /** 根据信号名查找 */
    const CanSignal* find_signal(const std::string& name) const;
    const CanMessage* find_message_by_signal(const std::string& name) const;

    /** 所有报文 */
    const std::vector<CanMessage>& all_messages() const { return messages_; }

    /** 打印数据库摘要 */
    void print() const;

    /** 初始化默认车辆信号数据库 (5帧12信号) */
    void init_default();

private:
    CanDatabase() = default;
    std::vector<CanMessage> messages_;
};

} // namespace mqtt_can

#endif // MQTT_CAN_DATABASE_HPP
