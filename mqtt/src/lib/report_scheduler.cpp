/**
 * report_scheduler.cpp — 国标/企标数据上报调度器实现
 */

#include "mqtt_can/report_scheduler.hpp"
#include <cstdio>
#include <sstream>
#include <cmath>

namespace mqtt_can {

// ============================================================
// ReportScheduler
// ============================================================

ReportScheduler::ReportScheduler(std::vector<ReportItem> items, std::string vehicle_id)
    : items_(std::move(items))
    , vehicle_id_(std::move(vehicle_id))
{
    for (const auto& item : items_)
        states_[item.signal_name] = {};
}

void ReportScheduler::set_vehicle_status(const std::string& status)
{
    vehicle_status_ = status;
    std::printf("[Scheduler] 车辆状态: %s\n", status.c_str());
}

static float round_to(float val, float precision)
{
    if (precision <= 0) return val;
    return std::round(val / precision) * precision;
}

std::string ReportScheduler::build_json(const CanParser& parser,
                                         const std::string& topic_hint) const
{
    std::ostringstream oss;
    oss << "{\"vehicle_id\":\"" << vehicle_id_ << "\""
        << ",\"ts\":" << std::time(nullptr)
        << ",\"status\":\"" << vehicle_status_ << "\"";

    if (!topic_hint.empty())
        oss << ",\"report_type\":\"" << topic_hint << "\"";

    oss << ",\"data\":{";
    bool first = true;
    for (const auto& item : items_) {
        auto val = parser.get(item.signal_name);
        if (!val) continue;

        float rounded = round_to(*val, item.precision);
        if (!first) oss << ",";
        first = false;
        oss << "\"" << item.gb_key << "\":"
            << std::fixed << rounded;
    }
    oss << "}}";
    return oss.str();
}

int ReportScheduler::tick(const CanParser& parser, const PublishFunc& publish)
{
    int reported = 0;
    auto now = std::chrono::steady_clock::now();

    for (const auto& item : items_) {
        auto val = parser.get(item.signal_name);
        if (!val) continue;

        auto& state = states_[item.signal_name];
        bool should_report = false;
        std::string reason;

        switch (item.policy) {
        case ReportPolicy::Periodic: {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                now - state.last_periodic).count();
            if (elapsed >= item.interval_sec) {
                should_report = true;
                reason = "periodic";
                state.last_periodic = now;
            }
            break;
        }
        case ReportPolicy::OnChange: {
            if (!state.has_last) {
                should_report = true;
                reason = "init";
            } else {
                float delta = std::abs(*val - state.last_value);
                if (item.change_threshold > 0) {
                    if (delta >= item.change_threshold) {
                        should_report = true;
                        reason = "changed(Δ=" + std::to_string(delta) + ")";
                    }
                } else {
                    // 阈值=0: 有变化即报
                    if (delta > 0.001f) {
                        should_report = true;
                        reason = "changed";
                    }
                }
            }
            break;
        }
        case ReportPolicy::PeriodicAndOnChange: {
            // 先检查变化
            if (state.has_last) {
                float delta = std::abs(*val - state.last_value);
                if (item.change_threshold > 0 && delta >= item.change_threshold) {
                    should_report = true;
                    reason = "changed(Δ=" + std::to_string(delta) + ")";
                    state.last_periodic = now;  // 变化上报重置周期计时
                }
            }
            // 再检查周期
            if (!should_report) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    now - state.last_periodic).count();
                if (elapsed >= item.interval_sec) {
                    should_report = true;
                    reason = "periodic";
                    state.last_periodic = now;
                }
            }
            break;
        }
        }

        if (should_report) {
            float rounded = round_to(*val, item.precision);
            state.last_value = rounded;
            state.has_last = true;

            // 构建单条信号的 JSON
            std::ostringstream oss;
            oss << "{\"vehicle_id\":\"" << vehicle_id_ << "\""
                << ",\"ts\":" << std::time(nullptr)
                << ",\"status\":\"" << vehicle_status_ << "\""
                << ",\"signal\":\"" << item.gb_key << "\""
                << ",\"value\":" << std::fixed << rounded
                << ",\"unit\":\"" << item.unit << "\""
                << ",\"reason\":\"" << reason << "\""
                << ",\"policy\":\""
                << (item.policy == ReportPolicy::Periodic ? "periodic" :
                    item.policy == ReportPolicy::OnChange  ? "on_change" : "mixed")
                << "\"}";

            publish(oss.str(), item.gb_key);
            reported++;
        }
    }

    return reported;
}

void ReportScheduler::force_report_all(const CanParser& parser,
                                        const PublishFunc& publish)
{
    auto json = build_json(parser, "force");
    publish(json, "all_signals");
}

// ============================================================
// 国标 GB/T 32960-2016 信号定义
// ============================================================

std::vector<ReportItem> gb32960_signals()
{
    return {
        // --- 整车数据 (周期 ≤30s) ---
        {"VehicleSpeed",     "vehicle_speed",     "km/h", ReportPolicy::Periodic, 30, 0, 0.1f},
        {"Odometer",         "odometer",          "km",   ReportPolicy::Periodic, 30, 0, 0.1f},
        {"GearPosition",     "gear",              "",     ReportPolicy::PeriodicAndOnChange, 30, 0, 1},
        // --- 驱动电机数据 ---
        {"EngineSpeed",      "motor_speed",       "rpm",  ReportPolicy::Periodic, 30, 0, 1},
        // --- 燃料电池 / 发动机 ---
        {"CoolantTemp",      "coolant_temp",      "°C",   ReportPolicy::Periodic, 30, 5, 0.5f},
        {"FuelLevel",        "fuel_level",        "%",    ReportPolicy::Periodic, 30, 5, 0.5f},
        // --- 极值数据 ---
        {"BatteryVoltage",   "battery_voltage",   "V",    ReportPolicy::Periodic, 30, 0, 0.1f},
        {"ThrottlePosition", "throttle_position", "%",    ReportPolicy::Periodic, 30, 0, 0.5f},
        // --- 报警数据 (变化即报) ---
        {"BrakeSwitch",      "brake_status",      "",     ReportPolicy::OnChange, 0, 0, 1},
    };
}

// ============================================================
// 企标扩展信号 (变化上报)
// ============================================================

std::vector<ReportItem> enterprise_signals()
{
    return {
        // 转向系统 — 异常转角立即上报
        {"SteeringAngle",    "steering_angle",    "deg",   ReportPolicy::OnChange, 0, 30, 0.1f},
        {"SteeringRate",     "steering_rate",     "deg/s", ReportPolicy::OnChange, 0, 50, 1},
        // 急加速/急减速检测 (油门变化)
        {"ThrottlePosition", "throttle_position", "%",     ReportPolicy::PeriodicAndOnChange, 10, 20, 0.5f},
        // 制动事件立即上报
        {"BrakeSwitch",      "brake_switch",      "",      ReportPolicy::OnChange, 0, 0, 1},
        // 电池异常
        {"BatteryVoltage",   "battery_voltage",   "V",     ReportPolicy::PeriodicAndOnChange, 10, 0.5f, 0.1f},
    };
}

} // namespace mqtt_can
