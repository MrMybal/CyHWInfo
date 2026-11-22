#pragma once

#include <cyhwinfo/core.hpp>

#include <cstdint>
#include <vector>

namespace cyhwinfo::gpu {

enum class Kind { unknown, integrated, discrete, virtualized };

struct PcieLink {
    std::uint32_t generation;
    std::uint32_t lanes;
};

struct PciDevice {
    std::uint16_t vendor_id;
    std::uint16_t device_id;
    std::optional<std::string> address;
    std::optional<PcieLink> max_link;
};

struct ComputeApi {
    std::string name;
    std::string version;
};

struct Device {
    std::string id;
    std::uint32_t index;
    std::string vendor;
    std::string name;
    Kind kind;
    std::optional<bool> unified_memory;
    std::optional<std::string> uuid;
    std::optional<PciDevice> pci;
    std::optional<std::string> architecture;
    std::optional<std::string> compute_capability;
    std::optional<std::uint32_t> compute_units;
    std::optional<std::uint32_t> cores;
    std::optional<std::string> driver;
    std::optional<std::string> driver_version;
    std::optional<std::string> vbios_version;
    std::optional<std::string> serial;
    std::optional<std::uint64_t> dedicated_memory_bytes;
    std::optional<std::uint64_t> shared_memory_bytes;
    std::optional<std::string> memory_type;
    std::optional<std::uint32_t> memory_bus_width_bits;
    std::optional<std::uint64_t> l2_cache_bytes;
    std::optional<std::uint64_t> max_frequency_hz;
    std::optional<std::uint64_t> max_memory_frequency_hz;
    std::optional<double> power_limit_watts;
    std::vector<ComputeApi> compute_apis;
    std::optional<std::uint64_t> windows_luid;
};

struct InventoryOptions {
    // Vendor queries may wake a suspended discrete GPU.
    bool vendor_details = true;
};

[[nodiscard]] Result<std::vector<Device>> enumerate(InventoryOptions options = {});

} // namespace cyhwinfo::gpu
