/**
 * vehicle_client.hpp — C++17 Vehicle D-Bus Client Proxy
 *
 * Connects to the VehicleService on the session bus and provides:
 *   - Async method call: GetVehicleInfo()
 *   - Signal subscription: SpeedChanged
 *   - Property polling: Speed (via org.freedesktop.DBus.Properties)
 *
 * All callbacks use std::function for flexible usage.
 */

#pragma once

#include "gdbus_cxx.hpp"
#include <string>

/// Result of GetVehicleInfo method call
struct VehicleInfo {
    std::string vehicle_id;
    double speed     = 0.0;
    double odometer  = 0.0;
};

/**
 * VehicleClient — C++17 D-Bus client proxy
 *
 * Usage:
 *   VehicleClient client;
 *   client.on_speed_changed = [](double s) { printf("Speed: %.1f\n", s); };
 *   client.on_info_received = [](const VehicleInfo& i) { ... };
 *   client.connect();  // async — callbacks fire when results arrive
 */
class VehicleClient {
public:
    /// Callback types
    using SpeedCallback  = std::function<void(double new_speed)>;
    using InfoCallback   = std::function<void(const VehicleInfo& info)>;
    using ErrorCallback  = std::function<void(const std::string& error)>;

    /// Signal: called when SpeedChanged signal is received
    SpeedCallback on_speed_changed;

    /// Signal: called when GetVehicleInfo result arrives
    InfoCallback on_info_received;

    /// Signal: called on any error
    ErrorCallback on_error;

    VehicleClient() = default;
    ~VehicleClient();

    VehicleClient(const VehicleClient&) = delete;
    VehicleClient& operator=(const VehicleClient&) = delete;

    /**
     * Connect to session bus and start listening.
     * Non-blocking — call before entering the main loop.
     */
    void connect();

    /**
     * Call GetVehicleInfo method asynchronously.
     * Result delivered via on_info_received callback.
     */
    void get_vehicle_info();

    /**
     * Read the Speed property via org.freedesktop.DBus.Properties.Get.
     * Result is delivered to on_speed_read callback (or logged).
     */
    void read_speed();

    /**
     * Start periodic property polling (every N seconds).
     */
    void start_property_polling(std::chrono::seconds interval);

    /// Access the underlying connection
    GDBusConnection* connection() const noexcept { return connection_; }

private:
    /// Called when D-Bus connection is established
    void on_connected(GDBusConnection* conn);

    /// Called when SpeedChanged signal arrives
    void on_speed_signal(double new_speed);

    GDBusConnection* connection_ = nullptr;
    gdbus_cxx::SignalSubscription speed_sub_;
    guint poll_timer_id_ = 0;
};
