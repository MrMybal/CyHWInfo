#pragma once

#include <cyhwinfo/core.hpp>

#include <cstdint>
#include <filesystem>
#include <vector>

namespace cyhwinfo::storage {

enum class Bus { unknown, nvme, sata, scsi, usb, mmc, virtio };

struct MountPoint {
    std::filesystem::path path;
    std::optional<std::string> filesystem;
    std::optional<std::string> source;
    std::optional<std::string> volume;
    std::optional<std::string> space_id;
    std::optional<bool> encrypted;
};

struct Disk {
    // On Windows this is the PhysicalDrive number and may change after reconnecting.
    std::uint32_t index;
    std::optional<std::string> vendor;
    std::optional<std::string> model;
    std::optional<std::string> serial_number;
    std::uint64_t size_bytes;
    Bus bus;
    std::optional<std::uint64_t> link_speed_bits_per_second;
    std::vector<MountPoint> mount_points;
};

struct Space {
    std::uint64_t capacity_bytes;
    std::uint64_t free_bytes;
    std::uint64_t available_bytes;
};

[[nodiscard]] Result<std::vector<Disk>> enumerate();
[[nodiscard]] Result<Space> space(const std::filesystem::path& path);

} // namespace cyhwinfo::storage
