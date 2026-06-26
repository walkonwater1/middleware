/**
 * gdbus_cxx.cpp — Non-inline implementations for RAII wrappers
 */

#include "gdbus_cxx.hpp"

namespace gdbus_cxx {

// ==========================================================================
// BusName
// ==========================================================================

BusName::~BusName() {
    if (owner_id_ != 0)
        g_bus_unown_name(owner_id_);
}

BusName::BusName(BusName&& o) noexcept
    : owner_id_(o.owner_id_), cb_(std::move(o.cb_))
{ o.owner_id_ = 0; }

BusName& BusName::operator=(BusName&& o) noexcept {
    if (this != &o) {
        if (owner_id_) g_bus_unown_name(owner_id_);
        owner_id_ = o.owner_id_;
        cb_ = std::move(o.cb_);
        o.owner_id_ = 0;
    }
    return *this;
}

void BusName::own(const std::string& name, AcquiredCb on_acquired, LostCb on_lost) {
    cb_ = std::make_unique<CbData>();
    cb_->acquired = std::move(on_acquired);
    cb_->lost     = std::move(on_lost);

    owner_id_ = g_bus_own_name(
        G_BUS_TYPE_SESSION, name.c_str(), G_BUS_NAME_OWNER_FLAGS_NONE,
        bus_acquired_cb,
        nullptr,  // name_acquired — we don't need it
        name_lost_cb,
        cb_.get(), nullptr);
}

void BusName::bus_acquired_cb(GDBusConnection* c, const gchar*, gpointer d) {
    auto* self = static_cast<CbData*>(d);
    if (self->acquired) self->acquired(c);
}

void BusName::name_lost_cb(GDBusConnection*, const gchar*, gpointer d) {
    auto* self = static_cast<CbData*>(d);
    if (self->lost) self->lost();
}

} // namespace gdbus_cxx
