#include <cyhwinfo/ui.hpp>
#include "sensor_values.hpp"

#ifdef CYHWINFO_UI_HAS_CPU
#include <cyhwinfo/cpu.hpp>
#endif
#ifdef CYHWINFO_UI_HAS_GPU
#include <cyhwinfo/gpu.hpp>
#endif
#ifdef CYHWINFO_UI_HAS_GPU_TELEMETRY
#include <cyhwinfo/gpu_telemetry.hpp>
#endif
#ifdef CYHWINFO_UI_HAS_GPU_PROCESSES
#include <cyhwinfo/gpu_processes.hpp>
#endif
#ifdef CYHWINFO_UI_HAS_MEMORY
#include <cyhwinfo/memory.hpp>
#endif
#ifdef CYHWINFO_UI_HAS_STORAGE
#include <cyhwinfo/storage.hpp>
#endif
#ifdef CYHWINFO_UI_HAS_STORAGE_HEALTH
#include <cyhwinfo/storage_health.hpp>
#endif
#ifdef CYHWINFO_UI_HAS_MOTHERBOARD
#include <cyhwinfo/motherboard.hpp>
#endif
#ifdef CYHWINFO_UI_HAS_NETWORK
#include <cyhwinfo/network.hpp>
#endif
#ifdef CYHWINFO_UI_HAS_BATTERY
#include <cyhwinfo/battery.hpp>
#endif
#ifdef CYHWINFO_UI_HAS_SENSORS
#include <cyhwinfo/sensors.hpp>
#endif

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <format>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace cyhwinfo::ui {
namespace {

using namespace std::chrono_literals;
constexpr std::size_t history_limit = 90;
constexpr const char* absent = "—";
constexpr double gibibyte = 1024.0 * 1024 * 1024;
using Metric = SensorMetric;

[[maybe_unused]] Section& section(Snapshot& snapshot, Page page) {
    return snapshot.sections[static_cast<std::size_t>(page)];
}

[[maybe_unused]] std::optional<double>& trend(Snapshot& snapshot, Summary metric) {
    return snapshot.trends[static_cast<std::size_t>(metric)].value;
}

[[maybe_unused]] void maximum(std::optional<double>& current, std::optional<double> value) {
    if (value && std::isfinite(*value) && (!current || *value > *current)) {
        current = value;
    }
}

[[maybe_unused]] Device& device(Section& items, std::string name, std::string description = {},
                               bool translate_name = false) {
    return items.devices.emplace_back(Device{std::move(name), std::move(description), {}, {}, translate_name});
}

[[maybe_unused]] void field(Device& item, std::string label, std::string value, bool translate = false) {
    if (!value.empty()) {
        item.properties.push_back({std::move(label), std::move(value), translate});
    }
}

[[maybe_unused]] void property(Device& item, std::string label, const std::optional<std::string>& value) {
    field(item, std::move(label), value.value_or(""));
}

[[maybe_unused]] void notice(Section& items, std::string title, const Error& error) {
    items.notices.push_back({std::move(title), error.message, error.status});
}

[[maybe_unused]] std::string join(const std::vector<std::string>& text) {
    std::string result;
    for (const auto& part : text) {
        if (!result.empty()) {
            result += ", ";
        }
        result += part;
    }
    return result;
}

[[maybe_unused]] std::string bytes(std::uint64_t value) {
    constexpr double gib = 1024.0 * 1024 * 1024;
    if (value >= gib) {
        return std::format("{:.2f} GiB", value / gib);
    }
    if (value >= 1024 * 1024) {
        return std::format("{:.1f} MiB", value / (1024.0 * 1024));
    }
    if (value >= 1024) {
        return std::format("{:.1f} KiB", value / 1024.0);
    }
    return std::format("{} B", value);
}

[[maybe_unused]] std::string bytes(const std::optional<std::uint64_t>& value) {
    return value ? bytes(*value) : absent;
}

[[maybe_unused]] std::string frequency(std::uint64_t hz) {
    return hz >= 1'000'000'000 ? std::format("{:.2f} GHz", hz / 1e9) : std::format("{:.0f} MHz", hz / 1e6);
}

[[maybe_unused]] std::string percent(std::optional<double> value) {
    return value ? std::format("{:.1f}%", *value) : absent;
}

[[maybe_unused]] std::string measure(std::optional<double> value, std::string_view unit, int decimals = 1) {
    return value ? std::format("{:.{}f} {}", *value, decimals, unit) : absent;
}

[[maybe_unused]] SensorGroup& sensor_group(Snapshot& snapshot, Page category, std::string id, std::string name,
                                           bool translate = false) {
    auto& groups = snapshot.sensor_groups;
    const auto found = std::ranges::find(groups, id, &SensorGroup::id);
    if (found != groups.end()) {
        return *found;
    }
    return groups.emplace_back(SensorGroup{category, std::move(id), std::move(name), {}, translate});
}

[[maybe_unused]] Reading<double> native_reading(std::optional<double> value, Source source = Source::hwinfo) {
    return {value, source, value ? Status::available : Status::unavailable};
}

template<class T>
Reading<double> scaled(const Reading<T>& reading, double divisor) {
    return {reading.value.transform([divisor](T value) { return static_cast<double>(value) / divisor; }),
            reading.source, reading.status};
}

[[maybe_unused]] void remember(SensorGroup& group, std::string id, std::string label, Metric metric,
                               Reading<double> reading, Timestamp observed_at, bool translate = true) {
    group.rows.push_back({std::move(id), std::move(label), metric, reading, observed_at, {}, translate});
}

#ifdef CYHWINFO_UI_HAS_STORAGE
std::string path_text(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return {utf8.begin(), utf8.end()};
}

std::string bus_name(storage::Bus bus) {
    switch (bus) {
    case storage::Bus::nvme: return "NVMe";
    case storage::Bus::sata: return "SATA";
    case storage::Bus::scsi: return "SCSI";
    case storage::Bus::usb: return "USB";
    case storage::Bus::mmc: return "MMC";
    case storage::Bus::virtio: return "VirtIO";
    case storage::Bus::unknown: return absent;
    }
    return absent;
}
#endif

#ifdef CYHWINFO_UI_HAS_STORAGE_HEALTH
std::string count(storage::Counter128 value) {
    return value.high ? std::format("0x{:x}{:016x}", value.high, value.low) : std::to_string(value.low);
}

void storage_health(Snapshot& snapshot, Device& item, Section& items, const storage::Disk& disk) {
    const auto health = storage::health(disk);
    if (!health) {
        if (health.error().status != Status::unsupported) {
            notice(items, "SMART read failed", health.error());
        }
        return;
    }
    field(item, "Temperature", measure(health->temperature_celsius.value, "°C"));
    auto& sensors = sensor_group(snapshot, Page::storage,
        std::format("storage:{}:{}", disk.index, disk.serial_number.value_or(item.name)), item.name);
    remember(sensors, "temperature", "Temperature", Metric::temperature_celsius,
             health->temperature_celsius, health->timestamp);
    if (health->ata_failure_predicted) {
        field(item, "Failure predicted", *health->ata_failure_predicted ? "Yes" : "No", true);
    }
    if (health->nvme) {
        const auto& data = *health->nvme;
        field(item, "Life used", std::format("{}%", data.percentage_used));
        field(item, "Available spare", std::format("{}%", data.available_spare_percent));
        field(item, "Power-on hours", count(data.power_on_hours));
        field(item, "Power cycles", count(data.power_cycles));
        field(item, "Unsafe shutdowns", count(data.unsafe_shutdowns));
        field(item, "Media errors", count(data.media_errors));
        field(item, "Critical warning flags", std::format("0x{:02x}", data.critical_warning_flags));
        field(item, "Data units read", count(data.data_units_read));
        field(item, "Data units written", count(data.data_units_written));
        remember(sensors, "wear", "Life used", Metric::level_percent,
                 native_reading(data.percentage_used, health->source), health->timestamp);
        remember(sensors, "spare", "Available spare", Metric::level_percent,
                 native_reading(data.available_spare_percent, health->source), health->timestamp);
        Table temperatures{"NVMe temperature sensors", {"Sensor", "Temperature"}, {}};
        for (std::size_t index = 0; index < data.temperature_sensors_celsius.size(); ++index) {
            const auto& reading = data.temperature_sensors_celsius[index];
            if (reading.value) {
                temperatures.rows.push_back({std::to_string(index + 1), measure(reading.value, "°C")});
                remember(sensors, std::format("temperature-{}", index),
                         std::format("NVMe #{}", index + 1), Metric::temperature_celsius, reading,
                         health->timestamp, false);
            }
        }
        if (!temperatures.rows.empty()) {
            item.tables.push_back(std::move(temperatures));
        }
    }
    if (!health->ata_attributes.empty()) {
        Table smart{"ATA SMART attributes", {"ID", "Normalized", "Worst", "Raw bytes"}, {}};
        for (const auto& attribute : health->ata_attributes) {
            std::string raw;
            for (const auto value : attribute.raw) {
                if (!raw.empty()) {
                    raw += ' ';
                }
                raw += std::format("{:02X}", value);
            }
            smart.rows.push_back({std::format("{:02X}", attribute.id), std::to_string(attribute.normalized),
                                  std::to_string(attribute.worst), std::move(raw)});
        }
        item.tables.push_back(std::move(smart));
    }
}
#endif

struct Collection {
    explicit Collection(MonitorOptions settings) : options(settings) {}
    MonitorOptions options;
    Snapshot inventory;

#ifdef CYHWINFO_UI_HAS_CPU
    cpu::Sampler cpu_sampler;
#endif
#ifdef CYHWINFO_UI_HAS_GPU
    struct Adapter {
        std::string id;
#ifdef CYHWINFO_UI_HAS_GPU_TELEMETRY
        std::optional<gpu::Sampler> telemetry;
#endif
#ifdef CYHWINFO_UI_HAS_GPU_PROCESSES
        std::optional<gpu::ProcessSampler> processes;
#endif
    };

    std::vector<Adapter> adapters;
#endif
#ifdef CYHWINFO_UI_HAS_BATTERY
    std::vector<std::uint32_t> batteries;
#endif
#ifdef CYHWINFO_UI_HAS_SENSORS
    std::optional<sensors::Sampler> sensors_sampler;
    std::optional<Error> sensor_error;
#endif

    void scan();
    Snapshot sample();
};

void Collection::scan() {
    inventory = {};
    section(inventory, Page::overview).enabled = true;
    section(inventory, Page::sensors).enabled = true;
#ifdef CYHWINFO_UI_HAS_CPU
    auto& processors = section(inventory, Page::cpu);
    processors.enabled = true;
    if (auto packages = cpu::enumerate()) {
        for (const auto& package : *packages) {
            auto& item = device(processors, package.model.empty() ? "CPU" : package.model, package.vendor);
            field(item, "Socket", std::to_string(package.socket));
            field(item, "Physical cores", std::to_string(package.physical_cores));
            field(item, "Logical processors", std::to_string(package.logical_cores));
            field(item, "Instruction sets", join(package.flags));
            Table topology{"Core topology", {"Core", "Logical processors", "Base clock", "Max clock", "L2 cache"}, {}};
            for (const auto& core : package.cores) {
                std::vector<std::string> logical;
                for (const auto index : core.logical_ids) {
                    logical.push_back(std::to_string(index));
                }
                topology.rows.push_back({std::to_string(core.id), join(logical),
                    core.base_frequency_hz ? frequency(*core.base_frequency_hz) : absent,
                    core.max_frequency_hz ? frequency(*core.max_frequency_hz) : absent, bytes(core.cache.l2_bytes)});
            }
            if (!topology.rows.empty()) {
                item.tables.push_back(std::move(topology));
            }
        }
    } else {
        notice(processors, "CPU inventory unavailable", packages.error());
    }
#endif
#ifdef CYHWINFO_UI_HAS_GPU
    auto& graphics = section(inventory, Page::gpu);
    graphics.enabled = true;
    if (auto devices = gpu::enumerate()) {
        auto previous = std::move(adapters);
        adapters.clear();
        adapters.reserve(devices->size());
        for (const auto& gpu : *devices) {
            auto& item = device(graphics, gpu.name.empty() ? "GPU" : gpu.name, gpu.vendor);
            field(item, "Adapter ID", gpu.id);
            property(item, "Architecture", gpu.architecture);
            property(item, "Driver", gpu.driver);
            property(item, "Driver version", gpu.driver_version);
            property(item, "Video BIOS", gpu.vbios_version);
            property(item, "Memory type", gpu.memory_type);
            if (gpu.dedicated_memory_bytes) {
                field(item, "Dedicated memory", bytes(*gpu.dedicated_memory_bytes));
            }
            if (gpu.shared_memory_bytes) {
                field(item, "Shared memory", bytes(*gpu.shared_memory_bytes));
            }
            if (gpu.pci) {
                property(item, "PCI address", gpu.pci->address);
                if (gpu.pci->max_link) {
                    field(item, "Maximum PCIe link", std::format("Gen {} ×{}", gpu.pci->max_link->generation,
                                                               gpu.pci->max_link->lanes));
                }
            }

            auto existing = std::ranges::find(previous, gpu.id, &Adapter::id);
            Adapter adapter{};
            if (existing != previous.end()) {
                adapter = std::move(*existing);
            } else {
                adapter.id = gpu.id;
            }
#ifdef CYHWINFO_UI_HAS_GPU_TELEMETRY
            if (!adapter.telemetry) {
                auto sampler = gpu::Sampler::open(gpu.id);
                if (sampler) {
                    adapter.telemetry.emplace(std::move(*sampler));
                } else {
                    notice(graphics, "GPU telemetry unavailable", sampler.error());
                }
            }
#endif
#ifdef CYHWINFO_UI_HAS_GPU_PROCESSES
            if (options.gpu_processes && !adapter.processes) {
                auto processes = gpu::ProcessSampler::open(gpu);
                if (processes) {
                    adapter.processes.emplace(std::move(*processes));
                }
            }
#endif
            adapters.push_back(std::move(adapter));
        }
    } else {
        adapters.clear();
        notice(graphics, "GPU inventory unavailable", devices.error());
    }
#endif
#ifdef CYHWINFO_UI_HAS_MEMORY
    auto& ram = section(inventory, Page::memory);
    ram.enabled = true;
    auto& ram_total = device(ram, "Physical memory", {}, true);
    if (auto result = memory::inventory()) {
        field(ram_total, "Total", bytes(result->total_bytes));
        if (result->slots) {
            field(ram_total, "Slots", std::to_string(*result->slots));
        }
        if (result->max_capacity_bytes) {
            field(ram_total, "Maximum capacity", bytes(*result->max_capacity_bytes));
        }
        if (result->modules) {
            for (const auto& module : *result->modules) {
                auto& item = device(ram, module.location.value_or(std::format("DIMM {}", module.index + 1)),
                                    module.model.value_or(""));
                property(item, "Manufacturer", module.vendor);
                property(item, "Serial number", module.serial_number);
                property(item, "Memory type", module.type);
                property(item, "Bank", module.bank);
                field(item, "Capacity", bytes(module.size_bytes));
                if (module.configured_rate_mt_per_second) {
                    field(item, "Configured rate", std::format("{} MT/s", *module.configured_rate_mt_per_second));
                }
                if (module.rated_rate_mt_per_second) {
                    field(item, "Rated rate", std::format("{} MT/s", *module.rated_rate_mt_per_second));
                }
                if (module.voltage_millivolts) {
                    field(item, "Voltage", std::format("{} mV", *module.voltage_millivolts));
                }
            }
        }
    } else {
        notice(ram, "Memory inventory unavailable", result.error());
    }
#endif
#ifdef CYHWINFO_UI_HAS_STORAGE
    auto& disks = section(inventory, Page::storage);
    disks.enabled = true;
    if (auto result = storage::enumerate()) {
        for (const auto& disk : *result) {
            auto& item = device(disks, disk.model.value_or(std::format("Disk {}", disk.index)), bus_name(disk.bus));
            field(item, "Disk index", std::to_string(disk.index));
            property(item, "Manufacturer", disk.vendor);
            property(item, "Serial number", disk.serial_number);
            field(item, "Capacity", bytes(disk.size_bytes));
            if (!disk.mount_points.empty()) {
                Table mounts{"Volumes", {"Mount point", "Filesystem", "Capacity", "Free"}, {}};
                for (const auto& mount : disk.mount_points) {
                    const auto space = storage::space(mount.path);
                    mounts.rows.push_back({path_text(mount.path), mount.filesystem.value_or(absent),
                        space ? bytes(space->capacity_bytes) : absent, space ? bytes(space->free_bytes) : absent});
                }
                item.tables.push_back(std::move(mounts));
            }
#ifdef CYHWINFO_UI_HAS_STORAGE_HEALTH
            if (options.storage_health) {
                storage_health(inventory, item, disks, disk);
            }
#endif
        }
    } else {
        notice(disks, "Disk inventory unavailable", result.error());
    }
#endif
#ifdef CYHWINFO_UI_HAS_MOTHERBOARD
    auto& board = section(inventory, Page::motherboard);
    board.enabled = true;
    if (auto result = motherboard::inventory()) {
        auto& item = device(board, result->name.value_or("Motherboard"), result->vendor.value_or(""), !result->name);
        property(item, "Board version", result->version);
        property(item, "Serial number", result->serial_number);
        property(item, "Firmware vendor", result->firmware_vendor);
        property(item, "Firmware version", result->firmware_version);
        if (result->firmware_date && result->firmware_date->ok()) {
            field(item, "Firmware date", std::format("{:%F}", *result->firmware_date));
        }
    } else {
        notice(board, "Motherboard inventory unavailable", result.error());
    }
#endif
#ifdef CYHWINFO_UI_HAS_NETWORK
    auto& links = section(inventory, Page::network);
    links.enabled = true;
    if (auto result = network::enumerate()) {
        std::ranges::stable_sort(*result, [](const auto& left, const auto& right) {
            return std::pair{left.is_loopback, !left.is_up} < std::pair{right.is_loopback, !right.is_up};
        });
        for (const auto& link : *result) {
            auto& item = device(links, link.description.value_or(link.name));
            field(item, "Interface", link.name);
            field(item, "Link state", link.is_up ? "Up" : "Down", true);
            field(item, "Loopback", link.is_loopback ? "Yes" : "No", true);
            property(item, "MAC address", link.mac);
            field(item, "IPv4 addresses", join(link.ipv4));
            field(item, "IPv6 addresses", join(link.ipv6));
        }
    } else {
        notice(links, "Network inventory unavailable", result.error());
    }
#endif
#ifdef CYHWINFO_UI_HAS_BATTERY
    auto& power = section(inventory, Page::battery);
    power.enabled = true;
    batteries.clear();
    if (auto result = battery::enumerate()) {
        for (const auto& battery : *result) {
            batteries.push_back(battery.index);
            auto& item = device(power, battery.model.value_or(std::format("Battery {}", battery.index + 1)),
                                battery.vendor.value_or(""));
            property(item, "Technology", battery.technology);
            property(item, "Serial number", battery.serial_number);
            if (battery.design_capacity_microwatt_hours) {
                field(item, "Design capacity", std::format("{:.2f} Wh", *battery.design_capacity_microwatt_hours / 1e6));
            }
            if (battery.full_charge_capacity_microwatt_hours) {
                field(item, "Full-charge capacity", std::format("{:.2f} Wh",
                                                               *battery.full_charge_capacity_microwatt_hours / 1e6));
            }
        }
    } else {
        notice(power, "Battery inventory unavailable", result.error());
    }
#endif
#ifdef CYHWINFO_UI_HAS_SENSORS
    if (options.advanced_sensors && !sensors_sampler) {
        auto sampler = sensors::Sampler::open({.memory = true});
        if (sampler) {
            sensors_sampler.emplace(std::move(*sampler));
            sensor_error.reset();
        } else {
            sensor_error = std::move(sampler.error());
        }
    }
#endif
}

#ifdef CYHWINFO_UI_HAS_SENSORS
void append_sensors(Snapshot& snapshot, Page page, const std::optional<sensors::GroupSample>& group, Timestamp stamp) {
    auto& area = section(snapshot, page);
    if (!group) {
        return;
    }
    if (group->error) {
        area.enabled = true;
        notice(area, "Sensor read failed", *group->error);
        if (page == Page::memory) {
            notice(section(snapshot, Page::sensors), "Sensor read failed", *group->error);
        }
    }
    if (group->sensors.empty()) {
        return;
    }
    area.enabled = true;
    auto& item = device(area, "Hardware sensors", {}, true);
    Table readings{"", {"Device", "Sensor", "Value", "Source"}, {}};
    for (const auto& sensor : group->sensors) {
        const bool translate = sensor.reading.source == Source::amd_smu || sensor.reading.source == Source::amd_umc ||
                               sensor.reading.source == Source::amd_thermal_controller;
        auto& item_sensors = sensor_group(snapshot, page,
            std::format("lhm:{}:{}", static_cast<int>(page), sensor.device_id), sensor.device_name, translate);
        remember(item_sensors, sensor.id, sensor.name, sensor.metric, sensor.reading, stamp, translate);
        readings.rows.push_back({sensor.device_name, sensor.name, detail::sensor_text(sensor.metric, sensor.reading.value),
                                 detail::source_name(sensor.reading.source)});
    }
    item.tables.push_back(std::move(readings));
}
#endif

#ifdef CYHWINFO_UI_HAS_BATTERY
const char* charge_state(battery::State state) {
    switch (state) {
    case battery::State::charging: return "Charging";
    case battery::State::discharging: return "Discharging";
    case battery::State::full: return "Full";
    case battery::State::not_charging: return "Not charging";
    case battery::State::unknown: return "Unknown";
    }
    return "Unknown";
}
#endif

Snapshot Collection::sample() {
    auto result = inventory;
#if defined(CYHWINFO_UI_HAS_CPU)
    const auto clocks_stamp = std::chrono::system_clock::now();
#endif
#ifdef CYHWINFO_UI_HAS_CPU
    auto load = cpu_sampler.sample();
    auto clocks = cpu::frequencies_hz();
    if (load) {
        trend(result, Summary::cpu) = load->total_percent;
    }
    auto& area = section(result, Page::cpu);
    if (load || clocks) {
        auto& item = device(area, "Live CPU", {}, true);
        field(item, "Utilization", percent(trend(result, Summary::cpu)));
        auto& native = sensor_group(result, Page::cpu, "cpu:os", "System measurements", true);
        remember(native, "total", "CPU load", Metric::load_percent, native_reading(trend(result, Summary::cpu)),
                 load ? load->timestamp : clocks_stamp);
        auto& threads = sensor_group(result, Page::cpu, "cpu:logical", "Logical processors", true);
        const auto load_count = load ? load->logical_percent.size() : 0;
        const auto clock_count = clocks ? clocks->size() : 0;
        Table logical{"Logical processors", {"Processor", "Utilization", "Clock"}, {}};
        for (std::size_t i = 0; i < std::max(load_count, clock_count); ++i) {
            logical.rows.push_back({std::to_string(i), i < load_count ? percent(load->logical_percent[i]) : absent,
                                    i < clock_count && (*clocks)[i] ? frequency((*clocks)[i]) : absent});
            if (i < load_count) {
                remember(threads, std::format("load:{}", i), std::format("CPU #{} · %", i),
                         Metric::load_percent, native_reading(load->logical_percent[i]), load->timestamp, false);
            }
            if (i < clock_count && (*clocks)[i] != 0) {
                remember(threads, std::format("clock:{}", i), std::format("CPU #{} · MHz", i),
                         Metric::clock_megahertz, native_reading((*clocks)[i] / 1e6), clocks_stamp, false);
            }
        }
        item.tables.push_back(std::move(logical));
    } else {
        notice(area, "CPU telemetry unavailable", load.error());
    }
#endif
#ifdef CYHWINFO_UI_HAS_GPU
    auto& graphics = section(result, Page::gpu);
    for (std::size_t i = 0; i < adapters.size(); ++i) {
        auto& adapter = adapters[i];
        auto& item = graphics.devices[i];
#ifdef CYHWINFO_UI_HAS_GPU_TELEMETRY
        if (adapter.telemetry) {
            const auto data = adapter.telemetry->sample();
            if (data) {
                auto& group = sensor_group(result, Page::gpu, "gpu:" + adapter.id, item.name);
                const auto stamp = data->timestamp;
                remember(group, "load", "GPU load", Metric::load_percent, data->utilization_percent, stamp);
                remember(group, "temperature", "Temperature", Metric::temperature_celsius, data->temperature_celsius, stamp);
                remember(group, "power", "Power", Metric::power_watts, data->power_watts, stamp);
                remember(group, "vram", "VRAM in use", Metric::data_gibibytes,
                         scaled(data->dedicated_memory_used_bytes, gibibyte), stamp);
                remember(group, "memory-load", "Memory utilization", Metric::load_percent, data->memory_utilization_percent, stamp);
                remember(group, "video-load", "Video utilization", Metric::load_percent, data->video_utilization_percent, stamp);
                remember(group, "fan-duty", "Fan duty", Metric::control_percent, data->fan_percent, stamp);
                remember(group, "clock", "Core clock", Metric::clock_megahertz, scaled(data->frequency_hz, 1e6), stamp);
                remember(group, "memory-clock", "Memory clock", Metric::clock_megahertz,
                         scaled(data->memory_frequency_hz, 1e6), stamp);
                maximum(trend(result, Summary::gpu), data->utilization_percent.value);
                maximum(trend(result, Summary::temperature), data->temperature_celsius.value);
                field(item, "Utilization", percent(data->utilization_percent.value));
                field(item, "Temperature", measure(data->temperature_celsius.value, "°C"));
                field(item, "Power", measure(data->power_watts.value, "W"));
                field(item, "VRAM in use", bytes(data->dedicated_memory_used_bytes.value));
                field(item, "Memory utilization", percent(data->memory_utilization_percent.value));
                field(item, "Video utilization", percent(data->video_utilization_percent.value));
                field(item, "Fan duty", percent(data->fan_percent.value));
                if (data->frequency_hz.value) {
                    field(item, "Core clock", frequency(*data->frequency_hz.value));
                }
                if (data->memory_frequency_hz.value) {
                    field(item, "Memory clock", frequency(*data->memory_frequency_hz.value));
                }
                if (data->pcie_link.value) {
                    field(item, "Current PCIe link", std::format("Gen {} ×{}", data->pcie_link.value->generation,
                                                               data->pcie_link.value->lanes));
                }
                if (data->suspended) {
                    field(item, "Adapter state", "Suspended", true);
                }
                Table sensors{"Physical adapter sensors", {"Adapter", "Temperature", "Fan", "Power limit %", "Memory clock"}, {}};
                for (const auto& physical : data->physical_adapters) {
                    const auto prefix = std::format("physical:{}", physical.physical_index);
                    const auto suffix = std::format(" #{}", physical.physical_index + 1);
                    remember(group, prefix + ":fan", "Fan" + (data->physical_adapters.size() > 1 ? suffix : ""),
                             Metric::fan_rpm, scaled(physical.fan_rpm, 1), stamp, data->physical_adapters.size() == 1);
                    remember(group, prefix + ":power-limit", "Power limit %",
                             Metric::level_percent, physical.power_percent_of_limit, stamp);
                    if (data->physical_adapters.size() > 1 || data->temperature_celsius.source != Source::windows_d3dkmt) {
                        remember(group, prefix + ":temperature", "D3DKMT" + suffix, Metric::temperature_celsius,
                                 physical.temperature_celsius, stamp, false);
                    }
                    sensors.rows.push_back({std::to_string(physical.physical_index),
                        measure(physical.temperature_celsius.value, "°C"),
                        physical.fan_rpm.value ? std::format("{} RPM", *physical.fan_rpm.value) : absent,
                        percent(physical.power_percent_of_limit.value),
                        physical.memory_frequency_hz.value ? frequency(*physical.memory_frequency_hz.value) : absent});
                }
                if (!sensors.rows.empty()) {
                    item.tables.push_back(std::move(sensors));
                }
            }
        }
#endif
#ifdef CYHWINFO_UI_HAS_GPU_PROCESSES
        if (adapter.processes) {
            auto usage = adapter.processes->sample();
            if (usage && usage->adapter_matched) {
                auto& group = sensor_group(result, Page::gpu, "gpu:" + adapter.id, item.name);
                remember(group, "wddm-load", "Busiest engine", Metric::load_percent,
                         usage->adapter_busiest_engine_percent, usage->timestamp);
                remember(group, "wddm-dedicated", "WDDM dedicated usage", Metric::data_gibibytes,
                         scaled(usage->adapter_dedicated_usage_bytes, gibibyte), usage->timestamp);
                remember(group, "wddm-shared", "WDDM shared usage", Metric::data_gibibytes,
                         scaled(usage->adapter_shared_usage_bytes, gibibyte), usage->timestamp);
                maximum(trend(result, Summary::gpu), usage->adapter_busiest_engine_percent.value);
                field(item, "WDDM dedicated usage", bytes(usage->adapter_dedicated_usage_bytes.value));
                field(item, "WDDM shared usage", bytes(usage->adapter_shared_usage_bytes.value));
                Table processes{"GPU processes", {"PID", "Busiest engine", "Local memory", "Non-local memory"}, {}};
                std::ranges::stable_sort(usage->processes, [](const auto& left, const auto& right) {
                    return left.busiest_engine_percent.value.value_or(0) > right.busiest_engine_percent.value.value_or(0);
                });
                for (const auto& process : usage->processes) {
                    processes.rows.push_back({std::to_string(process.pid), percent(process.busiest_engine_percent.value),
                        bytes(process.memory.local_bytes.value ? process.memory.local_bytes.value :
                                                                   process.memory.dedicated_usage_bytes.value),
                        bytes(process.memory.non_local_bytes.value ? process.memory.non_local_bytes.value :
                                                                       process.memory.shared_usage_bytes.value)});
                }
                if (!processes.rows.empty()) {
                    item.tables.push_back(std::move(processes));
                }
            } else {
                field(item, "Process counters", "Unavailable", true);
            }
        }
#endif
        (void) adapter;
        (void) item;
    }
#endif
#ifdef CYHWINFO_UI_HAS_MEMORY
    auto& ram = section(result, Page::memory);
    if (const auto usage = memory::usage(); usage && usage->total_bytes != 0) {
        const auto used = usage->total_bytes - std::min(usage->total_bytes, usage->available_bytes);
        trend(result, Summary::memory) = static_cast<double>(used) * 100 / usage->total_bytes;
        auto& group = sensor_group(result, Page::memory, "ram:usage", "Physical memory", true);
        remember(group, "load", "Memory load", Metric::load_percent,
                 native_reading(trend(result, Summary::memory)), usage->timestamp);
        remember(group, "used", "In use", Metric::data_gibibytes, native_reading(used / gibibyte), usage->timestamp);
        remember(group, "available", "Available", Metric::data_gibibytes,
                 native_reading(usage->available_bytes / gibibyte), usage->timestamp);
        field(ram.devices.front(), "In use", bytes(used));
        field(ram.devices.front(), "Available", bytes(usage->available_bytes));
        field(ram.devices.front(), "Utilization", percent(trend(result, Summary::memory)));
    }
#endif
#ifdef CYHWINFO_UI_HAS_BATTERY
    auto& power = section(result, Page::battery);
    for (std::size_t i = 0; i < batteries.size(); ++i) {
        if (auto charge = battery::charge(batteries[i])) {
            auto& group = sensor_group(result, Page::battery, std::format("battery:{}", batteries[i]), power.devices[i].name);
            remember(group, "charge", "Charge", Metric::level_percent, charge->percent, charge->timestamp);
            field(power.devices[i], "Charge", percent(charge->percent.value));
            field(power.devices[i], "Battery state", charge_state(charge->state), true);
        }
    }
#endif
#ifdef CYHWINFO_UI_HAS_SENSORS
    auto& cpu_area = section(result, Page::cpu);
    auto& board_area = section(result, Page::motherboard);
    if (options.advanced_sensors) {
        cpu_area.enabled = board_area.enabled = true;
        if (sensors_sampler) {
            auto data = sensors_sampler->sample();
            if (data) {
                append_sensors(result, Page::cpu, data->cpu, data->timestamp);
                append_sensors(result, Page::motherboard, data->motherboard, data->timestamp);
                append_sensors(result, Page::memory, data->memory, data->timestamp);
                if (!data->pawnio_accessible) {
                    const char* title = data->pawnio_installed ? "PawnIO is inaccessible" : "PawnIO is missing";
                    cpu_area.notices.push_back({title, {}, Status::unavailable});
                    board_area.notices.push_back({title, {}, Status::unavailable});
                    section(result, Page::sensors).notices.push_back({title, {}, Status::unavailable});
                }
            } else {
                sensor_error = std::move(data.error());
                sensors_sampler.reset();
            }
        }
        if (sensor_error) {
            notice(cpu_area, "Advanced sensors unavailable", *sensor_error);
            notice(board_area, "Advanced sensors unavailable", *sensor_error);
            notice(section(result, Page::sensors), "Advanced sensors unavailable", *sensor_error);
        }
    }
#endif
    result.timestamp = std::chrono::system_clock::now();
    return result;
}

struct RecordedSensor {
    Timestamp last_sample{};
    SensorStatistics statistics;

    void observe(const SensorRow& row) {
        if (row.observed_at == last_sample || row.reading.status != Status::available ||
            !row.reading.value || !std::isfinite(*row.reading.value)) {
            return;
        }
        last_sample = row.observed_at;
        const auto value = *row.reading.value;
        auto& stats = statistics;
        stats.minimum = std::min(stats.minimum.value_or(value), value);
        stats.maximum = std::max(stats.maximum.value_or(value), value);
        ++stats.samples;
        if (row.metric != Metric::boolean_flag) {
            const auto average = stats.average.value_or(value);
            stats.average = average + (value - average) / static_cast<double>(stats.samples);
        }
    }
};

using SensorHistory = std::unordered_map<std::string, std::unordered_map<std::string, RecordedSensor>>;

void record_sensors(Snapshot& snapshot, SensorHistory& history) {
    for (auto& group : snapshot.sensor_groups) {
        auto& recorded = history[group.id];
        for (auto& row : group.rows) {
            auto& entry = recorded[row.id];
            entry.observe(row);
            row.statistics = entry.statistics;
        }
    }
}

} // namespace

struct Monitor::State {
    explicit State(MonitorOptions options)
        : cadence(std::max(250ms, options.interval)),
          advanced_sensors(Monitor::supports_advanced_sensors() && options.advanced_sensors),
          worker([this, options](std::stop_token stop) { run(stop, options); }) {}

    std::mutex mutex;
    std::condition_variable_any wake;
    std::shared_ptr<const Snapshot> latest = std::make_shared<const Snapshot>();
    std::chrono::milliseconds cadence;
    bool advanced_sensors;
    bool is_paused = false;
    bool scan_pending = true;
    bool sensor_reset_pending = false;
    std::uint64_t settings_revision = 0;
    std::jthread worker;

    void run(std::stop_token stop, MonitorOptions options) {
        Collection collection(options);
        auto next_sample = std::chrono::steady_clock::now();
        auto next_inventory = next_sample;
        std::array<std::vector<std::optional<double>>, static_cast<std::size_t>(Summary::count)> history;
        SensorHistory sensor_history;

        while (!stop.stop_requested()) {
            std::unique_lock lock(mutex);
            const auto revision = settings_revision;
            const auto ready = [&] { return scan_pending || sensor_reset_pending || settings_revision != revision; };
            if (is_paused) {
                wake.wait(lock, stop, ready);
            } else {
                wake.wait_until(lock, stop, next_sample, ready);
            }
            if (stop.stop_requested()) {
                return;
            }
            const auto now = std::chrono::steady_clock::now();
            if (std::exchange(sensor_reset_pending, false)) {
                for (auto& group : sensor_history) {
                    for (auto& row : group.second) {
                        row.second.statistics = {};
                    }
                }
                auto cleared = std::make_shared<Snapshot>(*latest);
                record_sensors(*cleared, sensor_history);
                latest = std::move(cleared);
                if (!scan_pending && (is_paused || now < next_sample)) {
                    continue;
                }
            }
            if (!scan_pending && is_paused) {
                continue;
            }
            const bool scan = std::exchange(scan_pending, false) || now >= next_inventory;
            const auto interval = cadence;
            const bool sensors_enabled = advanced_sensors;
            lock.unlock();

            auto snapshot = std::make_shared<Snapshot>();
            try {
                collection.options.advanced_sensors = sensors_enabled;
#ifdef CYHWINFO_UI_HAS_SENSORS
                if (!sensors_enabled) {
                    collection.sensors_sampler.reset();
                    collection.sensor_error.reset();
                }
#endif
                if (scan) {
                    collection.scan();
                    next_inventory = std::chrono::steady_clock::now() + 20s;
                }
                *snapshot = collection.sample();
                record_sensors(*snapshot, sensor_history);
                for (std::size_t i = 0; i < history.size(); ++i) {
                    auto& values = history[i];
                    if (values.size() == history_limit) {
                        values.erase(values.begin());
                    }
                    values.push_back(snapshot->trends[i].value);
                    snapshot->trends[i].history = values;
                }
            } catch (const std::exception& error) {
                snapshot->timestamp = std::chrono::system_clock::now();
                section(*snapshot, Page::overview).notices.push_back({"Sampling failed", error.what(), Status::error});
            }

            lock.lock();
            latest = std::move(snapshot);
            next_sample = std::chrono::steady_clock::now() + interval;
        }
    }
};

Monitor::Monitor(MonitorOptions options) : state_(std::make_unique<State>(options)) {}
Monitor::~Monitor() = default;

std::shared_ptr<const Snapshot> Monitor::snapshot() const {
    const std::lock_guard lock(state_->mutex);
    return state_->latest;
}

bool Monitor::paused() const {
    const std::lock_guard lock(state_->mutex);
    return state_->is_paused;
}

std::chrono::milliseconds Monitor::interval() const {
    const std::lock_guard lock(state_->mutex);
    return state_->cadence;
}

bool Monitor::supports_advanced_sensors() noexcept {
#ifdef CYHWINFO_UI_HAS_SENSORS
    return true;
#else
    return false;
#endif
}

bool Monitor::advanced_sensors_enabled() const {
    const std::lock_guard lock(state_->mutex);
    return state_->advanced_sensors;
}

void Monitor::set_paused(bool pause) {
    {
        const std::lock_guard lock(state_->mutex);
        state_->is_paused = pause;
        ++state_->settings_revision;
    }
    state_->wake.notify_all();
}

void Monitor::set_interval(std::chrono::milliseconds interval) {
    {
        const std::lock_guard lock(state_->mutex);
        state_->cadence = std::max(250ms, interval);
        ++state_->settings_revision;
    }
    state_->wake.notify_all();
}

void Monitor::set_advanced_sensors_enabled(bool enabled) {
    if (!supports_advanced_sensors()) {
        return;
    }
    {
        const std::lock_guard lock(state_->mutex);
        if (state_->advanced_sensors == enabled) {
            return;
        }
        state_->advanced_sensors = enabled;
        state_->scan_pending = true;
    }
    state_->wake.notify_all();
}

void Monitor::refresh() {
    {
        const std::lock_guard lock(state_->mutex);
        state_->scan_pending = true;
    }
    state_->wake.notify_all();
}

void Monitor::reset_sensor_statistics() {
    {
        const std::lock_guard lock(state_->mutex);
        state_->sensor_reset_pending = true;
    }
    state_->wake.notify_all();
}

} // namespace cyhwinfo::ui
