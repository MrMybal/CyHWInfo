#pragma once

#include <cyhwinfo/core.hpp>

#include <cstdint>
#include <vector>

namespace cyhwinfo::battery {

enum class State { unknown, charging, discharging, full, not_charging };

struct Device {
    std::uint32_t index;
    std::optional<std::string> vendor;
    std::optional<std::string> model;
    std::optional<std::string> serial_number;
    std::optional<std::string> technology;
    std::optional<std::uint64_t> design_capacity_microwatt_hours;
    std::optional<std::uint64_t> full_charge_capacity_microwatt_hours;
};

struct Charge {
    Timestamp timestamp;
    State state;
    Reading<double> percent;
};

[[nodiscard]] Result<std::vector<Device>> enumerate();
[[nodiscard]] Result<Charge> charge(std::uint32_t index);

} // namespace cyhwinfo::battery
