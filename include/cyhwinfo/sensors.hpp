#pragma once

#include <cyhwinfo/core.hpp>

#include <filesystem>
#include <memory>
#include <vector>

namespace cyhwinfo::sensors {

using Metric = SensorMetric;

struct Sensor {
    Metric metric;
    std::string device_id;
    std::string device_name;
    std::string id;
    std::string name;
    Reading<double> reading;
};

struct GroupSample {
    Status status;
    std::vector<Sensor> sensors;
    std::optional<Error> error;
};

struct Sample {
    Timestamp timestamp;
    bool elevated;
    bool pawnio_installed;
    bool pawnio_accessible;
    // A group is empty when it was disabled in Options.
    std::optional<GroupSample> cpu;
    std::optional<GroupSample> motherboard;
    std::optional<GroupSample> memory;
};

struct Options {
    bool cpu = true;
    bool motherboard = true;
    // Defaults to cyhwinfo-lhm/CyHWInfo.SensorBridge.exe beside the application.
    std::filesystem::path bridge_executable;
    std::chrono::milliseconds timeout{15'000};
    // Active timings from the memory controller on supported AMD systems.
    bool memory = false;
};

class Sampler {
public:
    [[nodiscard]] static Result<Sampler> open(Options options = {});
    ~Sampler();
    Sampler(Sampler&&) noexcept;
    Sampler& operator=(Sampler&&) noexcept;

    [[nodiscard]] Result<Sample> sample();

private:
    struct State;
    explicit Sampler(std::unique_ptr<State> state);
    std::unique_ptr<State> state_;
};

} // namespace cyhwinfo::sensors
