/**
 * can_database.cpp — CAN 信号数据库实现
 */

#include "mqtt_can/can_database.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace mqtt_can {

// ============================================================
// 位操作
// ============================================================

uint64_t extract_bits(const std::array<uint8_t, 8>& data,
                      uint8_t start_bit, uint8_t length, ByteOrder order)
{
    uint64_t result = 0;
    if (order == ByteOrder::Intel) {
        for (uint8_t i = 0; i < length; i++) {
            uint8_t bit_pos  = start_bit + i;
            uint8_t byte_idx = bit_pos / 8;
            uint8_t bit_idx  = bit_pos % 8;
            if (byte_idx < 8 && (data[byte_idx] & (1u << bit_idx)))
                result |= (1ULL << i);
        }
    } else {
        for (uint8_t i = 0; i < length; i++) {
            int bit_pos = static_cast<int>(start_bit) - static_cast<int>(i);
            if (bit_pos < 0) break;
            auto byte_idx = static_cast<uint8_t>(bit_pos / 8);
            auto bit_idx  = static_cast<uint8_t>(bit_pos % 8);
            if (byte_idx < 8 && (data[byte_idx] & (1u << bit_idx)))
                result |= (1ULL << i);
        }
    }
    return result;
}

void pack_bits(std::array<uint8_t, 8>& data,
               uint8_t start_bit, uint8_t length,
               uint64_t value, ByteOrder order)
{
    if (order == ByteOrder::Intel) {
        for (uint8_t i = 0; i < length; i++) {
            uint8_t bit_pos  = start_bit + i;
            uint8_t byte_idx = bit_pos / 8;
            uint8_t bit_idx  = bit_pos % 8;
            if (byte_idx < 8) {
                if (value & (1ULL << i))
                    data[byte_idx] |= (1u << bit_idx);
                else
                    data[byte_idx] &= ~(1u << bit_idx);
            }
        }
    } else {
        for (uint8_t i = 0; i < length; i++) {
            int bit_pos = static_cast<int>(start_bit) - static_cast<int>(i);
            if (bit_pos < 0) break;
            auto byte_idx = static_cast<uint8_t>(bit_pos / 8);
            auto bit_idx  = static_cast<uint8_t>(bit_pos % 8);
            if (byte_idx < 8) {
                if (value & (1ULL << i))
                    data[byte_idx] |= (1u << bit_idx);
                else
                    data[byte_idx] &= ~(1u << bit_idx);
            }
        }
    }
}

// ============================================================
// CanSignal
// ============================================================

float CanSignal::decode(uint64_t raw) const
{
    int64_t val = static_cast<int64_t>(raw);
    if (is_signed && (raw & (1ULL << (length - 1)))) {
        val = static_cast<int64_t>(raw) - (1LL << length);
    }
    return static_cast<float>(static_cast<double>(val) * scale + offset);
}

uint64_t CanSignal::encode(float physical) const
{
    auto raw = static_cast<int64_t>(
        std::round((static_cast<double>(physical) - offset) / scale));
    if (raw < 0) raw = 0;
    uint64_t max_raw = (1ULL << length) - 1;
    if (static_cast<uint64_t>(raw) > max_raw) raw = static_cast<int64_t>(max_raw);
    return static_cast<uint64_t>(raw);
}

// ============================================================
// CanMessage
// ============================================================

std::vector<std::pair<std::string, float>>
CanMessage::parse(const std::array<uint8_t, 8>& data) const
{
    std::vector<std::pair<std::string, float>> result;
    result.reserve(signals.size());
    for (const auto& sig : signals) {
        uint64_t raw = extract_bits(data, sig.start_bit, sig.length, sig.byte_order);
        result.emplace_back(sig.name, sig.decode(raw));
    }
    return result;
}

std::array<uint8_t, 8>
CanMessage::encode(const std::vector<std::pair<std::string, float>>& values) const
{
    std::array<uint8_t, 8> data{};
    for (const auto& [name, physical] : values) {
        for (const auto& sig : signals) {
            if (sig.name == name) {
                uint64_t raw = sig.encode(physical);
                pack_bits(data, sig.start_bit, sig.length, raw, sig.byte_order);
                break;
            }
        }
    }
    return data;
}

// ============================================================
// CanDatabase
// ============================================================

CanDatabase& CanDatabase::instance()
{
    static CanDatabase db;
    return db;
}

void CanDatabase::register_message(CanMessage msg)
{
    messages_.push_back(std::move(msg));
}

const CanMessage* CanDatabase::find_message(uint32_t can_id) const
{
    for (const auto& msg : messages_) {
        if (msg.can_id == can_id) return &msg;
    }
    return nullptr;
}

const CanSignal* CanDatabase::find_signal(const std::string& name) const
{
    for (const auto& msg : messages_) {
        for (const auto& sig : msg.signals) {
            if (sig.name == name) return &sig;
        }
    }
    return nullptr;
}

const CanMessage* CanDatabase::find_message_by_signal(const std::string& name) const
{
    for (const auto& msg : messages_) {
        for (const auto& sig : msg.signals) {
            if (sig.name == name) return &msg;
        }
    }
    return nullptr;
}

void CanDatabase::print() const
{
    printf("==============================================================================\n");
    printf("  CAN 信号数据库 (DBC-like)\n");
    printf("==============================================================================\n");
    for (const auto& msg : messages_) {
        printf("\n[0x%03X] %-20s (周期=%ums, DLC=%u)\n",
               msg.can_id, msg.name.c_str(), msg.period_ms, msg.dlc);
        printf("  %-22s %6s %4s %8s %8s %8s\n",
               "信号名", "起始位", "长度", "Scale", "Offset", "单位");
        printf("  %-22s %6s %4s %8s %8s %8s\n",
               "----------------------", "------", "----", "--------", "--------", "--------");
        for (const auto& sig : msg.signals) {
            printf("  %-22s %6u %4u %8.3f %8.1f %8s\n",
                   sig.name.c_str(), sig.start_bit, sig.length,
                   sig.scale, sig.offset, sig.unit.c_str());
        }
    }
    printf("\n");
}

void CanDatabase::init_default()
{
    messages_.clear();

    // 0x100: PowerTrain
    {
        CanMessage msg{0x100, "PowerTrain", 8, 10};
        msg.signals = {
            {"EngineSpeed",   0, 16, 0.25f, 0.0f,   0.0f, 16000.0f, "rpm",   ByteOrder::Intel, false},
            {"VehicleSpeed", 16, 16, 0.01f, 0.0f,   0.0f, 500.0f,   "km/h",  ByteOrder::Intel, false},
            {"BrakeSwitch",  32, 2,  1.0f,  0.0f,   0.0f, 3.0f,     "",      ByteOrder::Intel, false},
        };
        messages_.push_back(std::move(msg));
    }

    // 0x200: ThermalElectric
    {
        CanMessage msg{0x200, "ThermalElectric", 8, 100};
        msg.signals = {
            {"CoolantTemp",    0, 8,  1.0f,  -40.0f, -40.0f, 215.0f, "°C", ByteOrder::Intel, false},
            {"BatteryVoltage", 8, 16, 0.1f,   0.0f,   0.0f,  65.0f,  "V",  ByteOrder::Intel, false},
            {"FuelLevel",     24, 8,  0.4f,   0.0f,   0.0f, 100.0f,  "%",  ByteOrder::Intel, false},
        };
        messages_.push_back(std::move(msg));
    }

    // 0x300: DriverControl
    {
        CanMessage msg{0x300, "DriverControl", 8, 20};
        msg.signals = {
            {"ThrottlePosition", 0, 8, 0.39f, 0.0f, 0.0f, 100.0f, "%", ByteOrder::Intel, false},
            {"GearPosition",     8, 4, 1.0f,  0.0f, 0.0f, 8.0f,    "",  ByteOrder::Intel, false},
        };
        messages_.push_back(std::move(msg));
    }

    // 0x400: Steering
    {
        CanMessage msg{0x400, "Steering", 8, 10};
        msg.signals = {
            {"SteeringAngle", 0,  16, 0.1f, -780.0f, -780.0f, 780.0f,  "deg",   ByteOrder::Intel, false},
            {"SteeringRate", 16, 16,  1.0f,    0.0f,    0.0f, 1016.0f, "deg/s", ByteOrder::Intel, false},
        };
        messages_.push_back(std::move(msg));
    }

    // 0x500: Instrument
    {
        CanMessage msg{0x500, "Instrument", 8, 100};
        msg.signals = {
            {"Odometer", 0, 32, 0.1f, 0.0f, 0.0f, 1000000.0f, "km", ByteOrder::Intel, false},
        };
        messages_.push_back(std::move(msg));
    }
}

} // namespace mqtt_can
