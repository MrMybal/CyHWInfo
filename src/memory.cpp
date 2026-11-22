#include <cyhwinfo/memory.hpp>

#include "hwinfo_adapter.hpp"

#include <hwinfo/ram.h>
#include <hwinfo/monitoring.h>

namespace cyhwinfo::memory {
namespace {

FormFactor form_of(hwinfo::MemoryFormFactor factor) {
    switch (factor) {
    case hwinfo::MemoryFormFactor::dimm: return FormFactor::dimm;
    case hwinfo::MemoryFormFactor::sodimm: return FormFactor::sodimm;
    case hwinfo::MemoryFormFactor::camm: return FormFactor::camm;
    case hwinfo::MemoryFormFactor::soldered: return FormFactor::soldered;
    case hwinfo::MemoryFormFactor::other: return FormFactor::other;
    case hwinfo::MemoryFormFactor::unknown: return FormFactor::unknown;
    }
    return FormFactor::unknown;
}

std::optional<std::uint64_t> rate(const std::optional<hwinfo::Hertz>& value) {
    return value.transform([](auto hz) { return hz.value / 1'000'000; });
}

} // namespace

Result<Inventory> inventory() {
    auto memory = hwinfo::memory();
    if (!memory) {
        return detail::upstream_error(memory.error());
    }
    std::optional<std::vector<Module>> modules;
    if (memory->modules) {
        modules.emplace().reserve(memory->modules->size());
        for (auto& module : *memory->modules) {
            modules->push_back({module.index, std::move(module.vendor), std::move(module.name),
                                std::move(module.bank), std::move(module.model), std::move(module.serial_number),
                                detail::quantity_value(module.size), rate(module.frequency), rate(module.max_frequency),
                                std::move(module.type), form_of(module.form_factor), module.rank,
                                module.data_width, module.total_width, module.voltage_mv});
        }
    }
    return Inventory{memory->total.value, std::move(modules), memory->slots,
                     detail::quantity_value(memory->max_capacity)};
}

Result<Usage> usage() {
    const auto result = hwinfo::memory_usage();
    if (!result) {
        return detail::upstream_error(result.error());
    }
    return Usage{std::chrono::system_clock::now(), result->total.value, result->free.value, result->available.value};
}

} // namespace cyhwinfo::memory
