/**
 * gdbus_cxx.hpp — C++17 RAII wrappers for GLib/GDBus
 *
 * Provides modern C++ wrappers around the GLib C API:
 *   - UniquePtr:   std::unique_ptr with custom GLib deleters
 *   - MainLoop:    RAII GMainLoop
 *   - Variant:     Thin GVariant ownership wrapper
 *   - BusName:     RAII bus name ownership (g_bus_own_name)
 *   - SignalSub:   RAII signal subscription
 *
 * All GLib resources are automatically freed on scope exit.
 */

#pragma once

#include <gio/gio.h>

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace gdbus_cxx {

// ==========================================================================
// Custom deleters for GLib types
// ==========================================================================

struct MainLoopDeleter {
    void operator()(GMainLoop* p) const noexcept { if (p) g_main_loop_unref(p); }
};

struct VariantDeleter {
    void operator()(GVariant* p) const noexcept { if (p) g_variant_unref(p); }
};

struct NodeInfoDeleter {
    void operator()(GDBusNodeInfo* p) const noexcept { if (p) g_dbus_node_info_unref(p); }
};

struct ErrorDeleter {
    void operator()(GError* p) const noexcept { if (p) g_error_free(p); }
};

struct StrDeleter {
    void operator()(gchar* p) const noexcept { if (p) g_free(p); }
};

// ==========================================================================
// UniquePtr aliases (std::unique_ptr with GLib deleters)
// ==========================================================================

using MainLoopPtr  = std::unique_ptr<GMainLoop,   MainLoopDeleter>;
using VariantPtr   = std::unique_ptr<GVariant,     VariantDeleter>;
using NodeInfoPtr  = std::unique_ptr<GDBusNodeInfo, NodeInfoDeleter>;
using ErrorPtr     = std::unique_ptr<GError,       ErrorDeleter>;
using StrPtr       = std::unique_ptr<gchar,        StrDeleter>;

// ==========================================================================
// MainLoop — RAII GMainLoop
// ==========================================================================

class MainLoop {
public:
    MainLoop() : loop_(g_main_loop_new(nullptr, FALSE)) {}

    MainLoop(const MainLoop&) = delete;
    MainLoop& operator=(const MainLoop&) = delete;
    MainLoop(MainLoop&&) = default;
    MainLoop& operator=(MainLoop&&) = default;

    void run() { g_main_loop_run(loop_.get()); }
    void quit() { g_main_loop_quit(loop_.get()); }
    bool is_running() const { return g_main_loop_is_running(loop_.get()) != FALSE; }
    GMainLoop* get() noexcept { return loop_.get(); }

private:
    MainLoopPtr loop_;
};

// ==========================================================================
// Variant — thin GVariant ownership wrapper with convenience methods
// ==========================================================================

class Variant {
public:
    Variant() = default;
    explicit Variant(GVariant* v) : var_(v) {}

    Variant(const Variant&) = delete;
    Variant& operator=(const Variant&) = delete;
    Variant(Variant&&) = default;
    Variant& operator=(Variant&&) = default;

    GVariant* get() noexcept { return var_.get(); }
    const GVariant* get() const noexcept { return var_.get(); }
    GVariant* release() noexcept { return var_.release(); }
    explicit operator bool() const noexcept { return var_ != nullptr; }

    // --- Factory methods for common types ---
    static Variant create_string(const char* s) {
        return Variant(g_variant_new_string(s));
    }
    static Variant create_double(double d) {
        return Variant(g_variant_new_double(d));
    }
    static Variant create_int32(gint32 i) {
        return Variant(g_variant_new_int32(i));
    }
    static Variant create_uint32(guint32 u) {
        return Variant(g_variant_new_uint32(u));
    }
    static Variant create_tuple(GVariant** children, gsize n) {
        return Variant(g_variant_new_tuple(children, n));
    }

    // --- Type queries ---
    bool is_double() const noexcept {
        return g_variant_is_of_type(var_.get(), G_VARIANT_TYPE_DOUBLE);
    }
    std::string type_string() const {
        StrPtr s(g_variant_print(var_.get(), FALSE));
        return s ? s.get() : "";
    }

private:
    VariantPtr var_;
};

// ==========================================================================
// VariantBuilder — RAII GVariantBuilder
// ==========================================================================

class VariantBuilder {
public:
    explicit VariantBuilder(const GVariantType* type)
        : builder_(g_variant_builder_new(type)) {}

    ~VariantBuilder() { g_variant_builder_unref(builder_); }

    VariantBuilder(const VariantBuilder&) = delete;
    VariantBuilder& operator=(const VariantBuilder&) = delete;

    void add_value(GVariant* child) {
        g_variant_builder_add_value(builder_, child);
    }

    GVariant* end() { return g_variant_builder_end(builder_); }

    GVariantBuilder* get() noexcept { return builder_; }

private:
    GVariantBuilder* builder_;
};

// ==========================================================================
// Error helper
// ==========================================================================

inline std::string error_message(GError* err) {
    if (!err) return "unknown error";
    return std::string(err->message ? err->message : "(no message)");
}

// ==========================================================================
// Timeout helpers — wrap g_timeout_add into std::chrono
// ==========================================================================

using TimeoutCallback = std::function<gboolean()>;

inline guint timeout_add(std::chrono::milliseconds interval, TimeoutCallback cb) {
    // We need to pass the callback through user_data.
    // g_timeout_add uses GSourceFunc (gboolean (*)(gpointer)).
    // We'll allocate the callback on the heap and free it in a wrapper.

    struct CallbackWrapper {
        TimeoutCallback cb;
        static gboolean trampoline(gpointer data) {
            auto* self = static_cast<CallbackWrapper*>(data);
            return self->cb();
        }
        static void destroy(gpointer data) {
            delete static_cast<CallbackWrapper*>(data);
        }
    };

    auto* wrapper = new CallbackWrapper{std::move(cb)};
    return g_timeout_add_full(
        G_PRIORITY_DEFAULT,
        static_cast<guint>(interval.count()),
        CallbackWrapper::trampoline,
        wrapper,
        CallbackWrapper::destroy);
}

// ==========================================================================
// BusName — RAII bus name ownership (server-side)
// ==========================================================================

class BusName {
public:
    using AcquiredCallback = std::function<void(GDBusConnection*, const std::string&)>;
    using LostCallback     = std::function<void(GDBusConnection*, const std::string&)>;

    BusName() = default;

    /**
     * Own a name on the specified bus.
     * @param bus_type   G_BUS_TYPE_SESSION or G_BUS_TYPE_SYSTEM
     * @param name       e.g. "com.example.VehicleService"
     * @param on_acquired  Called when the bus connection is acquired
     * @param on_lost      Called if the name is lost
     */
    void own(GBusType bus_type,
             const std::string& name,
             AcquiredCallback on_acquired,
             LostCallback on_lost);

    ~BusName();

    BusName(const BusName&) = delete;
    BusName& operator=(const BusName&) = delete;
    BusName(BusName&& other) noexcept;
    BusName& operator=(BusName&& other) noexcept;

    guint id() const noexcept { return owner_id_; }

private:
    guint owner_id_ = 0;

    // Wrapper struct for callback data
    struct CallbackData {
        AcquiredCallback on_acquired;
        LostCallback     on_lost;
    };
    std::unique_ptr<CallbackData> cb_data_;

    // Static C callbacks that bridge to C++
    static void on_bus_acquired_cb(GDBusConnection* conn, const gchar* name, gpointer data);
    static void on_name_acquired_cb(GDBusConnection* conn, const gchar* name, gpointer data);
    static void on_name_lost_cb(GDBusConnection* conn, const gchar* name, gpointer data);
};

// ==========================================================================
// SignalSubscription — RAII signal subscription (client-side)
// ==========================================================================

class SignalSubscription {
public:
    using SignalCallback = std::function<void(
        GDBusConnection*, const std::string& sender,
        const std::string& object_path, const std::string& iface,
        const std::string& signal_name, GVariant* parameters)>;

    SignalSubscription() = default;

    /**
     * Subscribe to a D-Bus signal.
     */
    void subscribe(GDBusConnection* conn,
                   const std::string& sender,
                   const std::string& iface,
                   const std::string& signal_name,
                   const std::string& object_path,
                   SignalCallback cb);

    ~SignalSubscription();

    SignalSubscription(const SignalSubscription&) = delete;
    SignalSubscription& operator=(const SignalSubscription&) = delete;
    SignalSubscription(SignalSubscription&& other) noexcept;
    SignalSubscription& operator=(SignalSubscription&& other) noexcept;

private:
    guint sub_id_ = 0;
    GDBusConnection* conn_ = nullptr;  // stored for unsubscribe in destructor
    std::unique_ptr<SignalCallback> cb_;

    static void signal_cb(GDBusConnection* conn, const gchar* sender,
                          const gchar* object_path, const gchar* iface,
                          const gchar* signal_name, GVariant* parameters,
                          gpointer user_data);
};

// ==========================================================================
// Connection helper: get session bus connection asynchronously (client-side)
// ==========================================================================

using ConnectionCallback = std::function<void(GDBusConnection*, const std::string& error)>;

inline void connect_session_bus(ConnectionCallback cb) {
    struct Data {
        ConnectionCallback cb;
        static void on_done(GObject* src, GAsyncResult* res, gpointer data) {
            auto* d = static_cast<Data*>(data);
            GError* err = nullptr;
            GDBusConnection* conn = g_dbus_connection_new_for_address_finish(res, &err);
            if (err) {
                d->cb(nullptr, error_message(err));
                g_error_free(err);
            } else {
                d->cb(conn, "");
            }
            delete d;
        }
    };
    auto* d = new Data{std::move(cb)};
    g_dbus_connection_new_for_address(
        g_getenv("DBUS_SESSION_BUS_ADDRESS"),
        static_cast<GDBusConnectionFlags>(
            G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
            G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
        nullptr,           // GDBusAuthObserver
        nullptr,           // GCancellable
        Data::on_done,     // GAsyncReadyCallback
        d);                // user_data
}

// ==========================================================================
// Async method call helper
// ==========================================================================

using CallCallback = std::function<void(GVariant* result, const std::string& error)>;

inline void async_call(GDBusConnection* conn,
                       const std::string& bus_name,
                       const std::string& object_path,
                       const std::string& iface,
                       const std::string& method,
                       GVariant* params,
                       const GVariantType* reply_type,
                       CallCallback cb) {
    struct Data {
        CallCallback cb;
        static void on_done(GObject* src, GAsyncResult* res, gpointer data) {
            auto* d = static_cast<Data*>(data);
            GError* err = nullptr;
            GVariant* result = g_dbus_connection_call_finish(
                G_DBUS_CONNECTION(src), res, &err);
            if (err) {
                d->cb(nullptr, error_message(err));
                g_error_free(err);
            } else {
                d->cb(result, "");
                if (result) g_variant_unref(result);
            }
            delete d;
        }
    };
    auto* d = new Data{std::move(cb)};
    g_dbus_connection_call(conn, bus_name.c_str(), object_path.c_str(),
                          iface.c_str(), method.c_str(),
                          params, reply_type,
                          G_DBUS_CALL_FLAGS_NONE, -1, nullptr,
                          Data::on_done, d);
}

} // namespace gdbus_cxx
