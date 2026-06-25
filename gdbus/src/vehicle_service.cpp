/**
 * vehicle_service.cpp — Implementation of VehicleService D-Bus service
 *
 * Bridges the GLib C callback API to C++ member functions via static trampolines.
 * Uses std::atomic for thread-safe state and RAII for resource management.
 */

#include "vehicle_service.hpp"

#include <cstdio>
#include <random>

// ==========================================================================
// Construction / Destruction
// ==========================================================================

VehicleService::VehicleService() {
    // Parse introspection XML into GDBusNodeInfo
    GError* err = nullptr;
    GDBusNodeInfo* info = g_dbus_node_info_new_for_xml(INTROSPECTION_XML, &err);
    if (err) {
        g_print("[SERVICE] 解析 XML 失败: %s\n", err->message);
        g_error_free(err);
    }
    node_info_.reset(info);
}

VehicleService::~VehicleService() {
    stop();
}

// ==========================================================================
// Service lifecycle
// ==========================================================================

void VehicleService::start() {
    if (running_) return;

    g_print("=== GDBus C++17 车辆信息服务 ===\n");
    g_print("总线名:   com.example.VehicleService\n");
    g_print("对象路径: /com/example/Vehicle\n");
    g_print("接口:     com.example.Vehicle\n\n");

    running_ = true;

    // Own bus name — on_bus_acquired callback handles object registration
    bus_name_.own(
        G_BUS_TYPE_SESSION,
        "com.example.VehicleService",
        // on_bus_acquired
        [this](GDBusConnection* conn, const std::string& /*name*/) {
            connection_ = conn;
            g_print("[SERVICE] 已获取 D-Bus 连接\n");

            // Build interface vtable — bridge C callbacks to C++ members
            static const GDBusInterfaceVTable vtable = {
                s_method_call,   // method_call
                s_get_property,  // get_property
                nullptr,         // set_property
                { 0, }
            };

            // Register object
            GError* err = nullptr;
            reg_id_ = g_dbus_connection_register_object(
                conn,
                "/com/example/Vehicle",
                node_info_->interfaces[0],
                &vtable,
                this,       // user_data → passed to static callbacks
                nullptr,    // user_data_free_func
                &err);

            if (err) {
                g_print("[SERVICE] 注册对象失败: %s\n", err->message);
                g_error_free(err);
                return;
            }
            g_print("[SERVICE] 已注册对象 /com/example/Vehicle (reg_id=%u)\n", reg_id_);

            // Start speed simulation timer (every 2 seconds)
            timer_id_ = g_timeout_add_seconds(2, s_speed_timer, this);
            g_print("[SERVICE] 已启动速度模拟 (2s 间隔)\n");
            g_print("[SERVICE] 按 Ctrl+C 退出\n\n");
        },
        // on_name_lost
        [this](GDBusConnection* /*conn*/, const std::string& /*name*/) {
            g_print("[SERVICE] 总线名丢失, 退出\n");
            if (timer_id_ > 0) {
                g_source_remove(timer_id_);
                timer_id_ = 0;
            }
            running_ = false;
        });
}

void VehicleService::stop() {
    if (timer_id_ > 0) {
        g_source_remove(timer_id_);
        timer_id_ = 0;
    }
    running_ = false;
    // bus_name_ destructor calls g_bus_unown_name
}

// ==========================================================================
// Method call handler
// ==========================================================================

void VehicleService::on_method_call(const std::string& sender,
                                    const std::string& method_name,
                                    GVariant* /*parameters*/,
                                    GDBusMethodInvocation* invocation)
{
    if (method_name == "GetVehicleInfo") {
        double spd = speed_.load();
        double odo = odometer_.load();

        g_print("[SERVICE] 收到 GetVehicleInfo 请求 (来自 %s)\n", sender.c_str());

        GVariant* result = g_variant_new("(sdd)",
                                         vehicle_id_.c_str(),
                                         spd,
                                         odo);
        g_dbus_method_invocation_return_value(invocation, result);
        g_print("[SERVICE] 返回: id=%s, speed=%.1f km/h, odo=%.1f km\n",
                vehicle_id_.c_str(), spd, odo);
    } else {
        g_print("[SERVICE] 未知方法: %s\n", method_name.c_str());
        g_dbus_method_invocation_return_error(invocation,
                                              G_DBUS_ERROR,
                                              G_DBUS_ERROR_UNKNOWN_METHOD,
                                              "Unknown method: %s",
                                              method_name.c_str());
    }
}

// ==========================================================================
// Property get handler
// ==========================================================================

GVariant* VehicleService::on_get_property(const std::string& property_name,
                                          GError** error)
{
    if (property_name == "Speed") {
        double spd = speed_.load();
        g_print("[SERVICE] 属性读取: Speed = %.1f km/h\n", spd);
        return g_variant_new_double(spd);
    }

    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY,
                "Unknown property: %s", property_name.c_str());
    return nullptr;
}

// ==========================================================================
// Signal emitters
// ==========================================================================

void VehicleService::emit_speed_changed(double new_speed) {
    g_print("[SERVICE] 发出信号: SpeedChanged(%.1f km/h)\n", new_speed);

    GError* err = nullptr;
    g_dbus_connection_emit_signal(
        connection_,
        nullptr,                   // destination (broadcast)
        "/com/example/Vehicle",    // object_path
        "com.example.Vehicle",     // interface_name
        "SpeedChanged",            // signal_name
        g_variant_new("(d)", new_speed),
        &err);

    if (err) {
        g_print("[SERVICE] 信号发送失败: %s\n", err->message);
        g_error_free(err);
    }
}

void VehicleService::emit_properties_changed(const std::map<std::string, GVariant*>& changed) {
    // Build the "changed" dictionary: a{sv}
    GVariantBuilder* changed_builder =
        g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));

    for (const auto& [key, value] : changed) {
        g_variant_builder_add(changed_builder, "{sv}",
                              key.c_str(), value);
    }

    // Build the "invalidated" array: as (empty in our case)
    GVariantBuilder* invalidated_builder =
        g_variant_builder_new(G_VARIANT_TYPE("as"));

    g_dbus_connection_emit_signal(
        connection_,
        nullptr,
        "/com/example/Vehicle",
        "org.freedesktop.DBus.Properties",
        "PropertiesChanged",
        g_variant_new("(sa{sv}as)",
                      "com.example.Vehicle",
                      changed_builder,
                      invalidated_builder),
        nullptr);

    g_variant_builder_unref(changed_builder);
    g_variant_builder_unref(invalidated_builder);
}

// ==========================================================================
// Speed simulation timer
// ==========================================================================

gboolean VehicleService::on_speed_timer() {
    // Simulate speed variation with a random walk (-5 to +5 km/h per tick)
    thread_local std::random_device rd;
    thread_local std::mt19937 gen(rd());
    thread_local std::uniform_real_distribution<double> dist(-5.0, 5.0);

    double delta = dist(gen);
    double new_speed = speed_.load() + delta;

    // Clamp
    if (new_speed < 0.0)  new_speed = 0.0;
    if (new_speed > 120.0) new_speed = 120.0;

    // Update odometer (2-second tick)
    double odo = odometer_.load() + new_speed / 3600.0 * 2.0;

    speed_.store(new_speed);
    odometer_.store(odo);

    // Emit signals
    emit_speed_changed(new_speed);

    // Emit PropertiesChanged
    std::map<std::string, GVariant*> changed;
    GVariant* speed_v = g_variant_new_double(new_speed);
    changed["Speed"] = speed_v;
    emit_properties_changed(changed);

    return G_SOURCE_CONTINUE;
}

// ==========================================================================
// Static C callback trampolines
// ==========================================================================

void VehicleService::s_method_call(GDBusConnection* conn,
                                   const gchar* sender,
                                   const gchar* object_path,
                                   const gchar* iface_name,
                                   const gchar* method_name,
                                   GVariant* parameters,
                                   GDBusMethodInvocation* invocation,
                                   gpointer user_data)
{
    (void)conn;
    (void)object_path;
    (void)iface_name;

    auto* self = static_cast<VehicleService*>(user_data);
    self->on_method_call(sender ? sender : "",
                         method_name ? method_name : "",
                         parameters,
                         invocation);
}

GVariant* VehicleService::s_get_property(GDBusConnection* conn,
                                         const gchar* sender,
                                         const gchar* object_path,
                                         const gchar* iface_name,
                                         const gchar* property_name,
                                         GError** error,
                                         gpointer user_data)
{
    (void)conn;
    (void)sender;
    (void)object_path;
    (void)iface_name;

    auto* self = static_cast<VehicleService*>(user_data);
    return self->on_get_property(property_name ? property_name : "", error);
}

gboolean VehicleService::s_speed_timer(gpointer user_data) {
    auto* self = static_cast<VehicleService*>(user_data);
    return self->on_speed_timer();
}
