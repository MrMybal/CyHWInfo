#pragma once

#include <cyhwinfo/core.hpp>

#include <array>
#include <memory>
#include <string_view>
#include <vector>

namespace cyhwinfo::ui {

enum class Page { overview, cpu, gpu, memory, storage, motherboard, network, battery, sensors, count };
enum class Summary { cpu, gpu, memory, temperature, count };

struct Property {
    std::string label;
    std::string value;
    bool translate_value = false;
};

struct Table {
    std::string title;
    std::vector<std::string> columns;
    std::vector<std::vector<std::string>> rows;
};

struct Device {
    std::string name;
    std::string description;
    std::vector<Property> properties;
    std::vector<Table> tables;
    bool translate_name = false;
};

struct Notice {
    std::string title;
    std::string detail;
    Status status = Status::unavailable;
};

struct Section {
    bool enabled = false;
    std::vector<Device> devices;
    std::vector<Notice> notices;
};

struct Trend {
    std::optional<double> value;
    std::vector<std::optional<double>> history;
};

struct SensorStatistics {
    std::optional<double> minimum;
    std::optional<double> maximum;
    // Boolean flags track their observed range and sample count.
    std::optional<double> average;
    std::uint64_t samples = 0;
};

struct SensorRow {
    std::string id;
    std::string label;
    SensorMetric metric;
    Reading<double> reading;
    Timestamp observed_at;
    SensorStatistics statistics;
    bool translate_label = false;
};

struct SensorGroup {
    Page category;
    std::string id;
    std::string name;
    std::vector<SensorRow> rows;
    bool translate_name = false;
};

struct Snapshot {
    Timestamp timestamp{};
    std::array<Section, static_cast<std::size_t>(Page::count)> sections;
    std::array<Trend, static_cast<std::size_t>(Summary::count)> trends;
    std::vector<SensorGroup> sensor_groups;

    [[nodiscard]] const Section& section(Page page) const { return sections[static_cast<std::size_t>(page)]; }
    [[nodiscard]] const Trend& trend(Summary metric) const { return trends[static_cast<std::size_t>(metric)]; }
};

struct MonitorOptions {
    std::chrono::milliseconds interval{1000};
    bool advanced_sensors = true;
    bool gpu_processes = true;
    bool storage_health = true;
};

// Samples the selected CyHWInfo components on a worker. Snapshots may outlive Monitor.
class Monitor {
public:
    explicit Monitor(MonitorOptions options = {});
    ~Monitor();
    Monitor(const Monitor&) = delete;
    Monitor& operator=(const Monitor&) = delete;

    [[nodiscard]] std::shared_ptr<const Snapshot> snapshot() const;
    [[nodiscard]] bool paused() const;
    [[nodiscard]] std::chrono::milliseconds interval() const;
    [[nodiscard]] static bool supports_advanced_sensors() noexcept;
    [[nodiscard]] bool advanced_sensors_enabled() const;
    void set_paused(bool pause);
    void set_interval(std::chrono::milliseconds interval);
    void set_advanced_sensors_enabled(bool enabled);
    void refresh();
    // Clear the running statistics. The current readings stay in the snapshot.
    void reset_sensor_statistics();

private:
    struct State;
    std::unique_ptr<State> state_;
};

// Register the supplied French translations before drawing. English is the default.
void register_translations();
[[nodiscard]] std::string_view page_label(Page page);

// Draw in an existing ImGui window with its CyUI context current.
class Dashboard {
public:
    [[nodiscard]] Page page() const noexcept { return page_; }
    void select(Page page);
    void draw(Monitor& monitor);

    // Hosts can render an individual panel or supply their own snapshots.
    void draw_panel(Page page, const Snapshot& snapshot);

private:
    Page page_ = Page::overview;
    std::array<char, 192> filter_{};
    bool available_sensors_only_ = false;
};

} // namespace cyhwinfo::ui
