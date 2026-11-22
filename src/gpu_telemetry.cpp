#include <cyhwinfo/gpu_telemetry.hpp>

#include "gpu_detail.hpp"
#include "hwinfo_adapter.hpp"

#include <hwinfo/monitoring.h>

#include <algorithm>
#include <bit>

#ifdef _WIN32
#include <windows.h>
#include <winternl.h>
#include <d3dkmthk.h>
#endif

namespace cyhwinfo::gpu {
namespace {

template<class T>
Reading<T> reading(std::optional<T> value, Source source = Source::hwinfo) {
    return {value, source, value ? Status::available : Status::unavailable};
}

std::optional<double> percent(const std::optional<double>& ratio) {
    return ratio.transform([](double value) { return value * 100; });
}

#ifdef _WIN32

// Adapted from windows_exporter, internal/collector/gpu/sensors.go (MIT).
// Copyright The Prometheus Authors. See THIRD-PARTY-NOTICES.txt.
struct AdapterHandle {
    D3DKMT_HANDLE value = 0;

    ~AdapterHandle() {
        if (value != 0) {
            const D3DKMT_CLOSEADAPTER request{value};
            D3DKMTCloseAdapter(&request);
        }
    }

    template<class Data>
    bool query(KMTQUERYADAPTERINFOTYPE type, Data& data) const {
        D3DKMT_QUERYADAPTERINFO request{value, type, &data, sizeof(Data)};
        return D3DKMTQueryAdapterInfo(&request) >= 0;
    }
};

struct PhysicalSupport {
    bool temperature = false;
    bool fan = false;
    bool power = false;
    bool memory_clock = false;
};

std::vector<PhysicalAdapterSensors> read_windows_sensors(std::uint64_t luid,
                                                        std::vector<PhysicalSupport>& known_support) {
    D3DKMT_OPENADAPTERFROMLUID open{};
    open.AdapterLuid.LowPart = static_cast<DWORD>(luid);
    open.AdapterLuid.HighPart = std::bit_cast<LONG>(static_cast<DWORD>(luid >> 32));
    if (D3DKMTOpenAdapterFromLuid(&open) < 0) {
        return {};
    }
    const AdapterHandle handle{open.hAdapter};
    D3DKMT_PHYSICAL_ADAPTER_COUNT count{};
    if (!handle.query(KMTQAITYPE_PHYSICALADAPTERCOUNT, count) || count.Count == 0) {
        count.Count = 1;
    }
    constexpr UINT max_physical_adapters = 64;
    count.Count = std::min(count.Count, max_physical_adapters);
    known_support.resize(count.Count);
    std::vector<PhysicalAdapterSensors> adapters;
    for (UINT index = 0; index < count.Count; ++index) {
        D3DKMT_ADAPTER_PERFDATACAPS caps{};
        caps.PhysicalAdapterIndex = index;
        if (!handle.query(KMTQAITYPE_ADAPTERPERFDATA_CAPS, caps)) {
            caps = {};
        }
        D3DKMT_ADAPTER_PERFDATA data{};
        data.PhysicalAdapterIndex = index;
        const bool has_data = handle.query(KMTQAITYPE_ADAPTERPERFDATA, data);
        auto& support = known_support[index];
        support.temperature = support.temperature || caps.TemperatureWarning || caps.TemperatureMax ||
                              (has_data && data.Temperature);
        support.fan = support.fan || caps.MaxFanRPM || (has_data && data.FanRPM);
        support.power = support.power || (has_data && data.Power);
        support.memory_clock = support.memory_clock || (has_data && (data.MemoryFrequency || data.MaxMemoryFrequency));

        constexpr auto source = Source::windows_d3dkmt;
        PhysicalAdapterSensors sensors{};
        sensors.physical_index = index;
        sensors.temperature_celsius = reading<double>(
            has_data && support.temperature ? std::optional(data.Temperature / 10.0) : std::nullopt, source);
        sensors.temperature_warning_celsius = reading<double>(
            caps.TemperatureWarning ? std::optional(caps.TemperatureWarning / 10.0) : std::nullopt, source);
        sensors.temperature_max_celsius = reading<double>(
            caps.TemperatureMax ? std::optional(caps.TemperatureMax / 10.0) : std::nullopt, source);
        sensors.fan_rpm = reading<std::uint32_t>(
            has_data && support.fan ? std::optional(data.FanRPM) : std::nullopt, source);
        sensors.max_fan_rpm = reading<std::uint32_t>(caps.MaxFanRPM ? std::optional(caps.MaxFanRPM) : std::nullopt, source);
        sensors.power_percent_of_limit = reading<double>(
            has_data && support.power ? std::optional(data.Power / 10.0) : std::nullopt, source);
        sensors.memory_frequency_hz = reading<std::uint64_t>(
            has_data && support.memory_clock ? std::optional(data.MemoryFrequency) : std::nullopt, source);
        sensors.max_memory_frequency_hz = reading<std::uint64_t>(
            has_data && data.MaxMemoryFrequency ? std::optional(data.MaxMemoryFrequency) : std::nullopt, source);

        constexpr UINT max_engine_nodes = 64;
        for (UINT node = 0; node < max_engine_nodes; ++node) {
            D3DKMT_NODE_PERFDATA engine{};
            engine.NodeOrdinal = node;
            engine.PhysicalAdapterIndex = index;
            if (!handle.query(KMTQAITYPE_NODEPERFDATA, engine)) {
                break;
            }
            if (engine.Frequency || engine.MaxFrequency || engine.Voltage || engine.VoltageMax) {
                sensors.engines.push_back({
                    node,
                    reading<std::uint64_t>(engine.Frequency || engine.MaxFrequency
                        ? std::optional(engine.Frequency) : std::nullopt, source),
                    reading<std::uint64_t>(engine.MaxFrequency ? std::optional(engine.MaxFrequency) : std::nullopt, source),
                    reading<std::uint32_t>(engine.Voltage || engine.VoltageMax
                        ? std::optional(engine.Voltage) : std::nullopt, source),
                });
            }
        }
        if (has_data || !sensors.engines.empty() || caps.MaxFanRPM || caps.TemperatureMax || caps.TemperatureWarning) {
            adapters.push_back(std::move(sensors));
        }
    }
    return adapters;
}
#endif

} // namespace

struct Sampler::State {
    State(const hwinfo::Gpu& adapter, const hwinfo::GpuQuery& query)
        : upstream(adapter, query)
#ifdef _WIN32
        , luid(adapter.luid)
#endif
    {}

    hwinfo::GpuSampler upstream;
#ifdef _WIN32
    std::optional<std::uint64_t> luid;
    std::vector<PhysicalSupport> known_support;
#endif
};

Sampler::Sampler(std::unique_ptr<State> state) : state_(std::move(state)) {}
Sampler::~Sampler() = default;
Sampler::Sampler(Sampler&&) noexcept = default;
Sampler& Sampler::operator=(Sampler&&) noexcept = default;

Result<Sampler> Sampler::open(std::string_view device_id, InventoryOptions options) {
    const auto query = options.vendor_details ? hwinfo::GpuQuery{} : hwinfo::GpuQuery::os_only();
    const auto adapters = hwinfo::gpus(query);
    if (!adapters) {
        return cyhwinfo::detail::upstream_error(adapters.error());
    }
    const auto adapter = std::ranges::find_if(*adapters, [device_id](const auto& gpu) {
        return detail::id_of(gpu) == device_id;
    });
    if (adapter == adapters->end()) {
        return std::unexpected(Error{Status::not_found, Source::hwinfo, {}, "GPU ID was not found"});
    }
    return Sampler(std::make_unique<State>(*adapter, query));
}

Result<Sample> Sampler::sample() {
    if (!state_) {
        return std::unexpected(Error{Status::error, Source::hwinfo, {}, "GPU sampler has been moved"});
    }
    Sample result{};
    const auto upstream = state_->upstream.sample();
    if (upstream) {
        using cyhwinfo::detail::quantity_value;
        result.suspended = upstream->suspended;
        result.utilization_percent = reading(percent(upstream->utilization));
        result.memory_utilization_percent = reading(percent(upstream->memory_utilization));
        result.video_utilization_percent = reading(percent(upstream->video_utilization));
        result.dedicated_memory_used_bytes = reading(quantity_value(upstream->memory_used));
        result.dedicated_memory_total_bytes = reading(quantity_value(upstream->memory_total));
        result.temperature_celsius = reading(upstream->temperature);
        result.power_watts = reading(upstream->power.transform([](auto power) { return power.to(hwinfo::PowerUnit::W); }));
        result.frequency_hz = reading(quantity_value(upstream->frequency));
        result.memory_frequency_hz = reading(quantity_value(upstream->memory_frequency));
        result.fan_percent = reading(percent(upstream->fan_speed));
        result.pcie_link = reading(upstream->pcie_link.transform(
            [](const auto& link) { return PcieLink{link.generation, link.width}; }));
    } else {
        result.upstream_error = cyhwinfo::detail::upstream_error(upstream.error()).error();
    }

#ifdef _WIN32
    if (state_->luid && !result.suspended) {
        result.physical_adapters = read_windows_sensors(*state_->luid, state_->known_support);
        if (result.physical_adapters.size() == 1 && result.physical_adapters.front().physical_index == 0 &&
            state_->known_support.size() == 1) {
            const auto& sensors = result.physical_adapters.front();
            if (!result.temperature_celsius.value) {
                result.temperature_celsius = sensors.temperature_celsius;
            }
            if (!result.memory_frequency_hz.value) {
                result.memory_frequency_hz = sensors.memory_frequency_hz;
            }
        }
    }
#endif
    if (result.upstream_error && result.physical_adapters.empty()) {
        return std::unexpected(std::move(*result.upstream_error));
    }
    result.timestamp = std::chrono::system_clock::now();
    return result;
}

} // namespace cyhwinfo::gpu
