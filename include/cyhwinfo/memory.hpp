#pragma once

#include <cyhwinfo/core.hpp>

#include <cstdint>
#include <vector>

namespace cyhwinfo::memory {

enum class FormFactor { unknown, dimm, sodimm, camm, soldered, other };

struct Module {
    std::uint32_t index;
    std::optional<std::string> vendor;
    std::optional<std::string> location;
    std::optional<std::string> bank;
    std::optional<std::string> model;
    std::optional<std::string> serial_number;
    std::optional<std::uint64_t> size_bytes;
    std::optional<std::uint64_t> configured_rate_mt_per_second;
    std::optional<std::uint64_t> rated_rate_mt_per_second;
    std::optional<std::string> type;
    FormFactor form_factor;
    std::optional<std::uint32_t> rank;
    std::optional<std::uint32_t> data_width_bits;
    std::optional<std::uint32_t> total_width_bits;
    std::optional<std::uint32_t> voltage_millivolts;
};

struct Inventory {
    std::uint64_t total_bytes;
    std::optional<std::vector<Module>> modules;
    std::optional<std::uint32_t> slots;
    std::optional<std::uint64_t> max_capacity_bytes;
};

struct Usage {
    Timestamp timestamp;
    std::uint64_t total_bytes;
    std::uint64_t free_bytes;
    std::uint64_t available_bytes;
};

[[nodiscard]] Result<Inventory> inventory();
[[nodiscard]] Result<Usage> usage();

} // namespace cyhwinfo::memory
