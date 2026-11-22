#include <cyhwinfo/storage_health.hpp>

#ifdef _WIN32
#include <windows.h>
#include <winioctl.h>
#include <nvme.h>

#include <cstring>
#include <format>
#include <numeric>
#include <span>
#endif

namespace cyhwinfo::storage {
namespace {

#ifdef _WIN32
// NVMe log and ATA SMART reads adapted from CrystalDiskInfo, AtaSmart.cpp (MIT).
// Copyright (c) 2008-2023 hiyohiyo. See THIRD-PARTY-NOTICES.txt.
struct DiskHandle {
    HANDLE value;

    ~DiskHandle() {
        if (value != INVALID_HANDLE_VALUE) {
            CloseHandle(value);
        }
    }
};

std::unexpected<Error> windows_error(Source source, DWORD code, std::string message) {
    auto status = Status::error;
    if (code == ERROR_ACCESS_DENIED || code == ERROR_PRIVILEGE_NOT_HELD) {
        status = Status::permission_denied;
    } else if (code == ERROR_INVALID_FUNCTION || code == ERROR_NOT_SUPPORTED) {
        status = Status::unsupported;
    } else if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND || code == ERROR_DEVICE_NOT_CONNECTED) {
        status = Status::not_found;
    }
    return std::unexpected(Error{status, source, {static_cast<int>(code), std::system_category()}, std::move(message)});
}

Counter128 counter(const UCHAR (&bytes)[16]) {
    Counter128 result{};
    for (unsigned index = 0; index < 8; ++index) {
        result.low |= static_cast<std::uint64_t>(bytes[index]) << (index * 8);
        result.high |= static_cast<std::uint64_t>(bytes[index + 8]) << (index * 8);
    }
    return result;
}

Reading<double> celsius(unsigned kelvin) {
    if (kelvin == 0 || kelvin == 0xffff) {
        return {std::nullopt, Source::windows_nvme, Status::unavailable};
    }
    return Reading<double>::observed(kelvin - 273.15, Source::windows_nvme);
}

Result<NVME_HEALTH_INFO_LOG> read_nvme_log(HANDLE disk, DWORD request_subvalue) {
    constexpr auto source = Source::windows_nvme;
    constexpr auto query_offset = offsetof(STORAGE_PROPERTY_QUERY, AdditionalParameters);
    constexpr auto descriptor_offset = offsetof(STORAGE_PROTOCOL_DATA_DESCRIPTOR, ProtocolSpecificData);
    constexpr auto payload_offset = sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA);
    constexpr DWORD payload_size = 4096;
    std::array<std::byte, query_offset + payload_offset + payload_size> buffer{};
    const STORAGE_PROPERTY_QUERY query{StorageAdapterProtocolSpecificProperty, PropertyStandardQuery, {}};
    STORAGE_PROTOCOL_SPECIFIC_DATA protocol{};
    protocol.ProtocolType = ProtocolTypeNvme;
    protocol.DataType = NVMeDataTypeLogPage;
    protocol.ProtocolDataRequestValue = NVME_LOG_PAGE_HEALTH_INFO;
    protocol.ProtocolDataRequestSubValue = request_subvalue;
    protocol.ProtocolDataOffset = payload_offset;
    protocol.ProtocolDataLength = payload_size;
    std::memcpy(buffer.data(), &query, query_offset);
    std::memcpy(buffer.data() + query_offset, &protocol, sizeof(protocol));

    DWORD returned = 0;
    const auto buffer_size = static_cast<DWORD>(buffer.size());
    if (!DeviceIoControl(disk, IOCTL_STORAGE_QUERY_PROPERTY, buffer.data(), buffer_size,
                         buffer.data(), buffer_size, &returned, nullptr)) {
        return windows_error(source, GetLastError(), "Reading the NVMe SMART/health log failed");
    }
    STORAGE_PROTOCOL_DATA_DESCRIPTOR descriptor{};
    if (returned < sizeof(descriptor) || returned > buffer.size()) {
        return windows_error(source, ERROR_INVALID_DATA, "Truncated NVMe protocol descriptor");
    }
    std::memcpy(&descriptor, buffer.data(), sizeof(descriptor));
    const auto& response = descriptor.ProtocolSpecificData;
    const auto available = returned - descriptor_offset;
    if (descriptor.Version != sizeof(descriptor) || descriptor.Size != sizeof(descriptor) ||
        response.ProtocolType != ProtocolTypeNvme || response.DataType != NVMeDataTypeLogPage ||
        response.ProtocolDataOffset < payload_offset || response.ProtocolDataOffset > available ||
        response.ProtocolDataLength < sizeof(NVME_HEALTH_INFO_LOG) ||
        response.ProtocolDataLength > available - response.ProtocolDataOffset) {
        return windows_error(source, ERROR_INVALID_DATA, "Invalid NVMe health log descriptor");
    }
    NVME_HEALTH_INFO_LOG log{};
    std::memcpy(&log, buffer.data() + descriptor_offset + response.ProtocolDataOffset, sizeof(log));
    return log;
}

Result<Health> nvme_health(HANDLE disk) {
    auto log = read_nvme_log(disk, 0);
    if (!log && log.error().status != Status::permission_denied && log.error().status != Status::not_found) {
        log = read_nvme_log(disk, 0xffffffff);
    }
    if (!log) {
        return std::unexpected(std::move(log.error()));
    }
    const auto temperature = log->Temperature[0] | (static_cast<unsigned>(log->Temperature[1]) << 8);
    NvmeHealth detail{
        log->CriticalWarning.AsUchar, log->AvailableSpare, log->AvailableSpareThreshold, log->PercentageUsed,
        counter(log->DataUnitRead), counter(log->DataUnitWritten), counter(log->HostReadCommands),
        counter(log->HostWrittenCommands), counter(log->ControllerBusyTime), counter(log->PowerCycle),
        counter(log->PowerOnHours), counter(log->UnsafeShutdowns), counter(log->MediaErrors),
        counter(log->ErrorInfoLogEntryCount), log->WarningCompositeTemperatureTime,
        log->CriticalCompositeTemperatureTime,
        {celsius(log->TemperatureSensor1), celsius(log->TemperatureSensor2), celsius(log->TemperatureSensor3),
         celsius(log->TemperatureSensor4), celsius(log->TemperatureSensor5), celsius(log->TemperatureSensor6),
         celsius(log->TemperatureSensor7), celsius(log->TemperatureSensor8)},
    };
    return Health{std::chrono::system_clock::now(), Source::windows_nvme, celsius(temperature), detail, {}, {}};
}

Health ata_snapshot(std::span<const std::uint8_t> page, std::optional<bool> failure_predicted = {}) {
    constexpr auto source = Source::windows_ata;
    constexpr unsigned attribute_count = 30;
    constexpr unsigned attribute_size = 12;
    std::vector<AtaAttribute> attributes;
    if (page.size() == READ_ATTRIBUTE_BUFFER_SIZE) {
        for (unsigned index = 0; index < attribute_count; ++index) {
            const auto* field = page.data() + 2 + index * attribute_size;
            if (field[0] != 0 && field[0] != 0xff) {
                AtaAttribute attribute{
                    field[0], static_cast<std::uint16_t>(field[1] | (field[2] << 8)), field[3], field[4], {},
                };
                std::memcpy(attribute.raw.data(), field + 5, attribute.raw.size());
                attributes.push_back(attribute);
            }
        }
    }
    Reading<double> temperature{std::nullopt, source, Status::unavailable};
    for (const auto& attribute : attributes) {
        // Common SMART temperature attributes. Preserve the raw values for vendor-specific interpretation.
        if ((attribute.id == 194 || (attribute.id == 190 && !temperature.value)) &&
            attribute.raw[0] > 0 && attribute.raw[0] <= 125) {
            temperature = Reading<double>::observed(attribute.raw[0], source);
        }
    }
    return {std::chrono::system_clock::now(), source, temperature, std::nullopt, std::move(attributes), failure_predicted};
}

Result<Health> ata_prediction(HANDLE disk) {
    constexpr auto source = Source::windows_ata;
    STORAGE_PREDICT_FAILURE response{};
    DWORD returned = 0;
    if (!DeviceIoControl(disk, IOCTL_STORAGE_PREDICT_FAILURE, nullptr, 0, &response, sizeof(response),
                         &returned, nullptr)) {
        return windows_error(source, GetLastError(), "Reading storage failure prediction failed");
    }
    if (returned != sizeof(response)) {
        return windows_error(source, ERROR_INVALID_DATA, "Truncated storage failure prediction");
    }
    const std::span<const std::uint8_t> page(response.VendorSpecific);
    const unsigned revision = page[0] | (page[1] << 8);
    const unsigned checksum = std::accumulate(page.begin(), page.end(), 0u) & 0xff;
    // The IOCTL also allows vendor-specific data. Decode only a valid ATA SMART page.
    const bool ata_page = revision != 0 && revision != 0xffff && checksum == 0;
    return ata_snapshot(ata_page ? page : std::span<const std::uint8_t>{}, response.PredictFailure != 0);
}

Result<Health> ata_health(HANDLE disk, std::uint32_t disk_index) {
    constexpr auto source = Source::windows_ata;
    constexpr auto header_size = offsetof(SENDCMDOUTPARAMS, bBuffer);
    std::array<std::uint8_t, header_size + READ_ATTRIBUTE_BUFFER_SIZE> buffer{};
    SENDCMDINPARAMS request{};
    request.cBufferSize = READ_ATTRIBUTE_BUFFER_SIZE;
    request.bDriveNumber = static_cast<BYTE>(disk_index);
    request.irDriveRegs.bCommandReg = SMART_CMD;
    request.irDriveRegs.bFeaturesReg = READ_ATTRIBUTES;
    request.irDriveRegs.bSectorCountReg = 1;
    request.irDriveRegs.bSectorNumberReg = 1;
    request.irDriveRegs.bDriveHeadReg = 0xa0;
    request.irDriveRegs.bCylLowReg = SMART_CYL_LOW;
    request.irDriveRegs.bCylHighReg = SMART_CYL_HI;

    DWORD returned = 0;
    if (!DeviceIoControl(disk, SMART_RCV_DRIVE_DATA, &request, offsetof(SENDCMDINPARAMS, bBuffer),
                         buffer.data(), static_cast<DWORD>(buffer.size()), &returned, nullptr)) {
        return windows_error(source, GetLastError(), "Reading ATA SMART attributes failed");
    }
    if (returned != buffer.size()) {
        return windows_error(source, ERROR_INVALID_DATA, "Truncated ATA SMART response");
    }
    SENDCMDOUTPARAMS descriptor{};
    std::memcpy(&descriptor, buffer.data(), header_size);
    if (descriptor.DriverStatus.bDriverError || descriptor.cBufferSize < READ_ATTRIBUTE_BUFFER_SIZE) {
        return windows_error(source, ERROR_INVALID_DATA, "Invalid ATA SMART response");
    }

    return ata_snapshot(std::span(buffer).subspan(header_size));
}

Result<Health> sata_health(const std::wstring& path, HANDLE disk, std::uint32_t disk_index) {
    auto prediction = ata_prediction(disk);
    if (prediction && !prediction->ata_attributes.empty()) {
        return prediction;
    }
    if (disk_index <= 255) {
        // The direct read IOCTL requires both access flags, even for read-only SMART commands.
        const DiskHandle direct{CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        if (direct.value != INVALID_HANDLE_VALUE) {
            auto attributes = ata_health(direct.value, disk_index);
            if (attributes) {
                if (prediction) {
                    attributes->ata_failure_predicted = prediction->ata_failure_predicted;
                }
                return attributes;
            }
            if (!prediction) {
                return attributes;
            }
        } else if (!prediction && prediction.error().status == Status::unsupported) {
            return windows_error(Source::windows_ata, GetLastError(), "Opening the disk for direct ATA SMART failed");
        }
    }
    return prediction;
}
#endif

} // namespace

Result<Health> health(const Disk& disk) {
#ifdef _WIN32
    const bool nvme = disk.bus == Bus::nvme;
    const bool ata = disk.bus == Bus::sata;
    if (nvme || ata) {
        const auto source = nvme ? Source::windows_nvme : Source::windows_ata;
        const auto path = std::format(LR"(\\.\PhysicalDrive{})", disk.index);
        const DiskHandle handle{CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        if (handle.value == INVALID_HANDLE_VALUE) {
            return windows_error(source, GetLastError(), "Opening the physical disk failed");
        }
        return nvme ? nvme_health(handle.value) : sata_health(path, handle.value, disk.index);
    }
#else
    (void) disk;
#endif
    return std::unexpected(Error{Status::unsupported, Source::unknown, {},
                                 "SMART health requires a supported NVMe or SATA disk on Windows"});
}

} // namespace cyhwinfo::storage
