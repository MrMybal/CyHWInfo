#pragma once

#include <cyhwinfo/core.hpp>
#include <hwinfo/error.h>

#include <cstdint>

namespace cyhwinfo::detail {

inline std::unexpected<Error> upstream_error(const hwinfo::error& error) {
    auto status = Status::error;
    if (error == hwinfo::errc::not_supported) {
        status = Status::unsupported;
    } else if (error == hwinfo::errc::permission_denied) {
        status = Status::permission_denied;
    } else if (error == hwinfo::errc::not_found) {
        status = Status::not_found;
    }
    return std::unexpected(Error{status, Source::hwinfo, error.code(), error.message()});
}

template<class Quantity>
std::optional<std::uint64_t> quantity_value(const std::optional<Quantity>& quantity) {
    return quantity.transform([](const auto& value) { return value.value; });
}

} // namespace cyhwinfo::detail
