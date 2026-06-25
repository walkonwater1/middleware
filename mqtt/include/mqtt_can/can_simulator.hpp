/**
 * mqtt_can/can_simulator.hpp — CAN 总线模拟器
 *
 * 模拟真实汽车 CAN 总线数据流。按报文周期调度帧发送，
 * 信号值使用随机游走模拟传感器的连续变化行为。
 */

#ifndef MQTT_CAN_SIMULATOR_HPP
#define MQTT_CAN_SIMULATOR_HPP

#include "mqtt_can/can_database.hpp"
#include <cstdint>
#include <array>
#include <chrono>

namespace mqtt_can {

// ============================================================
// 数据结构
// ============================================================

/** 原始 CAN 帧 */
struct CanFrame {
    uint32_t            can_id       = 0;
    uint32_t            timestamp_ms = 0;
    std::array<uint8_t, 8> data     = {};
    uint8_t             dlc          = 8;
};

/** 车辆物理状态 */
struct VehicleState {
    float engine_speed      = 800.0f;    // rpm
    float vehicle_speed     = 0.0f;      // km/h
    float throttle_position = 0.0f;      // %
    float coolant_temp      = 25.0f;     // °C
    float battery_voltage   = 12.6f;     // V
    float fuel_level        = 80.0f;     // %
    float gear_position     = 1.0f;      // 0=Unknown,1=P,2=R,3=N,4=D,5=S
    float steering_angle    = 0.0f;      // deg
    float steering_rate     = 0.0f;      // deg/s
    float brake_switch      = 1.0f;      // 0=off,1=on,2=ABS
    float odometer          = 50000.0f;  // km
};

// ============================================================
// 模拟器
// ============================================================

class CanSimulator {
public:
    /** 初始化，cold_start=true 表示冷启动状态 */
    explicit CanSimulator(bool cold_start = false);

    /**
     * 推进一帧时间，返回当前该发送的 CAN 帧。
     * @param max_frames 最大可存放帧数
     * @return 实际产生的帧列表
     */
    std::vector<CanFrame> tick(int max_frames = 8);

    /** 获取当前车辆状态 (只读) */
    const VehicleState& state() const { return state_; }

private:
    void update_signal(const CanSignal& sig, double dt);
    CanFrame build_frame(const CanMessage& msg);

    VehicleState state_;
    std::chrono::steady_clock::time_point start_time_;
    std::vector<double> last_send_;
};

} // namespace mqtt_can

#endif // MQTT_CAN_SIMULATOR_HPP
