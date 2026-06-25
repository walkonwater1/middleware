/**
 * vehicle_service.hpp — C++17 Vehicle D-Bus Service
 *
 * Exposes a vehicle info service over D-Bus session bus:
 *   Bus Name:    com.example.VehicleService
 *   Object Path: /com/example/Vehicle
 *   Interface:   com.example.Vehicle
 *
 * Methods:  GetVehicleInfo() → (s) vehicle_id, (d) speed, (d) odometer
 * Signals:  SpeedChanged(d new_speed)
 *           PropertiesChanged (via org.freedesktop.DBus.Properties)
 * Property: Speed (read, double)
 */

#pragma once

#include "gdbus_cxx.hpp"
#include <atomic>
#include <map>
#include <string>

/**
 * VehicleService — C++17 RAII D-Bus service
 *
 * Owns the bus name and registers the vehicle object.
 * Simulates vehicle speed changes via a GLib timer.
 *
 * Usage:
 *   VehicleService svc;
 *   svc.start();
 *   // ... main loop runs ...
 */
class VehicleService {
public:
    /// XML introspection data defining the com.example.Vehicle interface
    static constexpr const char* INTROSPECTION_XML = R"xml(
<node>
  <interface name='com.example.Vehicle'>
    <method name='GetVehicleInfo'>
      <arg type='s' name='vehicle_id' direction='out'/>
      <arg type='d' name='speed'      direction='out'/>
      <arg type='d' name='odometer'   direction='out'/>
    </method>
    <signal name='SpeedChanged'>
      <arg type='d' name='new_speed'/>
    </signal>
    <property name='Speed' type='d' access='read'/>
  </interface>
</node>
)xml";

    VehicleService();
    ~VehicleService();

    VehicleService(const VehicleService&) = delete;
    VehicleService& operator=(const VehicleService&) = delete;

    /**
     * Start the service — own bus name, register object, begin simulation.
     * @param loop  The main loop to attach timers to (default: creates its own).
     */
    void start();

    /// Stop simulation and release bus name
    void stop();

    // --- State accessors (thread-safe) ---
    double current_speed() const noexcept { return speed_.load(); }
    double current_odometer() const noexcept { return odometer_.load(); }

private:
    // --- D-Bus method & property handlers ---

    /// Handle incoming method calls
    void on_method_call(const std::string& sender,
                        const std::string& method_name,
                        GVariant* parameters,
                        GDBusMethodInvocation* invocation);

    /// Handle property reads
    GVariant* on_get_property(const std::string& property_name,
                              GError** error);

    // --- Signal emitters ---

    /// Emit SpeedChanged(d) signal
    void emit_speed_changed(double new_speed);

    /// Emit org.freedesktop.DBus.Properties.PropertiesChanged
    void emit_properties_changed(const std::map<std::string, GVariant*>& changed);

    // --- Simulation ---

    /// Timer callback: update speed and emit signals
    gboolean on_speed_timer();

    // ==== Static C callbacks that bridge to member functions ====

    static void s_method_call(GDBusConnection* conn,
                              const gchar* sender,
                              const gchar* object_path,
                              const gchar* iface_name,
                              const gchar* method_name,
                              GVariant* parameters,
                              GDBusMethodInvocation* invocation,
                              gpointer user_data);

    static GVariant* s_get_property(GDBusConnection* conn,
                                    const gchar* sender,
                                    const gchar* object_path,
                                    const gchar* iface_name,
                                    const gchar* property_name,
                                    GError** error,
                                    gpointer user_data);

    static gboolean s_speed_timer(gpointer user_data);

    // --- Member data ---

    std::string vehicle_id_ = "VIN-ABC-123456789";
    std::atomic<double> speed_{0.0};
    std::atomic<double> odometer_{12345.6};

    GDBusConnection* connection_ = nullptr;  // borrowed pointer, owned by GLib
    gdbus_cxx::BusName bus_name_;
    gdbus_cxx::NodeInfoPtr node_info_;
    guint timer_id_ = 0;
    guint reg_id_ = 0;
    bool running_ = false;
};
