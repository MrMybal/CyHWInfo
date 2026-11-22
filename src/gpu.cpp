#include <cyhwinfo/gpu.hpp>

#include "gpu_detail.hpp"
#include "hwinfo_adapter.hpp"

namespace cyhwinfo::gpu {
namespace {

Kind kind_of(hwinfo::GpuType type) {
    switch (type) {
    case hwinfo::GpuType::integrated: return Kind::integrated;
    case hwinfo::GpuType::discrete: return Kind::discrete;
    case hwinfo::GpuType::virtualized: return Kind::virtualized;
    case hwinfo::GpuType::unknown: return Kind::unknown;
    }
    return Kind::unknown;
}

std::optional<PciDevice> pci_of(const std::optional<hwinfo::PciDevice>& pci) {
    return pci.transform([](const auto& device) {
        return PciDevice{device.vendor_id, device.device_id, device.address, device.max_link.transform(
            [](const auto& link) { return PcieLink{link.generation, link.width}; })};
    });
}

} // namespace

Result<std::vector<Device>> enumerate(InventoryOptions options) {
    auto adapters = hwinfo::gpus(options.vendor_details ? hwinfo::GpuQuery{} : hwinfo::GpuQuery::os_only());
    if (!adapters) {
        return cyhwinfo::detail::upstream_error(adapters.error());
    }
    using cyhwinfo::detail::quantity_value;
    std::vector<Device> result;
    result.reserve(adapters->size());
    for (auto& adapter : *adapters) {
        std::vector<ComputeApi> apis;
        for (auto& api : adapter.compute_apis) {
            apis.push_back({std::move(api.name), std::move(api.version)});
        }
        result.push_back({
            .id = detail::id_of(adapter),
            .index = adapter.index,
            .vendor = std::move(adapter.vendor),
            .name = std::move(adapter.name),
            .kind = kind_of(adapter.type),
            .unified_memory = adapter.unified_memory,
            .uuid = adapter.uuid.transform([](const auto& uuid) { return hwinfo::to_string(uuid); }),
            .pci = pci_of(adapter.pci),
            .architecture = std::move(adapter.architecture),
            .compute_capability = std::move(adapter.compute_capability),
            .compute_units = adapter.compute_units,
            .cores = adapter.cores,
            .driver = std::move(adapter.driver),
            .driver_version = std::move(adapter.driver_version),
            .vbios_version = std::move(adapter.vbios_version),
            .serial = std::move(adapter.serial),
            .dedicated_memory_bytes = quantity_value(adapter.dedicated_memory),
            .shared_memory_bytes = quantity_value(adapter.shared_memory),
            .memory_type = std::move(adapter.memory_type),
            .memory_bus_width_bits = adapter.memory_bus_width,
            .l2_cache_bytes = quantity_value(adapter.l2_cache),
            .max_frequency_hz = quantity_value(adapter.max_frequency),
            .max_memory_frequency_hz = quantity_value(adapter.max_memory_frequency),
            .power_limit_watts = adapter.power_limit.transform([](auto power) { return power.to(hwinfo::PowerUnit::W); }),
            .compute_apis = std::move(apis),
            .windows_luid = adapter.luid,
        });
    }
    return result;
}

} // namespace cyhwinfo::gpu
