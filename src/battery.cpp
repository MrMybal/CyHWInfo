#include <cyhwinfo/battery.hpp>

#include "hwinfo_adapter.hpp"

#include <hwinfo/battery.h>
#include <hwinfo/monitoring.h>

#ifdef _WIN32
#include <windows.h>
#endif

namespace cyhwinfo::battery {
namespace {

#ifdef _WIN32
bool known_no_battery() {
    SYSTEM_POWER_STATUS power{};
    return GetSystemPowerStatus(&power) && power.BatteryFlag != BATTERY_FLAG_UNKNOWN &&
           (power.BatteryFlag & BATTERY_FLAG_NO_BATTERY) != 0;
}
#endif

State state_of(hwinfo::BatteryState state) {
    switch (state) {
    case hwinfo::BatteryState::charging: return State::charging;
    case hwinfo::BatteryState::discharging: return State::discharging;
    case hwinfo::BatteryState::full: return State::full;
    case hwinfo::BatteryState::not_charging: return State::not_charging;
    case hwinfo::BatteryState::unknown: return State::unknown;
    }
    return State::unknown;
}

} // namespace

Result<std::vector<Device>> enumerate() {
#ifdef _WIN32
    if (known_no_battery()) {
        return std::vector<Device>{};
    }
#endif
    auto batteries = hwinfo::batteries();
    if (!batteries) {
        return detail::upstream_error(batteries.error());
    }
    std::vector<Device> result;
    result.reserve(batteries->size());
    for (auto& battery : *batteries) {
        result.push_back({battery.index, std::move(battery.vendor), std::move(battery.model),
                          std::move(battery.serial_number), std::move(battery.technology),
                          detail::quantity_value(battery.design_capacity),
                          detail::quantity_value(battery.full_charge_capacity)});
    }
    return result;
}

Result<Charge> charge(std::uint32_t index) {
#ifdef _WIN32
    if (known_no_battery()) {
        return std::unexpected(Error{Status::not_found, Source::hwinfo, {}, "No system battery is installed"});
    }
#endif
    const auto status = hwinfo::battery_status(index);
    if (!status) {
        return detail::upstream_error(status.error());
    }
    Reading<double> percent;
    if (status->charge) {
        percent = Reading<double>::observed(*status->charge * 100, Source::hwinfo);
    }
    return Charge{std::chrono::system_clock::now(), state_of(status->state), percent};
}

} // namespace cyhwinfo::battery
