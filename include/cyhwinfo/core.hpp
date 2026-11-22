#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace cyhwinfo {

enum class Source {
    unknown, hwinfo, windows_pdh, windows_d3dkmt, windows_nvme, windows_ata, libre_hardware_monitor,
    amd_smu, amd_umc, amd_thermal_controller
};
enum class Status { available, unavailable, unsupported, permission_denied, not_found, error };

enum class SensorMetric : std::uint8_t {
    temperature_celsius, power_watts, voltage_volts, fan_rpm, current_amperes, clock_megahertz,
    load_percent, frequency_hertz, flow_liters_per_hour, control_percent, level_percent, factor,
    data_gibibytes, data_mebibytes, throughput_bytes_per_second, time_seconds, timing_nanoseconds,
    energy_milliwatt_hours, noise_dba, conductivity_microsiemens_per_cm, humidity_percent,
    timing_cycles, transfer_rate_mtps, boolean_flag, bandwidth_gibibytes_per_second,
    c_state_residency_percent, average_active_core_count, count
};

using Timestamp = std::chrono::system_clock::time_point;

struct Error {
    Status status;
    Source source;
    std::error_code code;
    std::string message;
};

template<class T>
using Result = std::expected<T, Error>;

template<class T>
struct Reading {
    std::optional<T> value;
    Source source{};
    Status status = Status::unavailable;

    [[nodiscard]] static Reading observed(T value, Source source) {
        return {std::move(value), source, Status::available};
    }
};

} // namespace cyhwinfo
