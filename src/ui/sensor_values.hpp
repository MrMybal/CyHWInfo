#pragma once

#include <cyhwinfo/ui.hpp>

#include <cmath>
#include <format>
#include <string_view>

namespace cyhwinfo::ui::detail {

struct SensorFormat {
    std::string_view unit;
    int decimals;
};

inline SensorFormat sensor_format(SensorMetric metric) {
    switch (metric) {
    case SensorMetric::temperature_celsius: return {"°C", 1};
    case SensorMetric::power_watts: return {"W", 1};
    case SensorMetric::voltage_volts: return {"V", 3};
    case SensorMetric::fan_rpm: return {"RPM", 0};
    case SensorMetric::current_amperes: return {"A", 3};
    case SensorMetric::clock_megahertz: return {"MHz", 1};
    case SensorMetric::load_percent:
    case SensorMetric::control_percent:
    case SensorMetric::level_percent:
    case SensorMetric::humidity_percent: return {"%", 1};
    case SensorMetric::frequency_hertz: return {"Hz", 1};
    case SensorMetric::flow_liters_per_hour: return {"L/h", 1};
    case SensorMetric::factor: return {"", 2};
    case SensorMetric::data_gibibytes: return {"GiB", 2};
    case SensorMetric::data_mebibytes: return {"MiB", 1};
    case SensorMetric::throughput_bytes_per_second: return {"B/s", 1};
    case SensorMetric::time_seconds: return {"s", 1};
    case SensorMetric::timing_nanoseconds: return {"ns", 2};
    case SensorMetric::energy_milliwatt_hours: return {"mWh", 1};
    case SensorMetric::noise_dba: return {"dBA", 1};
    case SensorMetric::conductivity_microsiemens_per_cm: return {"µS/cm", 1};
    case SensorMetric::timing_cycles: return {"tCK", 0};
    case SensorMetric::transfer_rate_mtps: return {"MT/s", 0};
    case SensorMetric::boolean_flag: return {"", 0};
    case SensorMetric::bandwidth_gibibytes_per_second: return {"GiB/s", 3};
    case SensorMetric::c_state_residency_percent: return {"%", 2};
    case SensorMetric::average_active_core_count: return {"", 2};
    case SensorMetric::count: return {"", 2};
    }
    return {"", 2};
}

inline std::string sensor_text(SensorMetric metric, std::optional<double> value) {
    if (!value || !std::isfinite(*value)) {
        return "—";
    }
    if (metric == SensorMetric::boolean_flag) {
        return *value == 1.0 ? "Yes" : "No";
    }
    const auto [unit, decimals] = sensor_format(metric);
    return std::format("{:.{}f}{}{}", *value, decimals, unit.empty() ? "" : " ", unit);
}

inline const char* source_name(Source source) {
    switch (source) {
    case Source::hwinfo: return "hwinfo";
    case Source::windows_pdh: return "WDDM / PDH";
    case Source::windows_d3dkmt: return "WDDM / D3DKMT";
    case Source::windows_nvme: return "NVMe SMART";
    case Source::windows_ata: return "ATA SMART";
    case Source::libre_hardware_monitor: return "LibreHardwareMonitor";
    case Source::amd_smu: return "AMD SMU (PawnIO)";
    case Source::amd_umc: return "AMD UMC (PawnIO)";
    case Source::amd_thermal_controller: return "AMD THM (PawnIO)";
    case Source::unknown: return "—";
    }
    return "—";
}

} // namespace cyhwinfo::ui::detail
