#pragma once

#include <cyhwinfo/core.hpp>

namespace cyhwinfo::motherboard {

struct Inventory {
    std::optional<std::string> vendor;
    std::optional<std::string> name;
    std::optional<std::string> version;
    std::optional<std::string> serial_number;
    std::optional<std::string> firmware_vendor;
    std::optional<std::string> firmware_version;
    std::optional<std::chrono::year_month_day> firmware_date;
};

[[nodiscard]] Result<Inventory> inventory();

} // namespace cyhwinfo::motherboard
