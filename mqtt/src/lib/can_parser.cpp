/**
 * can_parser.cpp — CAN 帧解析器实现
 */

#include "mqtt_can/can_parser.hpp"
#include <cstdio>
#include <iostream>

namespace mqtt_can {

CanParser::CanParser(std::string vehicle_id)
    : vehicle_id_(std::move(vehicle_id))
{}

int CanParser::feed(const CanFrame& frame)
{
    auto& db = CanDatabase::instance();
    const CanMessage* msg = db.find_message(frame.can_id);
    if (!msg) return 0;

    int parsed = 0;
    for (const auto& sig : msg->signals) {
        uint64_t raw = extract_bits(frame.data, sig.start_bit,
                                     sig.length, sig.byte_order);
        signals_[sig.name] = sig.decode(raw);
        parsed++;
    }
    return parsed;
}

int CanParser::feed(const std::vector<CanFrame>& frames)
{
    int total = 0;
    for (const auto& f : frames)
        total += feed(f);
    return total;
}

std::optional<float> CanParser::get(const std::string& signal_name) const
{
    auto it = signals_.find(signal_name);
    if (it != signals_.end())
        return it->second;
    return std::nullopt;
}

std::map<std::string, float> CanParser::all_signals() const
{
    return signals_;
}

void CanParser::print_snapshot() const
{
    static const std::pair<const char*, const char*> labels[] = {
        {"EngineSpeed",      "发动机转速"},
        {"VehicleSpeed",     "车速"},
        {"CoolantTemp",      "水温"},
        {"BatteryVoltage",   "电池电压"},
        {"FuelLevel",        "燃油"},
        {"ThrottlePosition", "油门"},
        {"GearPosition",     "档位"},
        {"SteeringAngle",    "方向盘转角"},
        {"SteeringRate",     "转向速率"},
        {"Odometer",         "里程"},
        {"BrakeSwitch",      "制动"},
    };
    static const char* units[] = {
        "rpm", "km/h", "°C", "V", "%", "%", "", "deg", "deg/s", "km", "",
    };
    static const char* gear_names[] = {"?", "P", "R", "N", "D", "S"};
    static const char* brake_names[] = {"OFF", "ON", "ABS"};

    std::printf("[%s] ", vehicle_id_.c_str());
    bool first = true;
    for (size_t i = 0; i < sizeof(labels)/sizeof(labels[0]); i++) {
        auto val = get(labels[i].first);
        if (val) {
            if (!first) std::printf(" | ");
            first = false;

            if (std::string(labels[i].first) == "GearPosition") {
                int g = static_cast<int>(*val);
                std::printf("%s=%s", labels[i].second,
                            (g >= 0 && g <= 5) ? gear_names[g] : "?");
            } else if (std::string(labels[i].first) == "BrakeSwitch") {
                int b = static_cast<int>(*val);
                std::printf("%s=%s", labels[i].second,
                            (b >= 0 && b <= 2) ? brake_names[b] : "?");
            } else {
                std::printf("%s=%.1f%s", labels[i].second, *val, units[i]);
            }
        }
    }
    std::printf("\n");
}

void CanParser::clear()
{
    signals_.clear();
}

} // namespace mqtt_can
