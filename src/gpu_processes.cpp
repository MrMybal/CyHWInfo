#include <cyhwinfo/gpu_processes.hpp>

#ifdef _WIN32
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <limits>
#include <map>
#include <regex>
#include <tuple>
#endif

namespace cyhwinfo::gpu {
namespace {

constexpr auto source = Source::windows_pdh;

#ifdef _WIN32
// Adapted from gputaskman, src/gputaskman/pdh.py (MIT).
// Copyright (c) 2026 Scott Hanselman. See THIRD-PARTY-NOTICES.txt.
struct Counter {
    PDH_HCOUNTER handle{};

    void add(PDH_HQUERY query, const wchar_t* path) {
        if (PdhAddEnglishCounterW(query, path, 0, &handle) != ERROR_SUCCESS) {
            handle = nullptr;
        }
    }

    template<class Read>
    void visit(DWORD format, Read read) const {
        if (!handle) {
            return;
        }
        DWORD bytes = 0;
        DWORD count = 0;
        const auto first = PdhGetFormattedCounterArrayW(handle, format, &bytes, &count, nullptr);
        if (first != PDH_MORE_DATA || bytes == 0) {
            return;
        }
        // The PDH structure contains pointers and a LARGE_INTEGER/double union.
        std::vector<PDH_FMT_COUNTERVALUE_ITEM_W> items;
        for (int attempt = 0; attempt < 3; ++attempt) {
            items.resize((bytes - 1) / sizeof(PDH_FMT_COUNTERVALUE_ITEM_W) + 1);
            const auto status = PdhGetFormattedCounterArrayW(handle, format, &bytes, &count, items.data());
            if (status == PDH_MORE_DATA && bytes != 0) {
                continue;
            }
            if (status == ERROR_SUCCESS && count <= items.size()) {
                for (DWORD index = 0; index < count; ++index) {
                    const auto& item = items[index];
                    if (item.szName && (item.FmtValue.CStatus == PDH_CSTATUS_VALID_DATA ||
                                        item.FmtValue.CStatus == PDH_CSTATUS_NEW_DATA)) {
                        read(item.szName, item.FmtValue);
                    }
                }
            }
            return;
        }
    }
};

const std::wregex& process_pattern() {
    static const std::wregex pattern(
        LR"(^pid_([0-9]{1,10})_luid_0x([0-9a-f]{1,8})_0x([0-9a-f]{1,8})(?:_phys_[0-9]+)?(?:#[0-9]+)?$)",
        std::regex::icase | std::regex::optimize);
    return pattern;
}

const std::wregex& engine_pattern() {
    static const std::wregex pattern(
        LR"(^pid_([0-9]{1,10})_luid_0x([0-9a-f]{1,8})_0x([0-9a-f]{1,8})_phys_([0-9]{1,10})_eng_([0-9]{1,10})_engtype_(.+?)(?:#[0-9]+)?$)",
        std::regex::icase | std::regex::optimize);
    return pattern;
}

const std::wregex& adapter_pattern() {
    static const std::wregex pattern(
        LR"(^luid_0x([0-9a-f]{1,8})_0x([0-9a-f]{1,8})(?:_phys_[0-9]+)?(?:#[0-9]+)?$)",
        std::regex::icase | std::regex::optimize);
    return pattern;
}

std::uint64_t number(const std::wcsub_match& match, int base = 10) {
    // The regular expressions bound the digit count and constrain the suffix.
    return std::wcstoull(match.first, nullptr, base);
}

std::uint64_t luid_of(const std::wcsub_match& high, const std::wcsub_match& low) {
    return (number(high, 16) << 32) | number(low, 16);
}

void add_bytes(Reading<std::uint64_t>& reading, std::int64_t value) {
    if (value < 0 || reading.status == Status::error) {
        return;
    }
    const auto bytes = static_cast<std::uint64_t>(value);
    const auto total = reading.value.value_or(0);
    if (std::numeric_limits<std::uint64_t>::max() - total < bytes) {
        reading = {std::nullopt, source, Status::error};
        return;
    }
    reading = Reading<std::uint64_t>::observed(total + bytes, source);
}

bool valid_percent(double value) {
    return std::isfinite(value) && value >= 0 && value <= 100;
}

std::unexpected<Error> pdh_error(PDH_STATUS status, std::string message) {
    const auto state = status == PDH_ACCESS_DENIED ? Status::permission_denied : Status::error;
    return std::unexpected(Error{state, source, {status, std::system_category()}, std::move(message)});
}

struct EngineUsage {
    std::wstring type;
    double total = 0;
    bool valid = true;

    void add(double value) {
        if (!valid_percent(value)) {
            valid = false;
        } else {
            total += value;
        }
    }

    Reading<double> reading() const {
        if (!valid) {
            return {std::nullopt, source, Status::unavailable};
        }
        return Reading<double>::observed(std::min(100.0, total), source);
    }
};
#endif

} // namespace

struct ProcessSampler::State {
#ifdef _WIN32
    std::uint64_t luid = 0;
    PDH_HQUERY query{};
    Counter engine;
    std::array<Counter, 5> process_memory;
    Counter adapter_dedicated;
    Counter adapter_shared;

    ~State() {
        if (query) {
            PdhCloseQuery(query);
        }
    }
#endif
};

ProcessSampler::ProcessSampler(std::unique_ptr<State> state) : state_(std::move(state)) {}
ProcessSampler::~ProcessSampler() = default;
ProcessSampler::ProcessSampler(ProcessSampler&&) noexcept = default;
ProcessSampler& ProcessSampler::operator=(ProcessSampler&&) noexcept = default;

Result<ProcessSampler> ProcessSampler::open(const Device& device) {
#ifdef _WIN32
    if (!device.windows_luid) {
        return std::unexpected(Error{Status::unsupported, source, {}, "GPU has no WDDM adapter LUID"});
    }
    auto state = std::make_unique<State>();
    state->luid = *device.windows_luid;
    const auto opened = PdhOpenQueryW(nullptr, 0, &state->query);
    if (opened != ERROR_SUCCESS) {
        return pdh_error(opened, "Opening the WDDM performance query failed");
    }
    constexpr std::array paths{
        LR"(\GPU Process Memory(*)\Local Usage)",
        LR"(\GPU Process Memory(*)\Non Local Usage)",
        LR"(\GPU Process Memory(*)\Dedicated Usage)",
        LR"(\GPU Process Memory(*)\Shared Usage)",
        LR"(\GPU Process Memory(*)\Total Committed)",
    };
    for (std::size_t index = 0; index < paths.size(); ++index) {
        state->process_memory[index].add(state->query, paths[index]);
    }
    state->engine.add(state->query, LR"(\GPU Engine(*)\Utilization Percentage)");
    state->adapter_dedicated.add(state->query, LR"(\GPU Adapter Memory(*)\Dedicated Usage)");
    state->adapter_shared.add(state->query, LR"(\GPU Adapter Memory(*)\Shared Usage)");
    if (!state->engine.handle && !state->adapter_dedicated.handle && !state->adapter_shared.handle &&
        std::ranges::none_of(state->process_memory, [](const auto& counter) { return counter.handle; })) {
        return std::unexpected(Error{Status::unsupported, source, {}, "WDDM GPU counters are unavailable"});
    }
    // Prime rate counters. GPU engine instances may appear after this call.
    PdhCollectQueryData(state->query);
    return ProcessSampler(std::move(state));
#else
    (void) device;
    return std::unexpected(Error{Status::unsupported, source, {}, "GPU process counters require Windows WDDM"});
#endif
}

Result<ProcessSample> ProcessSampler::sample() {
#ifdef _WIN32
    if (!state_) {
        return std::unexpected(Error{Status::error, source, {}, "GPU process sampler has been moved"});
    }
    const auto collected = PdhCollectQueryData(state_->query);
    if (collected != ERROR_SUCCESS && collected != PDH_NO_DATA) {
        return pdh_error(collected, "Collecting WDDM GPU counters failed");
    }
    ProcessSample result{};
    result.timestamp = std::chrono::system_clock::now();
    if (collected == PDH_NO_DATA) {
        return result;
    }

    std::map<std::uint32_t, ProcessUsage> processes;
    constexpr std::array memory_fields{
        &ProcessMemory::local_bytes,
        &ProcessMemory::non_local_bytes,
        &ProcessMemory::dedicated_usage_bytes,
        &ProcessMemory::shared_usage_bytes,
        &ProcessMemory::committed_bytes,
    };
    for (std::size_t index = 0; index < memory_fields.size(); ++index) {
        state_->process_memory[index].visit(PDH_FMT_LARGE, [&](const wchar_t* name, const auto& value) {
            std::wcmatch match;
            if (!std::regex_match(name, match, process_pattern()) || luid_of(match[2], match[3]) != state_->luid) {
                return;
            }
            const auto pid = number(match[1]);
            if (pid > std::numeric_limits<std::uint32_t>::max()) {
                return;
            }
            result.adapter_matched = true;
            add_bytes(processes[static_cast<std::uint32_t>(pid)].memory.*memory_fields[index], value.largeValue);
        });
    }

    using EngineKey = std::pair<std::uint32_t, std::uint32_t>;
    std::map<EngineKey, EngineUsage> adapter_engines;
    std::map<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>, EngineUsage> process_engines;
    state_->engine.visit(PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, [&](const wchar_t* name, const auto& value) {
        std::wcmatch match;
        if (!std::regex_match(name, match, engine_pattern()) || luid_of(match[2], match[3]) != state_->luid) {
            return;
        }
        const auto pid = number(match[1]);
        const auto physical = number(match[4]);
        const auto engine = number(match[5]);
        constexpr auto max_index = std::numeric_limits<std::uint32_t>::max();
        if (pid > max_index || physical > max_index || engine > max_index) {
            return;
        }
        result.adapter_matched = true;
        const EngineKey key{static_cast<std::uint32_t>(physical), static_cast<std::uint32_t>(engine)};
        auto& entry = process_engines[{static_cast<std::uint32_t>(pid), key.first, key.second}];
        entry.type = match[6].str();
        entry.add(value.doubleValue);
        adapter_engines[key].add(value.doubleValue);
    });

    const auto read_adapter_memory = [&](const Counter& counter, Reading<std::uint64_t>& destination) {
        counter.visit(PDH_FMT_LARGE, [&](const wchar_t* name, const auto& value) {
            std::wcmatch match;
            if (std::regex_match(name, match, adapter_pattern()) && luid_of(match[1], match[2]) == state_->luid) {
                result.adapter_matched = true;
                add_bytes(destination, value.largeValue);
            }
        });
    };
    read_adapter_memory(state_->adapter_dedicated, result.adapter_dedicated_usage_bytes);
    read_adapter_memory(state_->adapter_shared, result.adapter_shared_usage_bytes);

    for (auto& [key, usage] : process_engines) {
        const auto [pid, physical, engine] = key;
        processes[pid].engines.push_back({physical, engine, std::move(usage.type), usage.reading()});
    }
    const auto add_busiest = [](Reading<double>& busiest, const Reading<double>& engine) {
        if (!engine.value) {
            busiest = {std::nullopt, source, Status::error};
        } else if (busiest.status != Status::error) {
            busiest = Reading<double>::observed(std::max(busiest.value.value_or(0), *engine.value), source);
        }
    };
    for (const auto& [key, usage] : adapter_engines) {
        add_busiest(result.adapter_busiest_engine_percent, usage.reading());
    }
    for (auto& [pid, process] : processes) {
        process.pid = pid;
        for (const auto& engine : process.engines) {
            add_busiest(process.busiest_engine_percent, engine.utilization_percent);
        }
        result.processes.push_back(std::move(process));
    }
    return result;
#else
    return std::unexpected(Error{Status::unsupported, source, {}, "GPU process counters require Windows WDDM"});
#endif
}

} // namespace cyhwinfo::gpu
