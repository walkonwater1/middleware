/**
 * vehicle_someip.hpp — 车辆 SOME/IP 服务接口定义 (共享头文件)
 *
 * 统一定义 service/client 共用的:
 *   - Service ID / Instance ID / Method ID / Event ID / Field ID
 *   - 序列化/反序列化辅助函数 (含字节序转换)
 *   - 数据结构定义
 *
 * SOME/IP 三种通信模式:
 *   Method  : Request/Response (RPC 调用)
 *   Event   : 服务端主动推送 (订阅/通知)
 *   Field   : Get/Set/Notifier 组合 (状态属性)
 */

#ifndef VEHICLE_SOMEIP_HPP
#define VEHICLE_SOMEIP_HPP

#include <vsomeip/vsomeip.hpp>
#include <vector>
#include <cstring>
#include <cstdint>
#include <iostream>

namespace vehicle {

/* ============================================================
 * ID 常量 (与 .json 配置文件匹配)
 * ============================================================ */
constexpr vsomeip::service_id_t    SERVICE_ID          = 0x1234;
constexpr vsomeip::instance_id_t   INSTANCE_ID         = 0x5678;

/* 事件 */
constexpr vsomeip::event_id_t      EVENT_SPEED         = 0x8001;
constexpr vsomeip::event_id_t      EVENT_DOOR          = 0x8002;

/* 方法 */
constexpr vsomeip::method_id_t     METHOD_WINDOW       = 0x0001;

/* Field (空调温度) — Get/Set/Notifier 三位一体 */
constexpr vsomeip::method_id_t     METHOD_CLIMATE_GET  = 0x0002;  /* Getter */
constexpr vsomeip::method_id_t     METHOD_CLIMATE_SET  = 0x0003;  /* Setter */
constexpr vsomeip::event_id_t      EVENT_CLIMATE_TEMP  = 0x8003;  /* Notifier */

/* Eventgroup */
constexpr vsomeip::eventgroup_id_t EVENTGROUP_VEHICLE  = 0x0001;

/* ============================================================
 * 字节序转换 (SOME/IP 使用 Big-Endian 网络字节序)
 * ============================================================ */
namespace internal {

inline bool is_little_endian() {
    uint16_t v = 1;
    return (*reinterpret_cast<uint8_t*>(&v)) == 1;
}

inline uint16_t h2n_u16(uint16_t v) {
    if (!is_little_endian()) return v;
    return static_cast<uint16_t>((v >> 8) | (v << 8));
}

inline uint32_t h2n_u32(uint32_t v) {
    if (!is_little_endian()) return v;
    return ((v >> 24) & 0xFF)
         | ((v >>  8) & 0xFF00)
         | ((v <<  8) & 0xFF0000)
         | ((v << 24) & 0xFF000000);
}

inline uint64_t h2n_u64(uint64_t v) {
    if (!is_little_endian()) return v;
    return (static_cast<uint64_t>(h2n_u32(static_cast<uint32_t>(v >> 32))))
         | (static_cast<uint64_t>(h2n_u32(static_cast<uint32_t>(v))) << 32);
}

inline double h2n_double(double v) {
    if (!is_little_endian()) return v;
    uint64_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    bits = h2n_u64(bits);
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

inline double n2h_double(double v) { return h2n_double(v); }  /* 对称操作 */

} // namespace internal

/* ============================================================
 * 数据结构
 * ============================================================ */

struct VehicleSpeed {
    double kmh = 0.0;
};

struct DoorState {
    bool open = false;
};

struct WindowPosition {
    int percent = 0;   /* 0=关闭, 100=全开 */
};

struct ClimateSettings {
    double temperature = 22.0;   /* 目标温度 °C */
    int    fan_speed   = 3;      /* 风扇档位 0~7 */
    bool   ac_on       = true;   /* AC 开关 */
};

/* ============================================================
 * 序列化辅助函数
 * ============================================================ */

/* 创建 payload 的便捷函数 */
inline std::shared_ptr<vsomeip::payload> make_payload(
    const std::vector<vsomeip::byte_t> &data)
{
    auto p = vsomeip::runtime::get()->create_payload();
    p->set_data(data);
    return p;
}

/* ---- 速度 ---- */
inline std::shared_ptr<vsomeip::payload> serialize_speed(double kmh)
{
    std::vector<vsomeip::byte_t> data(sizeof(double));
    double net = internal::h2n_double(kmh);
    std::memcpy(data.data(), &net, sizeof(double));
    return make_payload(data);
}

inline double deserialize_speed(const std::shared_ptr<vsomeip::payload> &p)
{
    if (!p || p->get_length() < sizeof(double)) return 0.0;
    double net;
    std::memcpy(&net, p->get_data(), sizeof(double));
    return internal::n2h_double(net);
}

/* ---- 车门 ---- */
inline std::shared_ptr<vsomeip::payload> serialize_door(bool open)
{
    std::vector<vsomeip::byte_t> data(1);
    data[0] = open ? 1 : 0;
    return make_payload(data);
}

inline bool deserialize_door(const std::shared_ptr<vsomeip::payload> &p)
{
    if (!p || p->get_length() < 1) return false;
    return p->get_data()[0] != 0;
}

/* ---- 车窗 ---- */
inline std::shared_ptr<vsomeip::payload> serialize_window(int percent)
{
    std::vector<vsomeip::byte_t> data(sizeof(int32_t));
    int32_t net = static_cast<int32_t>(internal::h2n_u32(static_cast<uint32_t>(percent)));
    std::memcpy(data.data(), &net, sizeof(int32_t));
    return make_payload(data);
}

inline int deserialize_window(const std::shared_ptr<vsomeip::payload> &p)
{
    if (!p || p->get_length() < sizeof(int32_t)) return 0;
    int32_t net;
    std::memcpy(&net, p->get_data(), sizeof(int32_t));
    return static_cast<int>(internal::h2n_u32(static_cast<uint32_t>(net)));
}

/* ---- 空调温度 (double) ---- */
inline std::shared_ptr<vsomeip::payload> serialize_climate_temp(double temp)
{
    return serialize_speed(temp);  /* 同样的 double 序列化 */
}

inline double deserialize_climate_temp(const std::shared_ptr<vsomeip::payload> &p)
{
    return deserialize_speed(p);  /* 同样的 double 反序列化 */
}

/* ---- 空调设置 (完整结构) ---- */
inline std::shared_ptr<vsomeip::payload> serialize_climate(const ClimateSettings &c)
{
    /* 布局: temperature(8) + fan_speed(4) + ac_on(1) = 13 bytes */
    std::vector<vsomeip::byte_t> data(13);
    double net_temp = internal::h2n_double(c.temperature);
    int32_t net_fan = static_cast<int32_t>(
        internal::h2n_u32(static_cast<uint32_t>(c.fan_speed)));
    std::memcpy(&data[0], &net_temp, 8);
    std::memcpy(&data[8], &net_fan, 4);
    data[12] = c.ac_on ? 1 : 0;
    return make_payload(data);
}

inline ClimateSettings deserialize_climate(const std::shared_ptr<vsomeip::payload> &p)
{
    ClimateSettings c;
    if (!p || p->get_length() < 13) return c;
    double net_temp; int32_t net_fan;
    std::memcpy(&net_temp, p->get_data(), 8);
    std::memcpy(&net_fan, p->get_data() + 8, 4);
    c.temperature = internal::n2h_double(net_temp);
    c.fan_speed   = static_cast<int>(internal::h2n_u32(static_cast<uint32_t>(net_fan)));
    c.ac_on       = (p->get_data()[12] != 0);
    return c;
}

/* ============================================================
 * 打印辅助
 * ============================================================ */
inline void print_ids()
{
    std::cout << "  Service:  0x" << std::hex << SERVICE_ID
              << "  Instance: 0x" << INSTANCE_ID << std::dec << std::endl;
    std::cout << "  Events:   SPEED=0x" << std::hex << EVENT_SPEED
              << " DOOR=0x" << EVENT_DOOR
              << " CLIMATE=0x" << EVENT_CLIMATE_TEMP << std::dec << std::endl;
    std::cout << "  Methods:  WINDOW=0x" << std::hex << METHOD_WINDOW
              << " CLIMATE_GET=0x" << METHOD_CLIMATE_GET
              << " CLIMATE_SET=0x" << METHOD_CLIMATE_SET << std::dec << std::endl;
}

} // namespace vehicle

#endif // VEHICLE_SOMEIP_HPP
