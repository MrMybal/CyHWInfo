#include <cyhwinfo/motherboard.hpp>

#include "hwinfo_adapter.hpp"

#include <hwinfo/mainboard.h>

namespace cyhwinfo::motherboard {

Result<Inventory> inventory() {
    auto board = hwinfo::mainboard();
    if (!board) {
        return detail::upstream_error(board.error());
    }
    return Inventory{std::move(board->vendor), std::move(board->name), std::move(board->version),
                     std::move(board->serial_number), std::move(board->firmware_vendor),
                     std::move(board->firmware_version), board->firmware_date};
}

} // namespace cyhwinfo::motherboard
