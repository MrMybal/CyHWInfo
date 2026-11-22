#pragma once

#include <cyhwinfo/core.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace cyhwinfo::cpu {

enum class CoreKind { unknown, performance, efficiency };

struct Cache {
    std::optional<std::uint64_t> l1_data_bytes;
    std::optional<std::uint64_t> l1_instruction_bytes;
    std::optional<std::uint64_t> l2_bytes;
    std::optional<std::uint64_t> l3_bytes;
};

struct Core {
    std::uint32_t id;
    std::uint32_t threads;
    CoreKind kind;
    Cache cache;
    std::optional<std::uint64_t> base_frequency_hz;
    std::optional<std::uint64_t> max_frequency_hz;
    std::vector<std::uint32_t> logical_ids;
    std::optional<std::uint8_t> efficiency_class;
};

struct Package {
    std::uint32_t socket;
    std::string vendor;
    std::string model;
    std::uint32_t physical_cores;
    std::uint32_t logical_cores;
    std::vector<std::string> flags;
    std::vector<Core> cores;
};

struct Load {
    Timestamp timestamp;
    double total_percent;
    // Indexed by OS logical processor number.
    std::vector<double> logical_percent;
};

[[nodiscard]] Result<std::vector<Package>> enumerate();
[[nodiscard]] Result<std::vector<std::uint64_t>> frequencies_hz();

// Captures a baseline on construction. Sample at intervals; use one sampler per polling loop.
class Sampler {
public:
    Sampler();
    ~Sampler();
    Sampler(Sampler&&) noexcept;
    Sampler& operator=(Sampler&&) noexcept;

    [[nodiscard]] Result<Load> sample();

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace cyhwinfo::cpu
