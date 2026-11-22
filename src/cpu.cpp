#include <cyhwinfo/cpu.hpp>

#include "hwinfo_adapter.hpp"

#include <hwinfo/cpu.h>
#include <hwinfo/monitoring.h>

namespace cyhwinfo::cpu {
namespace {

CoreKind kind_of(hwinfo::CoreType type) {
    switch (type) {
    case hwinfo::CoreType::performance: return CoreKind::performance;
    case hwinfo::CoreType::efficiency: return CoreKind::efficiency;
    case hwinfo::CoreType::unknown: return CoreKind::unknown;
    }
    return CoreKind::unknown;
}

} // namespace

Result<std::vector<Package>> enumerate() {
    auto packages = hwinfo::cpus();
    if (!packages) {
        return detail::upstream_error(packages.error());
    }
    using detail::quantity_value;
    std::vector<Package> result;
    result.reserve(packages->size());
    for (auto& package : *packages) {
        std::vector<Core> cores;
        cores.reserve(package.cores.size());
        for (auto& core : package.cores) {
            const auto& cache = core.cache;
            cores.push_back({core.id, core.threads, kind_of(core.type),
                             {quantity_value(cache.l1_data), quantity_value(cache.l1_instruction),
                              quantity_value(cache.l2), quantity_value(cache.l3)},
                             quantity_value(core.base_frequency), quantity_value(core.max_frequency),
                             std::move(core.logical_ids), core.efficiency_class});
        }
        result.push_back({package.socket, std::move(package.vendor), std::move(package.model),
                          package.physical_cores, package.logical_cores, std::move(package.flags), std::move(cores)});
    }
    return result;
}

Result<std::vector<std::uint64_t>> frequencies_hz() {
    const auto frequencies = hwinfo::cpu_frequencies();
    if (!frequencies) {
        return detail::upstream_error(frequencies.error());
    }
    std::vector<std::uint64_t> result;
    result.reserve(frequencies->size());
    for (const auto frequency : *frequencies) {
        result.push_back(frequency.value);
    }
    return result;
}

struct Sampler::State {
    hwinfo::CpuSampler upstream;
};

Sampler::Sampler() : state_(std::make_unique<State>()) {}
Sampler::~Sampler() = default;
Sampler::Sampler(Sampler&&) noexcept = default;
Sampler& Sampler::operator=(Sampler&&) noexcept = default;

Result<Load> Sampler::sample() {
    if (!state_) {
        return std::unexpected(Error{Status::error, Source::hwinfo, {}, "CPU sampler has been moved"});
    }
    auto load = state_->upstream.sample();
    if (!load) {
        return detail::upstream_error(load.error());
    }
    for (auto& value : load->per_thread) {
        value *= 100;
    }
    return Load{std::chrono::system_clock::now(), load->total * 100, std::move(load->per_thread)};
}

} // namespace cyhwinfo::cpu
