#include <cyhwinfo/storage.hpp>

#include "hwinfo_adapter.hpp"

#include <hwinfo/disk.h>
#include <hwinfo/monitoring.h>

namespace cyhwinfo::storage {
namespace {

Bus bus_of(hwinfo::DiskBus bus) {
    switch (bus) {
    case hwinfo::DiskBus::nvme: return Bus::nvme;
    case hwinfo::DiskBus::sata: return Bus::sata;
    case hwinfo::DiskBus::scsi: return Bus::scsi;
    case hwinfo::DiskBus::usb: return Bus::usb;
    case hwinfo::DiskBus::mmc: return Bus::mmc;
    case hwinfo::DiskBus::virtio: return Bus::virtio;
    case hwinfo::DiskBus::unknown: return Bus::unknown;
    }
    return Bus::unknown;
}

} // namespace

Result<std::vector<Disk>> enumerate() {
    auto disks = hwinfo::disks();
    if (!disks) {
        return detail::upstream_error(disks.error());
    }
    std::vector<Disk> result;
    result.reserve(disks->size());
    for (auto& disk : *disks) {
        std::vector<MountPoint> mounts;
        mounts.reserve(disk.mount_points.size());
        for (auto& mount : disk.mount_points) {
            mounts.push_back({std::move(mount.path), std::move(mount.filesystem), std::move(mount.source),
                              std::move(mount.volume), std::move(mount.space_id), mount.encrypted});
        }
        result.push_back({disk.index, std::move(disk.vendor), std::move(disk.model), std::move(disk.serial_number),
                          disk.size.value, bus_of(disk.bus), detail::quantity_value(disk.link_speed),
                          std::move(mounts)});
    }
    return result;
}

Result<Space> space(const std::filesystem::path& path) {
    const auto result = hwinfo::disk_space(path);
    if (!result) {
        return detail::upstream_error(result.error());
    }
    return Space{result->capacity.value, result->free.value, result->available.value};
}

} // namespace cyhwinfo::storage
