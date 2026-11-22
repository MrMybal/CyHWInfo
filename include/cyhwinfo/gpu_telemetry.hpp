#pragma once

#include <cyhwinfo/gpu.hpp>

#include <memory>
#include <string_view>

namespace cyhwinfo::gpu {

struct EngineClock {
    std::uint32_t engine;
    Reading<std::uint64_t> frequency_hz;
    Reading<std::uint64_t> max_frequency_hz;
    Reading<std::uint32_t> voltage_millivolts;
};

struct PhysicalAdapterSensors {
    std::uint32_t physical_index;
    Reading<double> temperature_celsius;
    Reading<double> temperature_warning_celsius;
    Reading<double> temperature_max_celsius;
    Reading<std::uint32_t> fan_rpm;
    Reading<std::uint32_t> max_fan_rpm;
    Reading<double> power_percent_of_limit;
    Reading<std::uint64_t> memory_frequency_hz;
    Reading<std::uint64_t> max_memory_frequency_hz;
    std::vector<EngineClock> engines;
};

struct Sample {
    Timestamp timestamp;
    bool suspended;
    Reading<double> utilization_percent;
    Reading<double> memory_utilization_percent;
    Reading<double> video_utilization_percent;
    Reading<std::uint64_t> dedicated_memory_used_bytes;
    Reading<std::uint64_t> dedicated_memory_total_bytes;
    Reading<double> temperature_celsius;
    Reading<double> power_watts;
    Reading<std::uint64_t> frequency_hz;
    Reading<std::uint64_t> memory_frequency_hz;
    Reading<double> fan_percent;
    Reading<PcieLink> pcie_link;
    std::vector<PhysicalAdapterSensors> physical_adapters;
    std::optional<Error> upstream_error;
};

// Create one sampler per adapter and polling loop. Rates need an initial measurement interval.
class Sampler {
public:
    [[nodiscard]] static Result<Sampler> open(std::string_view device_id, InventoryOptions options = {});
    ~Sampler();
    Sampler(Sampler&&) noexcept;
    Sampler& operator=(Sampler&&) noexcept;

    [[nodiscard]] Result<Sample> sample();

private:
    struct State;
    explicit Sampler(std::unique_ptr<State> state);
    std::unique_ptr<State> state_;
};

} // namespace cyhwinfo::gpu
