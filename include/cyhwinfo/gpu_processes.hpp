#pragma once

#include <cyhwinfo/gpu.hpp>

#include <memory>

namespace cyhwinfo::gpu {

struct ProcessMemory {
    // Local/non-local usage charged to the process by WDDM.
    Reading<std::uint64_t> local_bytes;
    Reading<std::uint64_t> non_local_bytes;

    // Visible allocations can include memory shared with other processes.
    Reading<std::uint64_t> dedicated_usage_bytes;
    Reading<std::uint64_t> shared_usage_bytes;
    Reading<std::uint64_t> committed_bytes;
};

struct ProcessEngine {
    std::uint32_t physical_index;
    std::uint32_t engine;
    std::wstring type;
    Reading<double> utilization_percent;
};

struct ProcessUsage {
    std::uint32_t pid;
    ProcessMemory memory;
    std::vector<ProcessEngine> engines;
    Reading<double> busiest_engine_percent;
};

struct ProcessSample {
    Timestamp timestamp;
    bool adapter_matched;
    Reading<std::uint64_t> adapter_dedicated_usage_bytes;
    Reading<std::uint64_t> adapter_shared_usage_bytes;
    Reading<double> adapter_busiest_engine_percent;
    std::vector<ProcessUsage> processes;
};

// Uses WDDM performance counters on Windows. Keep the sampler alive between polls.
class ProcessSampler {
public:
    [[nodiscard]] static Result<ProcessSampler> open(const Device& device);
    ~ProcessSampler();
    ProcessSampler(ProcessSampler&&) noexcept;
    ProcessSampler& operator=(ProcessSampler&&) noexcept;

    [[nodiscard]] Result<ProcessSample> sample();

private:
    struct State;
    explicit ProcessSampler(std::unique_ptr<State> state);
    std::unique_ptr<State> state_;
};

} // namespace cyhwinfo::gpu
