#include <cyhwinfo/ui.hpp>
#include "sensor_values.hpp"

#include <cyui/Core.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <format>
#include <span>

namespace cyhwinfo::ui {
namespace {

constexpr std::array pages{
    Page::overview, Page::sensors, Page::cpu, Page::gpu, Page::memory, Page::storage,
    Page::motherboard, Page::network, Page::battery
};

struct SummaryCard {
    Summary metric;
    const char* label;
    const char* unit;
    double maximum;
};

constexpr std::array cards{
    SummaryCard{Summary::cpu, "CPU load", "%", 100},
    SummaryCard{Summary::gpu, "GPU load", "%", 100},
    SummaryCard{Summary::memory, "Memory load", "%", 100},
    SummaryCard{Summary::temperature, "GPU temperature", " °C", 110},
};

const char* page_icon(Page page) {
    switch (page) {
    case Page::overview: return cyui::icon::home;
    case Page::cpu: return cyui::icon::equalizer;
    case Page::gpu: return cyui::icon::view;
    case Page::memory: return cyui::icon::grid;
    case Page::storage: return cyui::icon::library;
    case Page::motherboard: return cyui::icon::tiles;
    case Page::network: return cyui::icon::connect;
    case Page::battery: return cyui::icon::power;
    case Page::sensors: return cyui::icon::equalizer;
    case Page::count: break;
    }
    return nullptr;
}

std::string translation(std::string_view key) {
    return cyui::tr(std::string(key).c_str());
}

void dim(std::string_view key) {
    cyui::textDim("%s", translation(key).c_str());
}

ImU32 series_color(Summary metric) {
    const auto& p = cyui::palette();
    switch (metric) {
    case Summary::cpu: return p.accent;
    case Summary::gpu: return p.info;
    case Summary::memory: return p.success;
    case Summary::temperature: return p.warning;
    case Summary::count: break;
    }
    return p.accent;
}

void graph_line(ImDrawList& draw, const Trend& trend, ImVec2 origin, ImVec2 size,
                double maximum, ImU32 color, bool fill) {
    if (trend.history.empty()) {
        return;
    }
    const float steps = static_cast<float>(std::max<std::size_t>(2, trend.history.size()) - 1);
    std::optional<ImVec2> previous;
    for (std::size_t index = 0; index < trend.history.size(); ++index) {
        const auto& value = trend.history[index];
        if (!value || !std::isfinite(*value)) {
            previous.reset();
            continue;
        }

        const float x = origin.x + size.x * static_cast<float>(index) / steps;
        const float y = origin.y + size.y * (1.0f - static_cast<float>(std::clamp(*value / maximum, 0.0, 1.0)));
        const ImVec2 point{x, y};
        if (previous) {
            if (fill) {
                const auto area = cyui::alpha(color, 0.13f);
                const auto baseline = origin.y + size.y;
                draw.AddQuadFilled(*previous, point, {point.x, baseline}, {previous->x, baseline}, area);
            }
            draw.AddLine(*previous, point, color, cyui::dp(1.65f));
        } else {
            draw.AddCircleFilled(point, cyui::dp(2), color);
        }
        previous = point;
    }
}

void sparkline(const Trend& trend, const SummaryCard& card) {
    const auto origin = ImGui::GetCursorScreenPos();
    const ImVec2 size{ImGui::GetContentRegionAvail().x, cyui::dp(27)};
    const auto color = series_color(card.metric);
    auto& draw = *ImGui::GetWindowDrawList();
    draw.AddLine({origin.x, origin.y + size.y}, {origin.x + size.x, origin.y + size.y},
                 cyui::palette().line, cyui::dp(1.0f));
    graph_line(draw, trend, origin, size, card.maximum, color, true);
    ImGui::Dummy(size);
}

void metric_cards(const Snapshot& snapshot) {
    const int columns = ImGui::GetContentRegionAvail().x >= cyui::dp(700) ? 4 : 2;
    cyui::ScopedVar padding(ImGuiStyleVar_CellPadding, cyui::dp(5, 5));
    if (!ImGui::BeginTable("##summary", columns, ImGuiTableFlags_SizingStretchSame)) {
        return;
    }
    for (const auto& card : cards) {
        ImGui::TableNextColumn();
        const auto& trend = snapshot.trend(card.metric);
        const auto id = cyui::ScopedID(static_cast<int>(card.metric));
        if (cyui::beginCard("##metric", nullptr, 118)) {
            const auto& palette = cyui::palette();
            cyui::statusDot(series_color(card.metric));
            ImGui::SameLine(0, cyui::dp(7));
            dim(card.label);
            const auto value = trend.value && std::isfinite(*trend.value)
                ? std::format("{:.1f}{}", *trend.value, card.unit) : std::string("—");
            {
                const cyui::ScopedFont font(cyui::boldFont(), 1.6f);
                const cyui::ScopedColor text(ImGuiCol_Text, palette.text);
                ImGui::TextUnformatted(value.c_str());
            }
            cyui::space(3);
            sparkline(trend, card);
        }
        cyui::endCard();
    }
    ImGui::EndTable();
}

void activity_chart(const Snapshot& snapshot) {
    if (cyui::beginCard("##activity")) {
        cyui::heading(cyui::tr("Activity history").c_str(), 3);
        for (unsigned index = 0; index < 3; ++index) {
            if (index) {
                ImGui::SameLine(0, cyui::dp(20));
            }
            cyui::statusDot(series_color(cards[index].metric));
            ImGui::SameLine(0, cyui::dp(6));
            dim(cards[index].label);
        }
        cyui::space(8);
        const auto& p = cyui::palette();
        auto& draw = *ImGui::GetWindowDrawList();
        const auto canvas = ImGui::GetCursorScreenPos();
        const auto width = std::max(cyui::dp(70), ImGui::GetContentRegionAvail().x);
        const auto height = cyui::dp(170);
        const ImVec2 origin{canvas.x + cyui::dp(38), canvas.y + cyui::dp(8)};
        const ImVec2 size{width - cyui::dp(38), height - cyui::dp(17)};
        for (int tick = 0; tick <= 4; ++tick) {
            const float y = origin.y + static_cast<float>(tick) * size.y / 4;
            const auto label = std::format("{}%", 100 - tick * 25);
            draw.AddText({canvas.x, y - ImGui::GetFontSize() / 2}, p.textDim, label.c_str());
            draw.AddLine({origin.x, y}, {origin.x + size.x, y}, p.grid, cyui::dp(1.0f));
        }
        for (int tick = 0; tick <= 6; ++tick) {
            const auto x = origin.x + static_cast<float>(tick) * size.x / 6;
            draw.AddLine({x, origin.y}, {x, origin.y + size.y}, p.grid, cyui::dp(1.0f));
        }
        for (unsigned index = 0; index < 3; ++index) {
            const auto metric = cards[index].metric;
            graph_line(draw, snapshot.trend(metric), origin, size, 100, series_color(metric), index == 0);
        }
        ImGui::Dummy({width, height});
        dim("Recent samples");
    }
    cyui::endCard();
}

void components(const Snapshot& snapshot, Page& current) {
    if (cyui::beginCard("##components", cyui::tr("Detected hardware").c_str())) {
        const auto& p = cyui::palette();
        if (ImGui::BeginTable("##devices", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn(cyui::tr("Component").c_str(), ImGuiTableColumnFlags_WidthFixed, cyui::dp(130));
            ImGui::TableSetupColumn(cyui::tr("Device").c_str(), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (const auto page : {Page::cpu, Page::gpu, Page::memory, Page::storage, Page::motherboard, Page::network}) {
                const auto& section = snapshot.section(page);
                if (!section.enabled || section.devices.empty()) {
                    continue;
                }
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const cyui::ScopedID id(static_cast<int>(page));
                const cyui::ScopedColor color(ImGuiCol_Text, p.text);
                if (ImGui::Selectable(cyui::trLabel(page_label(page).data()).c_str(), false,
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                    current = page;
                }
                ImGui::TableSetColumnIndex(1);
                const auto& item = section.devices.front();
                const auto first = item.translate_name ? translation(item.name) : item.name;
                const auto caption = section.devices.size() == 1 || page == Page::cpu || page == Page::memory ||
                    page == Page::motherboard
                    ? first : std::format("{}  +{}", first, section.devices.size() - 1);
                ImGui::TextUnformatted(caption.c_str());
            }
            ImGui::EndTable();
        }
    }
    cyui::endCard();
}

void notices(const Section& section) {
    for (const auto& notice : section.notices) {
        const auto title = translation(notice.title);
        auto text = notice.detail.empty() ? title : title + "\n" + notice.detail;
        cyui::callout(notice.status == Status::error || notice.status == Status::permission_denied
                     ? cyui::Tone::warning : cyui::Tone::neutral, text.c_str());
    }
}

ImU32 sensor_color(SensorMetric metric) {
    const auto& colors = cyui::palette();
    switch (metric) {
    case SensorMetric::temperature_celsius: return colors.accent;
    case SensorMetric::power_watts:
    case SensorMetric::voltage_volts:
    case SensorMetric::current_amperes: return colors.warning;
    case SensorMetric::fan_rpm: return colors.info;
    case SensorMetric::c_state_residency_percent:
    case SensorMetric::load_percent: return colors.success;
    default: return colors.textFaint;
    }
}

void sensor_number(int column, SensorMetric metric, std::optional<double> value, bool current = false) {
    if (!ImGui::TableSetColumnIndex(column)) {
        return;
    }
    auto text = detail::sensor_text(metric, value);
    const bool is_flag = metric == SensorMetric::boolean_flag;
    if (is_flag && value) {
        text = cyui::tr(text.c_str());
    }
    const cyui::ScopedFont font(cyui::monoFont(), 0.94f);
    const auto& p = cyui::palette();
    const auto normal_color = value ? (current ? p.text : p.textDim) : p.textFaint;
    const cyui::ScopedColor color(ImGuiCol_Text, is_flag && current && value == 1.0 ? p.warning : normal_color);
    const auto right = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(text.c_str()).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(right, 0.0f));
    ImGui::TextUnformatted(text.c_str());
}

void sensor_row(const SensorRow& row) {
    const auto label = row.translate_label ? translation(row.label) : row.label;
    ImGui::TableNextRow();
    if (ImGui::TableSetColumnIndex(0)) {
        cyui::statusDot(sensor_color(row.metric));
        ImGui::SameLine(0, cyui::dp(6.0f));
        ImGui::TextUnformatted(label.c_str());
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            auto tip = label + "\n" + cyui::tr("Source") + " · " + detail::source_name(row.reading.source);
            cyui::tooltip(tip.c_str());
        }
    }
    const auto current = row.reading.status == Status::available ? row.reading.value : std::nullopt;
    sensor_number(1, row.metric, current, true);
    sensor_number(2, row.metric, row.statistics.minimum);
    sensor_number(3, row.metric, row.statistics.maximum);
    if (row.metric != SensorMetric::boolean_flag) {
        sensor_number(4, row.metric, row.statistics.average);
    }
}

struct VisibleSensorGroup {
    const SensorGroup* group;
    std::vector<const SensorRow*> rows;
};

std::vector<VisibleSensorGroup> visible_sensors(const Snapshot& snapshot, const cyui::TextFilter& filter,
                                                bool available_only) {
    std::vector<VisibleSensorGroup> visible;
    for (const auto& group : snapshot.sensor_groups) {
        VisibleSensorGroup entry{&group, {}};
        const auto key = group.translate_name ? translation(group.name) : group.name;
        const auto category = translation(page_label(group.category));
        const bool entire_group = !filter.active() || filter.matches(key, category);
        for (const auto& row : group.rows) {
            if (available_only && (!row.reading.value || row.reading.status != Status::available)) {
                continue;
            }
            const bool residency = row.metric == SensorMetric::c_state_residency_percent &&
                                   filter.matches("C-states", translation("C-state residency"));
            if (entire_group || residency || filter.matches(row.translate_label ? translation(row.label) : row.label,
                                               detail::source_name(row.reading.source))) {
                entry.rows.push_back(&row);
            }
        }
        if (!entry.rows.empty()) {
            visible.push_back(std::move(entry));
        }
    }
    return visible;
}

enum class SensorFamily {
    temperatures, power, voltage, current, fans, clocks, timings, utilization, c_states, other, count
};

SensorFamily family_of(SensorMetric metric) {
    switch (metric) {
    case SensorMetric::temperature_celsius: return SensorFamily::temperatures;
    case SensorMetric::power_watts:
    case SensorMetric::energy_milliwatt_hours: return SensorFamily::power;
    case SensorMetric::voltage_volts: return SensorFamily::voltage;
    case SensorMetric::current_amperes: return SensorFamily::current;
    case SensorMetric::fan_rpm:
    case SensorMetric::flow_liters_per_hour:
    case SensorMetric::control_percent: return SensorFamily::fans;
    case SensorMetric::clock_megahertz:
    case SensorMetric::frequency_hertz:
    case SensorMetric::transfer_rate_mtps:
    case SensorMetric::factor:
    case SensorMetric::timing_nanoseconds: return SensorFamily::clocks;
    case SensorMetric::timing_cycles: return SensorFamily::timings;
    case SensorMetric::load_percent: return SensorFamily::utilization;
    case SensorMetric::c_state_residency_percent: return SensorFamily::c_states;
    default: return SensorFamily::other;
    }
}

void sensor_group_rows(const VisibleSensorGroup& entry, bool filtering) {
    if (entry.rows.size() < 9 || entry.group->id == "cpu:logical" || entry.group->id.ends_with("/smu/limits")) {
        for (const auto* row : entry.rows) {
            sensor_row(*row);
        }
        return;
    }

    constexpr std::array captions{"Temperatures", "Power and energy", "Voltages", "Currents",
                                  "Fans and controls", "Clocks and timings", "Memory timings", "Utilization",
                                  "C-state residency", "Other readings"};
    static_assert(captions.size() == static_cast<std::size_t>(SensorFamily::count));
    std::array<std::vector<const SensorRow*>, captions.size()> families;
    for (const auto* row : entry.rows) {
        families[static_cast<std::size_t>(family_of(row->metric))].push_back(row);
    }
    for (std::size_t i = 0; i < families.size(); ++i) {
        const auto& rows = families[i];
        if (rows.empty()) {
            continue;
        }
        const cyui::ScopedID id(static_cast<int>(i));
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (filtering) {
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        }
        const bool collapsed = i == static_cast<std::size_t>(SensorFamily::clocks) ||
                               i == static_cast<std::size_t>(SensorFamily::utilization) ||
                               i == static_cast<std::size_t>(SensorFamily::c_states);
        const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAllColumns |
            (collapsed ? ImGuiTreeNodeFlags_None : ImGuiTreeNodeFlags_DefaultOpen);
        if (ImGui::TreeNodeEx("##family", flags, "%s", cyui::tr(captions[i]).c_str())) {
            for (const auto* row : rows) {
                sensor_row(*row);
            }
            ImGui::TreePop();
        }
    }
}

void sensor_list(const Snapshot& snapshot, const char* query, bool available_only) {
    notices(snapshot.section(Page::sensors));
    cyui::TextFilter filter;
    filter.query = query;
    const auto visible = visible_sensors(snapshot, filter, available_only);
    if (visible.empty()) {
        if (snapshot.sensor_groups.empty()) {
            cyui::emptyState(cyui::tr("No sensor readings").c_str(),
                             cyui::tr("The enabled collectors have not reported any sensors.").c_str());
        } else {
            cyui::emptyState(cyui::tr("No results").c_str(), cyui::tr("Try a different search.").c_str());
        }
        return;
    }

    const auto area = ImGui::GetContentRegionAvail();
    const auto minimum_width = cyui::dp(520.0f);
    const bool needs_horizontal_scroll = area.x < minimum_width;
    const float table_width = std::max(area.x, minimum_width);
    const auto value_width = std::clamp(table_width * 0.14f, cyui::dp(89.0f), cyui::dp(145.0f));
    const cyui::ScopedVar padding(ImGuiStyleVar_CellPadding, cyui::dp(9.0f, 4.0f));
    ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
        ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter |
        ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY;
    if (needs_horizontal_scroll) {
        flags |= ImGuiTableFlags_ScrollX;
    }
    if (!ImGui::BeginTable("##sensorReadings", 5, flags, {0.0f, std::max(area.y, cyui::dp(180.0f))},
                           needs_horizontal_scroll ? table_width : 0.0f)) {
        return;
    }
    ImGui::TableSetupColumn(cyui::tr("Sensor").c_str(), ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoReorder);
    for (const auto* label : {"Current", "Minimum", "Maximum", "Average"}) {
        ImGui::TableSetupColumn(cyui::tr(label).c_str(), ImGuiTableColumnFlags_WidthFixed, value_width);
    }
    ImGui::TableSetupScrollFreeze(needs_horizontal_scroll ? 1 : 0, 1);
    ImGui::TableHeadersRow();

    {
        const cyui::ScopedID tree_state(filter.active() ? "filtered-sensors" : "all-sensors");
        const auto& p = cyui::palette();
        for (const auto category : {Page::cpu, Page::motherboard, Page::gpu, Page::memory, Page::storage, Page::battery,
                                    Page::network}) {
            if (!std::ranges::any_of(visible, [category](const auto& entry) {
                    return entry.group->category == category;
                })) {
                continue;
            }
            const cyui::ScopedID category_id(static_cast<int>(category));
            ImGui::TableNextRow();
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, p.bg3);
            ImGui::TableSetColumnIndex(0);
            if (filter.active()) {
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            }
            constexpr auto category_flags = ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_DefaultOpen;
            bool category_open = false;
            {
                const cyui::ScopedFont font(cyui::boldFont());
                category_open = ImGui::TreeNodeEx("##category", category_flags, "%s",
                                                 translation(page_label(category)).c_str());
            }
            if (!category_open) {
                continue;
            }
            for (const auto& entry : visible) {
                const auto& group = *entry.group;
                if (group.category != category) {
                    continue;
                }
                const cyui::ScopedID group_id(group.id.c_str());
                ImGui::TableNextRow();
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, p.bg2);
                ImGui::TableSetColumnIndex(0);
                if (filter.active()) {
                    ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                }
                ImGuiTreeNodeFlags group_flags = ImGuiTreeNodeFlags_SpanAllColumns;
                if (group.id != "cpu:logical") {
                    group_flags |= ImGuiTreeNodeFlags_DefaultOpen;
                }
                const auto name = group.translate_name ? translation(group.name) : group.name;
                if (ImGui::TreeNodeEx("##group", group_flags, "%s", name.c_str())) {
                    sensor_group_rows(entry, filter.active());
                    ImGui::TreePop();
                }
            }
            ImGui::TreePop();
        }
    }
    ImGui::EndTable();
}

bool contains(const cyui::TextFilter& filter, std::span<const std::string> cells) {
    return std::ranges::any_of(cells, [&filter](const auto& text) { return filter.matches(text); });
}

void data_table(const Table& table, const cyui::TextFilter& filter, bool entire_device) {
    if (table.rows.empty() || table.columns.empty()) {
        return;
    }
    std::vector<const std::vector<std::string>*> rows;
    for (const auto& row : table.rows) {
        if (entire_device || !filter.active() || contains(filter, row)) {
            rows.push_back(&row);
        }
    }
    if (rows.empty()) {
        return;
    }
    if (!table.title.empty()) {
        cyui::space(3);
        cyui::sectionTitle(translation(table.title).c_str());
    }
    auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                 ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
    const float row_height = ImGui::GetTextLineHeight() + ImGui::GetStyle().CellPadding.y * 2;
    const auto height = rows.size() > 12 ? row_height * 13 : 0.0f;
    if (height != 0) {
        flags |= ImGuiTableFlags_ScrollY;
    }
    if (!ImGui::BeginTable("##values", static_cast<int>(table.columns.size()), flags, {0.0f, height})) {
        return;
    }
    if (height != 0) {
        ImGui::TableSetupScrollFreeze(0, 1);
    }
    for (const auto& label : table.columns) {
        ImGui::TableSetupColumn(translation(label).c_str());
    }
    ImGui::TableHeadersRow();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()), row_height);
    while (clipper.Step()) {
        for (int row_index = clipper.DisplayStart; row_index < clipper.DisplayEnd; ++row_index) {
            ImGui::TableNextRow();
            const auto& row = *rows[static_cast<std::size_t>(row_index)];
            for (std::size_t column = 0; column < std::min(row.size(), table.columns.size()); ++column) {
                if (ImGui::TableNextColumn()) {
                    const auto& value = row[column];
                    ImGui::TextUnformatted(value.c_str());
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                        cyui::tooltip(value.c_str());
                    }
                }
            }
        }
    }
    ImGui::EndTable();
}

bool has_matches(const Device& device, const cyui::TextFilter& filter) {
    for (const auto& property : device.properties) {
        if (filter.matches(property.label, property.value)) {
            return true;
        }
    }
    for (const auto& table : device.tables) {
        if (std::ranges::any_of(table.rows, [&filter](const auto& row) { return contains(filter, row); })) {
            return true;
        }
    }
    return false;
}

void property_table(std::span<const Property> properties) {
    if (!cyui::beginProperties("##details", 168.0f)) {
        return;
    }
    for (const auto& property : properties) {
        cyui::property(translation(property.label).c_str());
        const auto value = property.translate_value ? translation(property.value) : property.value;
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted(value.c_str());
        ImGui::PopTextWrapPos();
    }
    cyui::endProperties();
}

void device_properties(const Device& item, const cyui::TextFilter& filter, bool entire_device) {
    const std::span<const Property> all(item.properties);
    std::vector<Property> matches;
    auto properties = all;
    if (!entire_device) {
        for (const auto& property : all) {
            if (filter.matches(property.label, property.value)) {
                matches.push_back(property);
            }
        }
        properties = matches;
    }
    if (properties.empty()) {
        return;
    }
    if (properties.size() < 9 || ImGui::GetContentRegionAvail().x < cyui::dp(820.0f) ||
        !ImGui::BeginTable("##propertyColumns", 2, ImGuiTableFlags_SizingStretchSame)) {
        property_table(properties);
        return;
    }
    const auto split = (properties.size() + 1) / 2;
    ImGui::TableNextColumn();
    {
        const cyui::ScopedID id(0);
        property_table(properties.first(split));
    }
    ImGui::TableNextColumn();
    {
        const cyui::ScopedID id(1);
        property_table(properties.subspan(split));
    }
    ImGui::EndTable();
}

void hardware_panel(Page page, const Section& section, const char* query) {
    if (!section.enabled) {
        cyui::emptyState(cyui::tr("Module unavailable").c_str(), cyui::tr("This component is not enabled in this build.").c_str());
        return;
    }
    notices(section);
    if (section.devices.empty()) {
        cyui::emptyState(cyui::tr("No devices detected").c_str(), cyui::tr("This system did not report any devices in this category.").c_str());
        return;
    }
    cyui::TextFilter filter;
    filter.query = query;
    bool shown = false;
    for (std::size_t index = 0; index < section.devices.size(); ++index) {
        const auto& item = section.devices[index];
        const auto entire_device = !filter.active() || filter.matches(item.name, item.description);
        if (!entire_device && !has_matches(item, filter)) {
            continue;
        }
        shown = true;
        const cyui::ScopedID id(static_cast<int>(index));
        const auto heading = std::format("{}###device", item.translate_name ? translation(item.name) : item.name);
        if (!cyui::beginFilteredSection(heading.c_str(), filter, true, index == 0 || page == Page::motherboard)) {
            continue;
        }
        if (!item.description.empty()) {
            cyui::textDim("%s", item.description.c_str());
        }
        device_properties(item, filter, entire_device);
        for (std::size_t table_index = 0; table_index < item.tables.size(); ++table_index) {
            const cyui::ScopedID table_id(static_cast<int>(table_index));
            data_table(item.tables[table_index], filter, entire_device);
        }
        cyui::endSection();
        cyui::space(8);
    }
    if (!shown) {
        cyui::emptyState(cyui::tr("No results").c_str(), cyui::tr("Try a different search.").c_str());
    }
}

bool tool(const char* icon, const char* label, const char* shortcut_label, bool active = false) {
    if (cyui::iconsAvailable()) {
        return cyui::iconButton(icon, cyui::tr(shortcut_label).c_str(), active);
    }
    return cyui::button(cyui::trLabel(label).c_str());
}

void settings_popup(Monitor& monitor) {
    if (!ImGui::BeginPopup("##monitorSettings")) {
        return;
    }
    cyui::heading(cyui::tr("Settings").c_str(), 3);
    cyui::space(3);
    if (cyui::beginProperties("##settings", 126)) {
        cyui::property(cyui::tr("Sample interval").c_str());
        static constexpr std::array intervals{
            std::chrono::milliseconds{250}, std::chrono::milliseconds{500}, std::chrono::milliseconds{1000},
            std::chrono::milliseconds{2000}, std::chrono::milliseconds{5000}
        };
        const auto current = monitor.interval();
        const auto label = std::format("{} ms", current.count());
        ImGui::SetNextItemWidth(cyui::dp(126));
        if (ImGui::BeginCombo("##interval", label.c_str())) {
            for (const auto interval : intervals) {
                if (ImGui::Selectable(std::format("{} ms", interval.count()).c_str(), interval == current)) {
                    monitor.set_interval(interval);
                }
            }
            ImGui::EndCombo();
        }

        cyui::property(cyui::tr("Language").c_str());
        ImGui::SetNextItemWidth(cyui::dp(126));
        if (ImGui::BeginCombo("##language", cyui::locale() == "fr" ? "Français" : "English")) {
            if (ImGui::Selectable("English", cyui::locale() == "en")) {
                cyui::setLocale("en");
            }
            if (ImGui::Selectable("Français", cyui::locale() == "fr")) {
                cyui::setLocale("fr");
            }
            ImGui::EndCombo();
        }
        if (Monitor::supports_advanced_sensors()) {
            cyui::property(cyui::tr("Advanced sensors").c_str());
            bool enabled = monitor.advanced_sensors_enabled();
            if (cyui::toggleSwitch("##advancedSensors", &enabled)) {
                monitor.set_advanced_sensors_enabled(enabled);
            }
        }
        cyui::endProperties();
    }
    ImGui::EndPopup();
}

void header(Page page, Monitor& monitor, bool ready) {
    cyui::heading(translation(page_label(page)).c_str(), 1);
    ImGui::SameLine();
    const bool paused = monitor.paused();
    const auto label = cyui::tr(paused ? "Paused" : ready ? "Live" : "Loading");
    cyui::alignRight(cyui::dp(cyui::iconsAvailable() ? 182.0f : 354.0f));
    cyui::badge(label.c_str(), paused ? cyui::Tone::warning : ready ? cyui::Tone::success : cyui::Tone::neutral);
    ImGui::SameLine();
    if (tool(cyui::icon::refresh, "Refresh", "Refresh (Ctrl+R)")) {
        monitor.refresh();
    }
    ImGui::SameLine();
    if (tool(paused ? cyui::icon::play : cyui::icon::pause, paused ? "Resume" : "Pause",
             paused ? "Resume (Space)" : "Pause (Space)", paused)) {
        monitor.set_paused(!paused);
    }
    ImGui::SameLine();
    if (tool(cyui::icon::settings, "Settings", "Settings")) {
        ImGui::OpenPopup("##monitorSettings");
    }
    settings_popup(monitor);
    if (!ImGui::IsAnyItemActive()) {
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_R, false)) {
            monitor.refresh();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false) && !ImGui::IsAnyItemFocused()) {
            monitor.set_paused(!paused);
        }
    }
}

std::string update_time(Timestamp timestamp) {
    if (timestamp == Timestamp{}) {
        return "—";
    }
    const auto value = std::chrono::system_clock::to_time_t(timestamp);
    std::tm parts{};
#ifdef _WIN32
    localtime_s(&parts, &value);
#else
    localtime_r(&value, &parts);
#endif
    return std::format("{:02d}:{:02d}:{:02d}", parts.tm_hour, parts.tm_min, parts.tm_sec);
}

} // namespace

std::string_view page_label(Page page) {
    switch (page) {
    case Page::overview: return "Overview";
    case Page::cpu: return "Processor";
    case Page::gpu: return "Graphics";
    case Page::memory: return "Memory";
    case Page::storage: return "Storage";
    case Page::motherboard: return "Motherboard";
    case Page::network: return "Network";
    case Page::battery: return "Battery";
    case Page::sensors: return "Sensors";
    case Page::count: break;
    }
    return "Overview";
}

void Dashboard::select(Page page) {
    if (page >= Page::overview && page < Page::count && page != page_) {
        page_ = page;
        filter_.fill('\0');
    }
}

void Dashboard::draw(Monitor& monitor) {
    const auto snapshot = monitor.snapshot();
    const bool ready = snapshot->timestamp != Timestamp{};
    const auto& p = cyui::palette();
    const auto available_width = ImGui::GetContentRegionAvail().x;
    const auto available_height = ImGui::GetContentRegionAvail().y;
    const auto sidebar = cyui::dp(available_width >= cyui::dp(850) ? 212.0f : 152.0f);

    {
        const cyui::ScopedColor ground(ImGuiCol_ChildBg, p.bg0);
        const cyui::ScopedVar padding(ImGuiStyleVar_WindowPadding, cyui::dp(13, 18));
        if (ImGui::BeginChild("##navigation", {sidebar, available_height}, ImGuiChildFlags_AlwaysUseWindowPadding,
                              ImGuiWindowFlags_NoScrollbar)) {
            cyui::space(5);
            cyui::heading("CyHWInfo", 2);
            cyui::space(12);
            cyui::sidebarGroup(cyui::tr("Hardware").c_str());
            for (const auto page : pages) {
                if (page == Page::overview || page == Page::sensors || !ready || snapshot->section(page).enabled) {
                    if (cyui::sidebarItem(page_icon(page), cyui::trLabel(page_label(page).data()).c_str(), page == page_)) {
                        select(page);
                    }
                }
            }

            const auto free = ImGui::GetContentRegionAvail().y;
            if (free > cyui::dp(58)) {
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + free - cyui::dp(46));
                cyui::textFaint("%s", cyui::tr("Last sample").c_str());
                cyui::textDim("%s", update_time(snapshot->timestamp).c_str());
            }
        }
        ImGui::EndChild();
    }
    ImGui::SameLine(0, 0);
    {
        const cyui::ScopedColor ground(ImGuiCol_ChildBg, p.bg1);
        const cyui::ScopedVar padding(ImGuiStyleVar_WindowPadding, cyui::dp(23, 18));
        if (ImGui::BeginChild("##workspace", {0, available_height}, ImGuiChildFlags_AlwaysUseWindowPadding)) {
            header(page_, monitor, ready);
            cyui::space(12);
            if (!ready) {
                cyui::emptyState(cyui::tr("Reading hardware").c_str(), cyui::tr("Waiting for the first sample.").c_str());
            } else {
                if (page_ != Page::overview && page_ != Page::sensors && !snapshot->section(page_).enabled) {
                    select(Page::overview);
                }
                if (page_ == Page::sensors) {
                    cyui::searchField("##sensorFind", filter_.data(), static_cast<int>(filter_.size()),
                                      cyui::tr("Filter sensors").c_str(), cyui::dp(235.0f));
                    if (ImGui::GetContentRegionAvail().x >= cyui::dp(615.0f)) {
                        ImGui::SameLine();
                    }
                    ImGui::Checkbox(cyui::trLabel("Available only").c_str(), &available_sensors_only_);
                    ImGui::SameLine();
                    if (cyui::button(cyui::trLabel("Reset statistics").c_str())) {
                        monitor.reset_sensor_statistics();
                    }
                    cyui::space(6);
                } else if (page_ != Page::overview) {
                    cyui::searchField("##hardwareFind", filter_.data(), static_cast<int>(filter_.size()),
                                      cyui::tr("Filter hardware").c_str(), cyui::dp(300));
                    cyui::space(8);
                }
                draw_panel(page_, *snapshot);
            }
        }
        ImGui::EndChild();
    }
}

void Dashboard::draw_panel(Page page, const Snapshot& snapshot) {
    if (page == Page::overview) {
        notices(snapshot.section(Page::overview));
        metric_cards(snapshot);
        cyui::space(10);
        activity_chart(snapshot);
        cyui::space(12);
        auto selected = page_;
        components(snapshot, selected);
        select(selected);
    } else if (page == Page::sensors) {
        sensor_list(snapshot, page == page_ ? filter_.data() : "", available_sensors_only_);
    } else if (page > Page::overview && page < Page::count) {
        hardware_panel(page, snapshot.section(page), page == page_ ? filter_.data() : "");
    }
}

} // namespace cyhwinfo::ui
