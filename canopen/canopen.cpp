/*
 * canopen.cpp — CANopen 核心实现
 */
#include "canopen.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace canopen {

// ==========================================================================
// 工具函数
// ==========================================================================
const char* nmt_state_str(NmtState s) {
    switch (s) {
        case NmtState::BootUp:         return "BOOTUP";
        case NmtState::Stopped:        return "STOPPED";
        case NmtState::Operational:    return "OPERATIONAL";
        case NmtState::PreOperational: return "PRE-OP";
        default:                       return "UNKNOWN";
    }
}

// ==========================================================================
// OdValue
// ==========================================================================
size_t OdValue::byte_size() const {
    switch (type) {
        case Type::U8:  case Type::I8:  return 1;
        case Type::U16: case Type::I16: return 2;
        case Type::U32: case Type::I32: return 4;
        default: return 0;
    }
}

void OdValue::serialize(U8* buf) const {
    switch (type) {
        case Type::U8:  buf[0] = u8; break;
        case Type::I8:  buf[0] = static_cast<U8>(i8); break;
        case Type::U16: memcpy(buf, &u16, 2); break;
        case Type::I16: memcpy(buf, &i16, 2); break;
        case Type::U32: memcpy(buf, &u32, 4); break;
        case Type::I32: memcpy(buf, &i32, 4); break;
        default: break;
    }
}

// ==========================================================================
// CanFrame
// ==========================================================================
CanFrame CanFrame::nmt(NmtCommand cmd, U8 node_id) {
    U8 data[2] = { static_cast<U8>(cmd), node_id };
    return CanFrame(COB_NMT, data, 2);
}

CanFrame CanFrame::heartbeat(U8 node_id, NmtState s) {
    U8 d = static_cast<U8>(s);
    return CanFrame(COB_HB(node_id), &d, 1);
}

CanFrame CanFrame::emcy(U8 node_id, const EmcyMessage& em) {
    U8 buf[8] = {};
    buf[0] = static_cast<U8>(em.error_code & 0xFF);
    buf[1] = static_cast<U8>((em.error_code >> 8) & 0xFF);
    buf[2] = em.error_register;
    memcpy(buf + 3, em.mf_specific, 5);
    return CanFrame(COB_EMCY(node_id), buf, 8);
}

CanFrame CanFrame::tpdo1(U8 node_id, const U8* d, U8 len) {
    return CanFrame(COB_TPDO1(node_id), d, len);
}

CanFrame CanFrame::tpdo2(U8 node_id, const U8* d, U8 len) {
    return CanFrame(COB_TPDO2(node_id), d, len);
}

// ==========================================================================
// ObjectDictionary
// ==========================================================================
ObjectDictionary::ObjectDictionary(U8 node_id) : node_id_(node_id) {
    // ---- 通信参数 (0x1000~0x1FFF) ----
    // 1000h: 设备类型 (CiA 402 伺服: 0x00020192)
    add_entry({0x1000, 0, "Device Type", OdAccessType::RO,
               OdValue(U32(0x00020192))});

    // 1001h: 错误寄存器
    add_entry({0x1001, 0, "Error Register", OdAccessType::RO,
               OdValue(U8(0))});

    // 1017h: 心跳生产时间 (ms)
    add_entry({0x1017, 0, "Producer Heartbeat Time", OdAccessType::RW,
               OdValue(U16(100)), OdValue(U16(0)), OdValue(U16(65535))});

    // 1018h: 设备标识
    add_entry({0x1018, 1, "Vendor ID",       OdAccessType::RO, OdValue(U32(0x0000ABCD))});
    add_entry({0x1018, 2, "Product Code",    OdAccessType::RO, OdValue(U32(0x00000001))});
    add_entry({0x1018, 3, "Revision Number", OdAccessType::RO, OdValue(U32(0x00010000))});
    add_entry({0x1018, 4, "Serial Number",   OdAccessType::RO, OdValue(U32(0x00000001))});

    // ---- PDO 通信/映射参数 (0x1400~0x1BFF) ----
    // RPDO1
    add_entry({0x1400, 1, "RPDO1 COB-ID",    OdAccessType::RW, OdValue(U32(COB_RPDO1(node_id)))});
    add_entry({0x1400, 2, "RPDO1 Trans Type", OdAccessType::RW, OdValue(U8(255))});
    add_entry({0x1600, 0, "RPDO1 mapping count", OdAccessType::RW, OdValue(U8(0))});
    add_entry({0x1600, 1, "RPDO1 map[1]", OdAccessType::RW, OdValue(U32(0))});
    add_entry({0x1600, 2, "RPDO1 map[2]", OdAccessType::RW, OdValue(U32(0))});

    // TPDO1
    add_entry({0x1800, 1, "TPDO1 COB-ID",     OdAccessType::RW, OdValue(U32(COB_TPDO1(node_id)))});
    add_entry({0x1800, 2, "TPDO1 Trans Type",  OdAccessType::RW, OdValue(U8(255))});
    add_entry({0x1800, 5, "TPDO1 Event Timer", OdAccessType::RW, OdValue(U16(0))});
    add_entry({0x1A00, 0, "TPDO1 mapping count", OdAccessType::RW, OdValue(U8(2))});
    add_entry({0x1A00, 1, "TPDO1 map[1]", OdAccessType::RW, OdValue(U32(0x60640020))}); // pos
    add_entry({0x1A00, 2, "TPDO1 map[2]", OdAccessType::RW, OdValue(U32(0x606C0020))}); // vel

    // TPDO2
    add_entry({0x1801, 1, "TPDO2 COB-ID",     OdAccessType::RW, OdValue(U32(COB_TPDO2(node_id)))});
    add_entry({0x1801, 2, "TPDO2 Trans Type",  OdAccessType::RW, OdValue(U8(254))});
    add_entry({0x1801, 5, "TPDO2 Event Timer", OdAccessType::RW, OdValue(U16(0))});
    add_entry({0x1A01, 0, "TPDO2 mapping count", OdAccessType::RW, OdValue(U8(2))});
    add_entry({0x1A01, 1, "TPDO2 map[1]", OdAccessType::RW, OdValue(U32(0x60770010))}); // torque
    add_entry({0x1A01, 2, "TPDO2 map[2]", OdAccessType::RW, OdValue(U32(0x60780010))}); // current

    // ---- CiA 402 驱动器参数 (0x6000~0x6FFF) ----
    // 控制字
    add_entry({0x6040, 0, "Controlword",  OdAccessType::RW, OdValue(U16(0))});
    // 状态字
    add_entry({0x6041, 0, "Statusword",   OdAccessType::RO, OdValue(U16(0x0250))});
    // 实际位置
    add_entry({0x6064, 0, "Actual Position", OdAccessType::RO, OdValue(I32(0))});
    // 实际速度
    add_entry({0x606C, 0, "Actual Velocity", OdAccessType::RO, OdValue(I32(0))});
    // 目标位置
    add_entry({0x607A, 0, "Target Position", OdAccessType::RW, OdValue(I32(0))});
    // 实际转矩
    add_entry({0x6077, 0, "Actual Torque",   OdAccessType::RO, OdValue(I16(0))});
    // 实际电流
    add_entry({0x6078, 0, "Actual Current",  OdAccessType::RO, OdValue(I16(0))});
}

void ObjectDictionary::add_entry(const OdEntry& entry) {
    entries_[make_key(entry.index, entry.sub_index)] = entry;
}

OdEntry* ObjectDictionary::get_entry(U16 index, U8 sub_index) {
    auto it = entries_.find(make_key(index, sub_index));
    return (it != entries_.end()) ? &it->second : nullptr;
}

const OdEntry* ObjectDictionary::get_entry(U16 index, U8 sub_index) const {
    auto it = entries_.find(make_key(index, sub_index));
    return (it != entries_.end()) ? &it->second : nullptr;
}

bool ObjectDictionary::sdo_read(U16 index, U8 sub_index, U8* data, size_t* len) {
    auto* entry = get_entry(index, sub_index);
    if (!entry) return false;
    if (entry->access == OdAccessType::WO) return false;  // 不可读

    size_t sz = entry->value.byte_size();
    if (sz > *len) return false;

    entry->value.serialize(data);
    *len = sz;
    return true;
}

bool ObjectDictionary::sdo_write(U16 index, U8 sub_index, const U8* data,
                                  size_t len, U32* abort_code) {
    auto* entry = get_entry(index, sub_index);
    if (!entry) {
        *abort_code = static_cast<U32>(SdoAbortCode::AddressNotFound);
        return false;
    }
    if (entry->access == OdAccessType::RO) {
        *abort_code = static_cast<U32>(SdoAbortCode::ReadOnly);
        return false;
    }

    // 构造 OdValue
    OdValue new_val;
    switch (entry->value.type) {
        case OdValue::Type::U8:  if(len>=1) new_val = OdValue(data[0]); break;
        case OdValue::Type::U16: if(len>=2) { U16 v; memcpy(&v, data, 2); new_val = OdValue(v); } break;
        case OdValue::Type::U32: if(len>=4) { U32 v; memcpy(&v, data, 4); new_val = OdValue(v); } break;
        case OdValue::Type::I8:  if(len>=1) new_val = OdValue(static_cast<I8>(data[0])); break;
        case OdValue::Type::I16: if(len>=2) { I16 v; memcpy(&v, data, 2); new_val = OdValue(v); } break;
        case OdValue::Type::I32: if(len>=4) { I32 v; memcpy(&v, data, 4); new_val = OdValue(v); } break;
        default: { *abort_code = static_cast<U32>(SdoAbortCode::Unsupported); return false; }
    }

    // 范围校验
    if (!validate_range(index, sub_index, new_val, abort_code))
        return false;

    entry->value = new_val;
    return true;
}

bool ObjectDictionary::validate_range(U16 index, U8 sub, const OdValue& val,
                                       U32* abort_code) const {
    auto it = entries_.find(make_key(index, sub));
    if (it == entries_.end()) return true;

    const auto& entry = it->second;
    if (entry.min_value.type == OdValue::Type::NONE) return true;

    // 简化比较 (仅支持同类型)
    auto cmp = [](const OdValue& a, const OdValue& b) -> bool {
        if (a.type != b.type) return true; // 无法比较,放行
        switch (a.type) {
            case OdValue::Type::U8:  return a.u8  <= b.u8;
            case OdValue::Type::U16: return a.u16 <= b.u16;
            case OdValue::Type::U32: return a.u32 <= b.u32;
            case OdValue::Type::I8:  return a.i8  <= b.i8;
            case OdValue::Type::I16: return a.i16 <= b.i16;
            case OdValue::Type::I32: return a.i32 <= b.i32;
            default: return true;
        }
    };

    if (!cmp(val, entry.max_value) || !cmp(entry.min_value, val)) {
        *abort_code = static_cast<U32>(SdoAbortCode::OutOfRange);
        return false;
    }
    return true;
}

size_t ObjectDictionary::pdo_pack(const PdoDescriptor& pdo, U8* buf, size_t buf_size) const {
    size_t offset = 0;
    for (const auto& m : pdo.mapping) {
        if (m.bit_length == 0) continue;

        auto* entry = get_entry(m.index, m.sub_index);
        if (!entry) continue;

        size_t sz = entry->value.byte_size();
        if (offset + sz > buf_size) break;

        entry->value.serialize(buf + offset);
        offset += sz;
    }
    return offset;
}

bool ObjectDictionary::pdo_unpack(const PdoDescriptor& pdo, const U8* data, size_t len) {
    size_t offset = 0;
    for (const auto& m : pdo.mapping) {
        if (m.bit_length == 0) continue;

        size_t sz = m.bit_length / 8;
        if (offset + sz > len) return false;

        auto* entry = get_entry(m.index, m.sub_index);
        if (!entry) { offset += sz; continue; }
        if (entry->access == OdAccessType::RO) { offset += sz; continue; }

        switch (entry->value.type) {
            case OdValue::Type::U8:  if(sz>=1) entry->value = OdValue(data[offset]); break;
            case OdValue::Type::U16: if(sz>=2) { U16 v; memcpy(&v, data+offset, 2); entry->value = OdValue(v); } break;
            case OdValue::Type::U32: if(sz>=4) { U32 v; memcpy(&v, data+offset, 4); entry->value = OdValue(v); } break;
            case OdValue::Type::I8:  if(sz>=1) entry->value = OdValue(static_cast<I8>(data[offset])); break;
            case OdValue::Type::I16: if(sz>=2) { I16 v; memcpy(&v, data+offset, 2); entry->value = OdValue(v); } break;
            case OdValue::Type::I32: if(sz>=4) { I32 v; memcpy(&v, data+offset, 4); entry->value = OdValue(v); } break;
            default: break;
        }
        offset += sz;
    }
    return true;
}

U16 ObjectDictionary::vendor_id() const {
    auto* e = get_entry(0x1018, 1);
    return (e && e->value.type == OdValue::Type::U32) ?
           static_cast<U16>(e->value.u32 & 0xFFFF) : 0;
}

U32 ObjectDictionary::product_code() const {
    auto* e = get_entry(0x1018, 2);
    return (e && e->value.type == OdValue::Type::U32) ? e->value.u32 : 0;
}

U16 ObjectDictionary::heartbeat_producer_time() const {
    auto* e = get_entry(0x1017, 0);
    return (e && e->value.type == OdValue::Type::U16) ? e->value.u16 : 0;
}

void ObjectDictionary::dump() const {
    printf("======== Object Dictionary (node #%d) ========\n", node_id_);
    for (const auto& [key, entry] : entries_) {
        if (entry.sub_index > 0) continue; // 只打印主索引
        printf("  [%04X] %s\n", entry.index, entry.name.c_str());
    }
    printf("==============================================\n");
}

// ==========================================================================
// NmtStateMachine
// ==========================================================================
NmtStateMachine::NmtStateMachine(U8 node_id)
    : node_id_(node_id), state_(NmtState::BootUp) {}

void NmtStateMachine::boot_up() {
    auto old = state_;
    state_ = NmtState::PreOperational;
    printf("[NODE#%d] State: %s → %s (bootup)\n",
           node_id_, nmt_state_str(old), nmt_state_str(state_));
    if (on_change_) on_change_(old, state_);
}

bool NmtStateMachine::apply_command(NmtCommand cmd) {
    NmtState next;
    bool valid = true;

    switch (cmd) {
        case NmtCommand::StartRemoteNode:
            if (state_ == NmtState::Operational) return true; // 幂等
            next = (state_ == NmtState::PreOperational || state_ == NmtState::Stopped)
                   ? NmtState::Operational : state_;
            break;
        case NmtCommand::StopRemoteNode:
            next = (state_ == NmtState::Operational || state_ == NmtState::PreOperational)
                   ? NmtState::Stopped : state_;
            break;
        case NmtCommand::EnterPreOp:
            next = (state_ == NmtState::Operational || state_ == NmtState::Stopped)
                   ? NmtState::PreOperational : state_;
            break;
        case NmtCommand::ResetNode:
            next = NmtState::BootUp;
            break;
        case NmtCommand::ResetComm:
            next = NmtState::BootUp;
            break;
        default:
            valid = false;
            next = state_;
            break;
    }

    if (next != state_) {
        auto old = state_;
        state_ = next;
        printf("[NODE#%d] State: %s → %s (cmd=%02X)\n",
               node_id_, nmt_state_str(old), nmt_state_str(state_),
               static_cast<U8>(cmd));
        if (on_change_) on_change_(old, state_);
    }
    return valid;
}

// ==========================================================================
// VirtualCanBus
// ==========================================================================
void VirtualCanBus::subscribe(U16 cob_id, FrameHandler handler) {
    handlers_.emplace(cob_id, std::move(handler));
}

void VirtualCanBus::unsubscribe(U16 cob_id) {
    handlers_.erase(cob_id);
}

void VirtualCanBus::send(const CanFrame& frame) {
    auto range = handlers_.equal_range(frame.cob_id);
    for (auto it = range.first; it != range.second; ++it) {
        it->second(frame);
    }
}

} // namespace canopen
