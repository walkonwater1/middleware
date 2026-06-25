/**
 * can_simulator.cpp — CAN 总线模拟器实现
 */

#include "mqtt_can/can_simulator.hpp"
#include <random>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace mqtt_can {

namespace {

// 随机数生成器
std::mt19937& rng() {
    static std::mt19937 r(std::random_device{}());
    return r;
}

double gauss_random(double mean = 0.0, double stddev = 1.0) {
    std::normal_distribution<double> dist(mean, stddev);
    return dist(rng());
}

double uniform_random() {
    static std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(rng());
}

// 默认冷启动状态
const VehicleState COLD_STATE{
    800.0f,  0.0f,  0.0f,  25.0f, 12.6f, 80.0f,
    1.0f,    0.0f,  0.0f,  1.0f,  50000.0f,
};

// 默认行驶状态
const VehicleState DRIVING_STATE{
    1800.0f, 60.0f, 25.0f, 90.0f, 13.8f, 65.0f,
    4.0f,    0.0f,  0.0f,  0.0f,  52300.0f,
};

} // anonymous namespace

// ============================================================
// CanSimulator
// ============================================================

CanSimulator::CanSimulator(bool cold_start)
    : state_(cold_start ? COLD_STATE : DRIVING_STATE)
    , start_time_(std::chrono::steady_clock::now())
{
    auto& db = CanDatabase::instance();
    last_send_.resize(db.all_messages().size(), 0.0);
}

void CanSimulator::update_signal(const CanSignal& sig, double dt)
{
    auto& s = state_;
    double target, current, rate;

    if (sig.name == "EngineSpeed") {
        int gear = static_cast<int>(s.gear_position);
        if (gear == 1) {
            target = 800.0 + gauss_random(0.0, 20.0);
        } else if (gear == 2) {
            target = std::max(600.0, 800.0 + gauss_random(0.0, 50.0));
        } else {
            target = 800.0 + s.throttle_position * 50.0
                   + s.vehicle_speed * 30.0 + gauss_random(0.0, 50.0);
        }
        target = std::clamp(target, 600.0, 7000.0);
        s.engine_speed += static_cast<float>((target - s.engine_speed) * 0.3 * dt);
    }
    else if (sig.name == "VehicleSpeed") {
        int gear = static_cast<int>(s.gear_position);
        double accel = 0.0;
        if (gear == 4) {
            accel = s.throttle_position * 1.5 - s.vehicle_speed * 0.05;
            if (s.brake_switch >= 1.0f) accel -= 8.0;
        } else if (gear == 2) {
            accel = -s.throttle_position * 0.8;
        } else {
            accel = -s.vehicle_speed * 2.0;
        }
        s.vehicle_speed = static_cast<float>(
            std::clamp(s.vehicle_speed + accel * dt, 0.0, 200.0));
    }
    else if (sig.name == "CoolantTemp") {
        target = 90.0 + gauss_random(0.0, 2.0);
        s.coolant_temp += static_cast<float>((target - s.coolant_temp) * 0.05 * dt);
    }
    else if (sig.name == "BatteryVoltage") {
        target = (s.engine_speed > 700.0f) ? 13.8 : 12.4;
        double new_val = s.battery_voltage + (target - s.battery_voltage) * 0.1 * dt;
        s.battery_voltage = static_cast<float>(std::clamp(new_val, 11.5, 14.5));
    }
    else if (sig.name == "FuelLevel") {
        s.fuel_level = std::max(5.0f, s.fuel_level - static_cast<float>(0.002 * dt));
    }
    else if (sig.name == "ThrottlePosition") {
        s.throttle_position += static_cast<float>((uniform_random() * 10.0 - 5.0) * dt);
        s.throttle_position = std::clamp(s.throttle_position, 0.0f, 100.0f);
        if (s.vehicle_speed < 30.0f && uniform_random() < 0.02) {
            s.throttle_position = std::min(80.0f, s.throttle_position + 20.0f);
        }
    }
    else if (sig.name == "GearPosition") {
        if (uniform_random() < 0.001) {
            if (s.vehicle_speed < 1.0f) {
                float choices[] = {1.0f, 3.0f, 4.0f};
                s.gear_position = choices[std::uniform_int_distribution<int>(0, 2)(rng())];
            } else {
                float choices[] = {3.0f, 4.0f};
                s.gear_position = choices[std::uniform_int_distribution<int>(0, 1)(rng())];
            }
        }
        if (s.vehicle_speed > 5.0f && s.gear_position == 1.0f)
            s.gear_position = 4.0f;
    }
    else if (sig.name == "SteeringAngle") {
        target = gauss_random(0.0, std::max(5.0, 40.0 - s.vehicle_speed * 0.3));
        target = std::clamp(target, -500.0, 500.0);
        s.steering_angle += static_cast<float>((target - s.steering_angle) * 0.5 * dt);
    }
    else if (sig.name == "SteeringRate") {
        s.steering_rate = static_cast<float>(
            std::abs(s.steering_angle) * 0.5 + gauss_random(0.0, 10.0));
    }
    else if (sig.name == "Odometer") {
        s.odometer += static_cast<float>(s.vehicle_speed / 3600.0 * dt);
    }
    else if (sig.name == "BrakeSwitch") {
        if (s.vehicle_speed < 1.0f) {
            s.brake_switch = 1.0f;
        } else {
            if (uniform_random() < 0.005) s.brake_switch = 1.0f;
            else if (uniform_random() < 0.01) s.brake_switch = 0.0f;
        }
    }
}

CanFrame CanSimulator::build_frame(const CanMessage& msg)
{
    const double dt = 0.01;

    // 更新所有信号
    for (const auto& sig : msg.signals)
        update_signal(sig, dt);

    // 编码
    CanFrame frame;
    frame.can_id = msg.can_id;
    frame.dlc = msg.dlc;
    frame.data = {};

    for (const auto& sig : msg.signals) {
        float physical = 0.0f;
        if (sig.name == "EngineSpeed")      physical = state_.engine_speed;
        else if (sig.name == "VehicleSpeed")     physical = state_.vehicle_speed;
        else if (sig.name == "BrakeSwitch")      physical = state_.brake_switch;
        else if (sig.name == "CoolantTemp")      physical = state_.coolant_temp;
        else if (sig.name == "BatteryVoltage")   physical = state_.battery_voltage;
        else if (sig.name == "FuelLevel")        physical = state_.fuel_level;
        else if (sig.name == "ThrottlePosition") physical = state_.throttle_position;
        else if (sig.name == "GearPosition")     physical = state_.gear_position;
        else if (sig.name == "SteeringAngle")    physical = state_.steering_angle;
        else if (sig.name == "SteeringRate")     physical = state_.steering_rate;
        else if (sig.name == "Odometer")         physical = state_.odometer;

        uint64_t raw = sig.encode(physical);
        pack_bits(frame.data, sig.start_bit, sig.length, raw, sig.byte_order);
    }

    return frame;
}

std::vector<CanFrame> CanSimulator::tick(int max_frames)
{
    auto now = std::chrono::steady_clock::now();
    double now_sec = std::chrono::duration<double>(now - start_time_).count();

    auto& db = CanDatabase::instance();
    const auto& messages = db.all_messages();

    std::vector<CanFrame> result;

    for (size_t i = 0; i < messages.size() && static_cast<int>(result.size()) < max_frames; i++) {
        double elapsed = now_sec - last_send_[i];
        double period_sec = messages[i].period_ms / 1000.0;

        if (elapsed >= period_sec) {
            last_send_[i] = now_sec;

            auto frame = build_frame(messages[i]);

            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - start_time_).count();
            frame.timestamp_ms = static_cast<uint32_t>(ms);

            result.push_back(std::move(frame));
        }
    }

    return result;
}

} // namespace mqtt_can
