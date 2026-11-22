#pragma once

#include <cyhwinfo/core.hpp>

#include <cstdint>
#include <vector>

namespace cyhwinfo::network {

struct Interface {
    std::uint32_t index;
    std::string name;
    std::optional<std::string> description;
    std::optional<std::string> mac;
    std::vector<std::string> ipv4;
    std::vector<std::string> ipv6;
    bool is_up;
    bool is_loopback;
};

[[nodiscard]] Result<std::vector<Interface>> enumerate();

} // namespace cyhwinfo::network
