#include <cyhwinfo/network.hpp>

#include "hwinfo_adapter.hpp"

#include <hwinfo/network.h>

namespace cyhwinfo::network {

Result<std::vector<Interface>> enumerate() {
    auto interfaces = hwinfo::network_interfaces();
    if (!interfaces) {
        return detail::upstream_error(interfaces.error());
    }
    std::vector<Interface> result;
    result.reserve(interfaces->size());
    for (auto& nic : *interfaces) {
        result.push_back({nic.index, std::move(nic.name), std::move(nic.description), std::move(nic.mac),
                          std::move(nic.ipv4), std::move(nic.ipv6), nic.is_up, nic.is_loopback});
    }
    return result;
}

} // namespace cyhwinfo::network
