/**
 * gdbus_cxx.cpp — Non-inline implementations for gdbus_cxx RAII wrappers
 *
 * Implements the C-to-C++ callback bridging for BusName and SignalSubscription.
 */

#include "gdbus_cxx.hpp"

namespace gdbus_cxx {

// ==========================================================================
// BusName
// ==========================================================================

void BusName::own(GBusType bus_type,
                  const std::string& name,
                  AcquiredCallback on_acquired,
                  LostCallback on_lost)
{
    cb_data_ = std::make_unique<CallbackData>();
    cb_data_->on_acquired = std::move(on_acquired);
    cb_data_->on_lost     = std::move(on_lost);

    owner_id_ = g_bus_own_name(
        bus_type,
        name.c_str(),
        G_BUS_NAME_OWNER_FLAGS_NONE,
        on_bus_acquired_cb,
        on_name_acquired_cb,
        on_name_lost_cb,
        cb_data_.get(),
        nullptr);  // user_data_free_func — we manage lifetime ourselves
}

BusName::~BusName() {
    if (owner_id_ != 0) {
        g_bus_unown_name(owner_id_);
    }
}

BusName::BusName(BusName&& other) noexcept
    : owner_id_(other.owner_id_)
    , cb_data_(std::move(other.cb_data_))
{
    other.owner_id_ = 0;
}

BusName& BusName::operator=(BusName&& other) noexcept {
    if (this != &other) {
        if (owner_id_ != 0) g_bus_unown_name(owner_id_);
        owner_id_ = other.owner_id_;
        cb_data_  = std::move(other.cb_data_);
        other.owner_id_ = 0;
    }
    return *this;
}

void BusName::on_bus_acquired_cb(GDBusConnection* conn, const gchar* name, gpointer data) {
    auto* self = static_cast<CallbackData*>(data);
    if (self->on_acquired) {
        self->on_acquired(conn, name ? name : "");
    }
}

void BusName::on_name_acquired_cb(GDBusConnection* /*conn*/, const gchar* name, gpointer /*data*/) {
    g_print("[SERVICE] 已获取总线名: %s\n", name);
}

void BusName::on_name_lost_cb(GDBusConnection* conn, const gchar* name, gpointer data) {
    auto* self = static_cast<CallbackData*>(data);
    if (self->on_lost) {
        self->on_lost(conn, name ? name : "");
    }
}

// ==========================================================================
// SignalSubscription
// ==========================================================================

void SignalSubscription::subscribe(GDBusConnection* conn,
                                   const std::string& sender,
                                   const std::string& iface,
                                   const std::string& signal_name,
                                   const std::string& object_path,
                                   SignalCallback cb)
{
    conn_ = conn;
    cb_ = std::make_unique<SignalCallback>(std::move(cb));

    sub_id_ = g_dbus_connection_signal_subscribe(
        conn,
        sender.empty() ? nullptr : sender.c_str(),
        iface.c_str(),
        signal_name.c_str(),
        object_path.c_str(),
        nullptr,  // arg0
        G_DBUS_SIGNAL_FLAGS_NONE,
        signal_cb,
        cb_.get(),
        nullptr);  // user_data_free_func — cb_ lifetime managed by us
}

SignalSubscription::~SignalSubscription() {
    if (sub_id_ != 0 && conn_ != nullptr) {
        g_dbus_connection_signal_unsubscribe(conn_, sub_id_);
    }
}

SignalSubscription::SignalSubscription(SignalSubscription&& other) noexcept
    : sub_id_(other.sub_id_)
    , conn_(other.conn_)
    , cb_(std::move(other.cb_))
{
    other.sub_id_ = 0;
    other.conn_   = nullptr;
}

SignalSubscription& SignalSubscription::operator=(SignalSubscription&& other) noexcept {
    if (this != &other) {
        // Unsubscribe our current subscription first
        if (sub_id_ != 0 && conn_ != nullptr) {
            g_dbus_connection_signal_unsubscribe(conn_, sub_id_);
        }
        sub_id_ = other.sub_id_;
        conn_   = other.conn_;
        cb_     = std::move(other.cb_);
        other.sub_id_ = 0;
        other.conn_   = nullptr;
    }
    return *this;
}

void SignalSubscription::signal_cb(GDBusConnection* conn,
                                   const gchar* sender,
                                   const gchar* object_path,
                                   const gchar* iface,
                                   const gchar* signal_name,
                                   GVariant* parameters,
                                   gpointer user_data)
{
    auto* self = static_cast<SignalCallback*>(user_data);
    if (*self) {
        (*self)(conn,
                sender ? sender : "",
                object_path ? object_path : "",
                iface ? iface : "",
                signal_name ? signal_name : "",
                parameters);
    }
}

} // namespace gdbus_cxx
