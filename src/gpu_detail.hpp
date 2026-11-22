#pragma once

#include <hwinfo/gpu.h>
#include <format>

namespace cyhwinfo::gpu::detail {

inline std::string id_of(const hwinfo::Gpu& adapter) {
    if (adapter.luid) {
        return std::format("luid:{:016x}", *adapter.luid);
    }
    if (adapter.pci && adapter.pci->address) {
        return "pci:" + *adapter.pci->address;
    }
    if (adapter.uuid) {
        return "uuid:" + hwinfo::to_string(*adapter.uuid);
    }
    return std::format("index:{}", adapter.index);
}

} // namespace cyhwinfo::gpu::detail
