/**
 * gdbus_cxx.hpp — C++17 RAII wrappers for GLib/GDBus fundamentals
 *
 * Only wraps the essentials that gdbus-codegen doesn't generate:
 *   - MainLoop:   RAII GMainLoop
 *   - BusName:    RAII bus name ownership
 *
 * All interface/proxy/signal handling is auto-generated from .xml by gdbus-codegen.
 */

#pragma once

#include <gio/gio.h>

#include <cstdio>
#include <functional>
#include <memory>
#include <string>

namespace gdbus_cxx {

// ==========================================================================
// Custom deleters
// ==========================================================================
struct MainLoopDeleter {
    void operator()(GMainLoop* p) const noexcept { if (p) g_main_loop_unref(p); }
};
struct ObjectDeleter {
    void operator()(gpointer p) const noexcept { if (p) g_object_unref(p); }
};

using MainLoopPtr = std::unique_ptr<GMainLoop, MainLoopDeleter>;

// ==========================================================================
// MainLoop — RAII GMainLoop
// ==========================================================================
class MainLoop {
public:
    MainLoop() : loop_(g_main_loop_new(nullptr, FALSE)) {}

    MainLoop(const MainLoop&) = delete;
    MainLoop& operator=(const MainLoop&) = delete;

    void run()     { g_main_loop_run(loop_.get()); }
    void quit()    { g_main_loop_quit(loop_.get()); }
    bool is_running() const { return g_main_loop_is_running(loop_.get()) != FALSE; }

private:
    MainLoopPtr loop_;
};

// ==========================================================================
// BusName — RAII bus name ownership (server-side)
// ==========================================================================
class BusName {
public:
    using AcquiredCb = std::function<void(GDBusConnection*)>;
    using LostCb     = std::function<void()>;

    BusName() = default;
    ~BusName();

    BusName(const BusName&) = delete;
    BusName& operator=(const BusName&) = delete;
    BusName(BusName&&) noexcept;
    BusName& operator=(BusName&&) noexcept;

    /**
     * Own a name on the session bus.
     * on_acquired: called with the connection after bus is acquired
     * on_lost:     called if the name is lost
     */
    void own(const std::string& name, AcquiredCb on_acquired, LostCb on_lost);

private:
    guint owner_id_ = 0;

    struct CbData {
        AcquiredCb acquired;
        LostCb     lost;
    };
    std::unique_ptr<CbData> cb_;

    static void bus_acquired_cb(GDBusConnection* c, const gchar* n, gpointer d);
    static void name_lost_cb(GDBusConnection* c, const gchar* n, gpointer d);
};

// ==========================================================================
// Utility: run g_timeout_add with a C++ lambda
// ==========================================================================
inline guint timeout_add_seconds(guint interval_sec, std::function<gboolean()> cb) {
    struct Wrapper {
        std::function<gboolean()> fn;
        static gboolean tramp(gpointer d) { return static_cast<Wrapper*>(d)->fn(); }
        static void destroy(gpointer d) { delete static_cast<Wrapper*>(d); }
    };
    auto* w = new Wrapper{std::move(cb)};
    return g_timeout_add_seconds_full(G_PRIORITY_DEFAULT, interval_sec,
                                      Wrapper::tramp, w, Wrapper::destroy);
}

} // namespace gdbus_cxx
