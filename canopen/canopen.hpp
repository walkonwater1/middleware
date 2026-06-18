#pragma once
/*
 * canopen.hpp — CANopen 核心数据结构和定义
 *
 * 参考: CiA 301 v4.2.0
 *
 * 本文件定义了 CANopen 从站的核心组件：
 *   - 对象字典 (Object Dictionary) 结构
 *   - PDO 映射 / 通信参数
 *   - NMT 状态机
 *   - SDO 协议
 *   - COB-ID 计算宏
 *   - 错误/心跳码
 */

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace canopen {

// ==========================================================================
// 基本类型 (CiA 301 定义)
// ==========================================================================
using U8  = uint8_t;
using U16 = uint16_t;
using U32 = uint32_t;
using I8  = int8_t;
using I16 = int16_t;
using I32 = int32_t;

// ==========================================================================
// CAN-ID / COB-ID 构造宏
// ==========================================================================
constexpr U16 COB_NMT        = 0x000;          // 网络管理
constexpr U16 COB_SYNC       = 0x080;          // 同步
constexpr U16 COB_EMCY_BASE  = 0x080;          // 紧急报文基址
constexpr U16 COB_TIME       = 0x100;          // 时间戳
constexpr U16 COB_TPDO1_BASE = 0x180;          // 发送 PDO1
constexpr U16 COB_RPDO1_BASE = 0x200;          // 接收 PDO1
constexpr U16 COB_TPDO2_BASE = 0x280;          // 发送 PDO2
constexpr U16 COB_RPDO2_BASE = 0x300;          // 接收 PDO2
constexpr U16 COB_TSDO_BASE  = 0x580;          // SDO 应答
constexpr U16 COB_RSDO_BASE  = 0x600;          // SDO 请求
constexpr U16 COB_HEARTBEAT  = 0x700;          // 心跳基址

#define COB_TPDO1(id) (COB_TPDO1_BASE + (id))
#define COB_RPDO1(id) (COB_RPDO1_BASE + (id))
#define COB_TPDO2(id) (COB_TPDO2_BASE + (id))
#define COB_RPDO2(id) (COB_RPDO2_BASE + (id))
#define COB_TSDO(id)  (COB_TSDO_BASE  + (id))
#define COB_RSDO(id)  (COB_RSDO_BASE  + (id))
#define COB_EMCY(id)  (COB_EMCY_BASE  + (id))
#define COB_HB(id)    (COB_HEARTBEAT  + (id))

// ==========================================================================
// NMT 命令字
// ==========================================================================
enum class NmtCommand : U8 {
    StartRemoteNode  = 0x01,
    StopRemoteNode   = 0x02,
    EnterPreOp       = 0x80,
    ResetNode        = 0x81,
    ResetComm        = 0x82,
};

// NMT 状态 (CiA 301 §7.3.2)
enum class NmtState : U8 {
    BootUp       = 0x00,
    Stopped      = 0x04,
    Operational  = 0x05,
    PreOperational = 0x7F,
};

const char* nmt_state_str(NmtState s);

// ==========================================================================
// SDO 命令说明符 (CS)
// ==========================================================================
enum class SdoCCS : U8 {
    DownloadSegment   = 0x00,
    InitiateDownload  = 0x20,   // 客户端发起写请求
    InitiateUpload    = 0x40,   // 客户端发起读请求
    DownloadSegmentReq = 0x60,
    Abort             = 0x80,
};

// SDO 异常码 (部分)
enum class SdoAbortCode : U32 {
    ToggleBit        = 0x05030000,
    Timeout          = 0x05040000,
    Unsupported      = 0x06010000,
    AddressNotFound  = 0x06020000,
    ReadOnly         = 0x06010002,
    WriteOnly        = 0x06010001,
    OutOfRange       = 0x06090030,
    OutOfMemory      = 0x05040005,
};

// ==========================================================================
// 对象字典条目 (CiA 301 §7.4)
// ==========================================================================
enum class OdAccessType : U8 {
    RO = 0,    // 只读
    WO = 1,    // 只写
    RW = 2,    // 读写
    CONST = 3, // 常量
};

// 存储多种类型值的 variant
struct OdValue {
    enum class Type { U8, U16, U32, I8, I16, I32, STRING, NONE } type;
    union {
        U8   u8;
        U16  u16;
        U32  u32;
        I8   i8;
        I16  i16;
        I32  i32;
    };
    std::string str;

    OdValue() : type(Type::NONE), u32(0) {}
    explicit OdValue(U8   v) : type(Type::U8),  u8(v)  {}
    explicit OdValue(U16  v) : type(Type::U16), u16(v) {}
    explicit OdValue(U32  v) : type(Type::U32), u32(v) {}
    explicit OdValue(I8   v) : type(Type::I8),  i8(v)  {}
    explicit OdValue(I16  v) : type(Type::I16), i16(v) {}
    explicit OdValue(I32  v) : type(Type::I32), i32(v) {}
    explicit OdValue(const std::string& s) : type(Type::STRING), u32(0), str(s) {}

    // 获取字节数
    size_t byte_size() const;
    // 序列化到 buffer
    void serialize(U8* buf) const;
};

struct OdEntry {
    U16          index;         // 索引 (0x1000~0x9FFF)
    U8           sub_index;     // 子索引 (0x00~0xFF)
    std::string  name;          // 条目名称 (调试用)
    OdAccessType access;
    OdValue      value;
    OdValue      min_value;     // 范围校验
    OdValue      max_value;
};

// ==========================================================================
// PDO 映射条目
// ==========================================================================
struct PdoMappingEntry {
    U16 index;
    U8  sub_index;
    U8  bit_length;  // 映射位的长度 (8/16/32)
};

// PDO 通信参数 (CiA 301 §8.3)
struct PdoCommParam {
    U16 cob_id;        // COB-ID (bit31 表示禁用)
    U8  trans_type;    // 传输类型: 0=同步, 254=异步, 255=事件驱动
    U16 inhibit_time;  // 抑制时间 (100μs 单位)
    U16 event_timer;   // 事件定时器 (ms)
};

// PDO 描述符
struct PdoDescriptor {
    PdoCommParam            comm;
    std::vector<PdoMappingEntry> mapping;  // 最多 8 字节
    bool                    enabled;
    PdoDescriptor() : enabled(true) {
        comm.trans_type = 255;
        comm.inhibit_time = 0;
        comm.event_timer = 0;
    }
};

// ==========================================================================
// 从站对象字典 (OD) — 核心数据结构
// ==========================================================================
class ObjectDictionary {
public:
    ObjectDictionary(U8 node_id);

    // 添加/获取条目
    void add_entry(const OdEntry& entry);
    OdEntry* get_entry(U16 index, U8 sub_index = 0);
    const OdEntry* get_entry(U16 index, U8 sub_index = 0) const;

    // SDO 协议栈读写
    bool sdo_read(U16 index, U8 sub_index, U8* data, size_t* len);
    bool sdo_write(U16 index, U8 sub_index, const U8* data, size_t len, U32* abort_code);

    // PDO 打包 — 根据映射表构造 CAN 帧数据
    size_t pdo_pack(const PdoDescriptor& pdo, U8* buf, size_t buf_size) const;

    // PDO 解包 — 根据映射表从 CAN 帧解析数据
    bool pdo_unpack(const PdoDescriptor& pdo, const U8* data, size_t len);

    // 获取设备信息
    U16 vendor_id() const;
    U32 product_code() const;
    U8  node_id() const { return node_id_; }

    // 心跳生产时间 (ms)
    U16 heartbeat_producer_time() const;

    void dump() const;  // 调试打印

private:
    U8  node_id_;
    std::map<U32, OdEntry> entries_;  // key = (index << 8) | sub_index

    static U32 make_key(U16 index, U8 sub) {
        return (static_cast<U32>(index) << 8) | sub;
    }

    bool validate_range(U16 index, U8 sub, const OdValue& val, U32* abort_code) const;
};

// ==========================================================================
// NMT 状态机
// ==========================================================================
class NmtStateMachine {
public:
    NmtStateMachine(U8 node_id);

    // 状态转换
    void boot_up();
    bool apply_command(NmtCommand cmd);  // 返回是否转换成功

    NmtState state() const { return state_; }
    U8       node_id() const { return node_id_; }

    // 回调 — 状态变化时触发
    using StateChangeHandler = std::function<void(NmtState old_s, NmtState new_s)>;
    void on_state_change(StateChangeHandler h) { on_change_ = std::move(h); }

private:
    U8      node_id_;
    NmtState state_;
    StateChangeHandler on_change_;
};

// ==========================================================================
// EMCY (紧急报文) 格式
// ==========================================================================
struct EmcyMessage {
    U16 error_code;     // CiA 301 标准错误码
    U8  error_register; // 1001h 错误寄存器内容
    U8  mf_specific[5]; // 厂商自定义
};

// CiA 301 标准 EMCY 错误码 (部分)
namespace EmcyCode {
    constexpr U16 ERROR_RESET        = 0x0000;
    constexpr U16 GENERIC_ERROR      = 0x1000;
    constexpr U16 CURRENT            = 0x2000;
    constexpr U16 VOLTAGE            = 0x3000;
    constexpr U16 TEMPERATURE        = 0x4000;
    constexpr U16 DEVICE_HARDWARE    = 0x5000;
    constexpr U16 DEVICE_SOFTWARE    = 0x6000;
    constexpr U16 COMMUNICATION      = 0x8100;
    constexpr U16 PROTOCOL_ERROR     = 0x8200;
    constexpr U16 EXTERNAL_ERROR     = 0x9000;
}

// ==========================================================================
// Heartbeat 消息
// ==========================================================================
struct HeartbeatMessage {
    NmtState state;
};

// ==========================================================================
// CAN 帧封装
// ==========================================================================
struct CanFrame {
    U16 cob_id;
    U8  dlc;             // 数据长度 (0~8)
    U8  data[8];

    CanFrame() : cob_id(0), dlc(0) { std::fill(data, data + 8, 0); }
    CanFrame(U16 id, const U8* d, U8 len) : cob_id(id), dlc(len) {
        dlc = (dlc > 8) ? 8 : dlc;
        std::copy(d, d + dlc, data);
        std::fill(data + dlc, data + 8, 0);
    }

    static CanFrame nmt(NmtCommand cmd, U8 node_id);
    static CanFrame heartbeat(U8 node_id, NmtState s);
    static CanFrame emcy(U8 node_id, const EmcyMessage& em);
    static CanFrame tpdo1(U8 node_id, const U8* d, U8 len);
    static CanFrame tpdo2(U8 node_id, const U8* d, U8 len);
};

// ==========================================================================
// 虚拟 CAN 总线 — 用于无硬件的仿真
// ==========================================================================
class VirtualCanBus {
public:
    using FrameHandler = std::function<void(const CanFrame&)>;

    // 注册特定 COB-ID 的处理函数
    void subscribe(U16 cob_id, FrameHandler handler);
    void unsubscribe(U16 cob_id);

    // 发送帧（广播给所有匹配的订阅者）
    void send(const CanFrame& frame);

private:
    std::multimap<U16, FrameHandler> handlers_;
};

} // namespace canopen
