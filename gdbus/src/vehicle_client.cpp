/**
 * vehicle_client.cpp — Implementation of VehicleClient D-Bus proxy
 *
 * Demonstrates C++17 async patterns:
 *   - Structured bindings for parsing results
 *   - std::optional for nullable GVariant results
 *   - Lambda callbacks with std::function
 *   - RAII signal subscription
 */

#include "vehicle_client.hpp"

#include <cstdio>

// ==========================================================================
// Destruction
// ==========================================================================

VehicleClient::~VehicleClient() {
    if (poll_timer_id_ > 0) {
        g_source_remove(poll_timer_id_);
    }
}

// ==========================================================================
// Connection
// ==========================================================================

void VehicleClient::connect() {
    g_print("=== GDBus C++17 车辆信息客户端 ===\n\n");

    gdbus_cxx::connect_session_bus(
        [this](GDBusConnection* conn, const std::string& error) {
            if (!error.empty()) {
                g_print("[CLIENT] D-Bus 连接失败: %s\n", error.c_str());
                if (on_error) on_error(error);
                return;
            }
            on_connected(conn);
        });

    g_print("正在连接 D-Bus session bus...\n");
}

void VehicleClient::on_connected(GDBusConnection* conn) {
    connection_ = conn;
    g_print("[CLIENT] D-Bus 连接成功\n");

    // Subscribe to SpeedChanged signal
    speed_sub_.subscribe(
        conn,
        "com.example.VehicleService",  // sender
        "com.example.Vehicle",         // interface
        "SpeedChanged",                // signal
        "/com/example/Vehicle",        // object_path
        [this](GDBusConnection* /*conn*/,
               const std::string& /*sender*/,
               const std::string& /*path*/,
               const std::string& /*iface*/,
               const std::string& /*signal*/,
               GVariant* parameters) {
            // Parse "(d)" — speed value in a tuple
            double new_speed = 0.0;
            g_variant_get(parameters, "(d)", &new_speed);
            on_speed_signal(new_speed);
        });

    g_print("[CLIENT] 已订阅 SpeedChanged 信号\n");

    // Call GetVehicleInfo immediately
    get_vehicle_info();

    // Read Speed property
    read_speed();

    // Start polling every 5 seconds
    start_property_polling(std::chrono::seconds(5));
}

// ==========================================================================
// Method call: GetVehicleInfo
// ==========================================================================

void VehicleClient::get_vehicle_info() {
    if (!connection_) return;

    g_print("[CLIENT] 调用 GetVehicleInfo...\n");

    gdbus_cxx::async_call(
        connection_,
        "com.example.VehicleService",
        "/com/example/Vehicle",
        "com.example.Vehicle",
        "GetVehicleInfo",
        nullptr,                        // no input parameters
        G_VARIANT_TYPE("(sdd)"),        // expected reply type
        [this](GVariant* result, const std::string& error) {
            if (!error.empty()) {
                g_print("[CLIENT] GetVehicleInfo 调用失败: %s\n", error.c_str());
                if (on_error) on_error(error);
                return;
            }

            // C++17 structured bindings would be nice here, but we work with
            // GVariant's C API for parsing. We extract to our struct.
            const char* vid = nullptr;
            double speed, odo;

            g_variant_get(result, "(&sdd)", &vid, &speed, &odo);

            VehicleInfo info{std::string(vid), speed, odo};

            g_print("[CLIENT] GetVehicleInfo 结果:\n");
            g_print("  Vehicle ID: %s\n", info.vehicle_id.c_str());
            g_print("  Speed:      %.1f km/h\n", info.speed);
            g_print("  Odometer:   %.1f km\n", info.odometer);

            if (on_info_received) {
                on_info_received(info);
            }
        });
}

// ==========================================================================
// Property read: Speed
// ==========================================================================

void VehicleClient::read_speed() {
    if (!connection_) return;

    // Call org.freedesktop.DBus.Properties.Get("com.example.Vehicle", "Speed")
    // Returns variant (v) containing the property value
    GVariant* params = g_variant_new("(ss)", "com.example.Vehicle", "Speed");

    gdbus_cxx::async_call(
        connection_,
        "com.example.VehicleService",
        "/com/example/Vehicle",
        "org.freedesktop.DBus.Properties",
        "Get",
        params,
        G_VARIANT_TYPE("(v)"),          // reply: a variant
        [this](GVariant* result, const std::string& error) {
            if (!error.empty()) {
                g_print("[CLIENT] 属性读取失败: %s\n", error.c_str());
                if (on_error) on_error(error);
                return;
            }

            // Parse the variant wrapper
            GVariant* inner = nullptr;
            g_variant_get(result, "(v)", &inner);

            if (g_variant_is_of_type(inner, G_VARIANT_TYPE_DOUBLE)) {
                double speed = g_variant_get_double(inner);
                g_print("[CLIENT] 属性 Speed = %.1f km/h\n", speed);
            }

            g_variant_unref(inner);
        });
}

// ==========================================================================
// Periodic property polling
// ==========================================================================

void VehicleClient::start_property_polling(std::chrono::seconds interval) {
    g_print("[CLIENT] 启动属性轮询 (每 %lld 秒)\n",
            static_cast<long long>(interval.count()));

    poll_timer_id_ = gdbus_cxx::timeout_add(
        interval,
        [this]() -> gboolean {
            read_speed();
            return G_SOURCE_CONTINUE;
        });
}

// ==========================================================================
// Signal handler
// ==========================================================================

void VehicleClient::on_speed_signal(double new_speed) {
    g_print("[CLIENT] <<< 收到信号 SpeedChanged: %.1f km/h\n", new_speed);
    if (on_speed_changed) {
        on_speed_changed(new_speed);
    }
}
