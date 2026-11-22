#pragma once

#include <cyhwinfo/storage.hpp>

#include <array>

namespace cyhwinfo::storage {

struct Counter128 {
    std::uint64_t low;
    std::uint64_t high;
};

struct NvmeHealth {
    std::uint8_t critical_warning_flags;
    std::uint8_t available_spare_percent;
    std::uint8_t available_spare_threshold_percent;
    // NVMe allows values above 100.
    std::uint8_t percentage_used;
    Counter128 data_units_read;
    Counter128 data_units_written;
    Counter128 host_read_commands;
    Counter128 host_write_commands;
    Counter128 controller_busy_minutes;
    Counter128 power_cycles;
    Counter128 power_on_hours;
    Counter128 unsafe_shutdowns;
    Counter128 media_errors;
    Counter128 error_log_entries;
    std::uint32_t warning_temperature_minutes;
    std::uint32_t critical_temperature_minutes;
    std::array<Reading<double>, 8> temperature_sensors_celsius;
};

struct AtaAttribute {
    std::uint8_t id;
    std::uint16_t flags;
    std::uint8_t normalized;
    std::uint8_t worst;
    // Meaning depends on the attribute and the firmware.
    std::array<std::uint8_t, 6> raw;
};

struct Health {
    Timestamp timestamp;
    Source source;
    Reading<double> temperature_celsius;
    std::optional<NvmeHealth> nvme;
    std::vector<AtaAttribute> ata_attributes;
    std::optional<bool> ata_failure_predicted;
};

// Uses read-only SMART queries. Refresh the inventory after reconnecting a device.
[[nodiscard]] Result<Health> health(const Disk& disk);

} // namespace cyhwinfo::storage
