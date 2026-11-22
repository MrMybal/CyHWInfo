#include <cyhwinfo/sensors.hpp>

#include <windows.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <span>
#include <string_view>
#include <thread>

namespace cyhwinfo::sensors {
namespace {

constexpr auto source = Source::libre_hardware_monitor;
constexpr std::uint32_t protocol_version = 6;
constexpr std::uint32_t max_frame_length = 1024 * 1024;
constexpr std::uint8_t ready_frame = 1;
constexpr std::uint8_t sample_frame = 2;
constexpr std::uint8_t error_frame = 3;

struct CloseHandleDeleter {
    void operator()(HANDLE handle) const noexcept { CloseHandle(handle); }
};

using Handle = std::unique_ptr<void, CloseHandleDeleter>;

std::unexpected<Error> error(Status status, std::string message, std::error_code code = {}) {
    return std::unexpected(Error{status, source, code, std::move(message)});
}

std::unexpected<Error> win_error(std::string_view message, DWORD code = GetLastError()) {
    Status status = Status::error;
    if (code == ERROR_ACCESS_DENIED || code == ERROR_PRIVILEGE_NOT_HELD) {
        status = Status::permission_denied;
    } else if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) {
        status = Status::not_found;
    }
    return error(status, std::string(message), {static_cast<int>(code), std::system_category()});
}

struct Pipe {
    Handle read;
    Handle write;
};

Result<Pipe> pipe(bool child_reads) {
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE reader = nullptr;
    HANDLE writer = nullptr;
    if (!CreatePipe(&reader, &writer, &security, 0)) {
        return win_error("Creating a sensor bridge pipe failed");
    }
    Pipe handles{Handle(reader), Handle(writer)};
    if (!SetHandleInformation(child_reads ? writer : reader, HANDLE_FLAG_INHERIT, 0)) {
        return win_error("Configuring sensor bridge pipe inheritance failed");
    }
    return handles;
}

class InheritedHandles {
public:
    ~InheritedHandles() {
        if (list) {
            DeleteProcThreadAttributeList(list);
        }
    }

    Result<void> init(std::span<HANDLE> handles) {
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        if (bytes == 0) {
            return win_error("Sizing the sensor bridge startup attributes failed");
        }
        storage_.resize(bytes);
        const auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage_.data());
        if (!InitializeProcThreadAttributeList(attributes, 1, 0, &bytes)) {
            return win_error("Creating sensor bridge startup attributes failed");
        }
        list = attributes;
        if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles.data(),
                                      handles.size_bytes(), nullptr, nullptr)) {
            return win_error("Selecting handles for the sensor bridge failed");
        }
        return {};
    }

    LPPROC_THREAD_ATTRIBUTE_LIST list = nullptr;

private:
    std::vector<std::byte> storage_;
};

Result<std::filesystem::path> bridge_path(const std::filesystem::path& requested) {
    if (!requested.empty()) {
        std::error_code code;
        auto result = std::filesystem::absolute(requested, code);
        if (code) {
            return error(Status::error, "Resolving the sensor bridge path failed", code);
        }
        return result;
    }
    std::wstring path(256, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (!length) {
            return win_error("Finding the application directory failed");
        }
        if (length < path.size()) {
            path.resize(length);
            return std::filesystem::path(path).parent_path() / L"cyhwinfo-lhm" / L"CyHWInfo.SensorBridge.exe";
        }
        if (path.size() == 32'768) {
            return win_error("The application path is too long", ERROR_FILENAME_EXCED_RANGE);
        }
        path.resize(path.size() * 2);
    }
}

struct InvalidFrame {};

class FrameReader {
public:
    explicit FrameReader(std::span<const std::byte> data) : data_(data) {}

    template<class T>
    T number() {
        static_assert(std::endian::native == std::endian::little);
        T value{};
        const auto bytes = take(sizeof(T));
        std::memcpy(&value, bytes.data(), sizeof(T));
        return value;
    }

    bool flag() {
        const auto value = number<std::uint8_t>();
        if (value > 1) {
            throw InvalidFrame{};
        }
        return value == 1;
    }

    std::string string() {
        const auto bytes = take(number<std::uint32_t>());
        return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }

    std::size_t remaining() const { return data_.size(); }

private:
    std::span<const std::byte> take(std::size_t bytes) {
        if (bytes > remaining()) {
            throw InvalidFrame{};
        }
        const auto result = data_.first(bytes);
        data_ = data_.subspan(bytes);
        return result;
    }

    std::span<const std::byte> data_;
};

std::optional<GroupSample> group(FrameReader& frame) {
    if (!frame.flag()) {
        return std::nullopt;
    }
    auto message = frame.string();
    const auto count = frame.number<std::uint32_t>();
    constexpr std::size_t smallest_sensor = 2 + 4 * sizeof(std::uint32_t) + 1;
    if (count > frame.remaining() / smallest_sensor) {
        throw InvalidFrame{};
    }
    GroupSample result{Status::unavailable, {}, {}};
    result.sensors.reserve(count);
    if (!message.empty()) {
        result.status = Status::error;
        result.error = Error{Status::error, source, {}, std::move(message)};
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto metric = frame.number<std::uint8_t>();
        if (metric >= static_cast<std::uint8_t>(Metric::count)) {
            throw InvalidFrame{};
        }
        Source sensor_source;
        switch (frame.number<std::uint8_t>()) {
        case 0: sensor_source = Source::libre_hardware_monitor; break;
        case 1: sensor_source = Source::amd_smu; break;
        case 2: sensor_source = Source::amd_umc; break;
        case 3: sensor_source = Source::amd_thermal_controller; break;
        default: throw InvalidFrame{};
        }
        Sensor sensor{static_cast<Metric>(metric), frame.string(), frame.string(), frame.string(), frame.string(),
                      {std::nullopt, sensor_source, Status::unavailable}};
        if (frame.flag()) {
            const auto value = frame.number<double>();
            if (!std::isfinite(value) || (sensor.metric == Metric::boolean_flag && value != 0.0 && value != 1.0)) {
                throw InvalidFrame{};
            }
            sensor.reading = Reading<double>::observed(value, sensor_source);
            result.status = Status::available;
        }
        result.sensors.push_back(std::move(sensor));
    }
    return result;
}

std::unexpected<Error> managed_error(FrameReader& frame) {
    auto message = frame.string();
    if (frame.remaining() != 0) {
        throw InvalidFrame{};
    }
    return error(Status::error, "LibreHardwareMonitor: " + message);
}

} // namespace

struct Sampler::State {
    Handle input;
    Handle output;
    Handle process;
    std::chrono::milliseconds timeout;
    std::mutex mutex;

    ~State() { stop(); }

    void stop() noexcept {
        input.reset();
        output.reset();
        if (process && WaitForSingleObject(process.get(), 500) == WAIT_TIMEOUT) {
            TerminateProcess(process.get(), 1);
        }
        process.reset();
    }

    Result<void> read(std::span<std::byte> bytes, std::chrono::steady_clock::time_point deadline) const {
        while (!bytes.empty()) {
            DWORD available = 0;
            if (!PeekNamedPipe(output.get(), nullptr, 0, nullptr, &available, nullptr)) {
                const auto code = GetLastError();
                if (code == ERROR_BROKEN_PIPE || code == ERROR_PIPE_NOT_CONNECTED) {
                    DWORD process_code = 0;
                    if (GetExitCodeProcess(process.get(), &process_code) && process_code == 0x80008096) {
                        return error(Status::not_found, "The sensor bridge requires the .NET 8 Runtime (Microsoft.NETCore.App)");
                    }
                    return error(Status::error, "The LibreHardwareMonitor sensor bridge exited",
                                 {static_cast<int>(process_code), std::system_category()});
                }
                return win_error("Reading from the sensor bridge failed", code);
            }
            if (available != 0) {
                const auto chunk = std::min(available, static_cast<DWORD>(bytes.size()));
                DWORD received = 0;
                if (!ReadFile(output.get(), bytes.data(), chunk, &received, nullptr)) {
                    return win_error("Receiving the sensor bridge response failed");
                }
                if (received == 0) {
                    return error(Status::error, "The sensor bridge closed its response pipe");
                }
                bytes = bytes.subspan(received);
            } else {
                if (std::chrono::steady_clock::now() >= deadline) {
                    return error(Status::error, "Timed out waiting for the LibreHardwareMonitor sensor bridge",
                                 std::make_error_code(std::errc::timed_out));
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
        return {};
    }

    Result<std::vector<std::byte>> frame() const {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        std::uint32_t size = 0;
        if (auto header = read(std::as_writable_bytes(std::span{&size, 1}), deadline); !header) {
            return std::unexpected(std::move(header.error()));
        }
        if (size == 0 || size > max_frame_length) {
            return error(Status::error, "Invalid LibreHardwareMonitor sensor frame length");
        }
        std::vector<std::byte> data(size);
        if (auto body = read(data, deadline); !body) {
            return std::unexpected(std::move(body.error()));
        }
        return data;
    }
};

Sampler::Sampler(std::unique_ptr<State> state) : state_(std::move(state)) {}
Sampler::~Sampler() = default;
Sampler::Sampler(Sampler&&) noexcept = default;
Sampler& Sampler::operator=(Sampler&&) noexcept = default;

Result<Sampler> Sampler::open(Options options) {
    if ((!options.cpu && !options.motherboard && !options.memory) ||
        options.timeout <= std::chrono::milliseconds::zero()) {
        return error(Status::error, "Select a sensor group and a positive timeout",
                     std::make_error_code(std::errc::invalid_argument));
    }
    auto executable = bridge_path(options.bridge_executable);
    if (!executable) {
        return std::unexpected(std::move(executable.error()));
    }
    const auto directory = executable->parent_path();
    auto command = L"\"" + executable->native() + L"\"" + (options.cpu ? L" 1" : L" 0") +
                   (options.motherboard ? L" 1" : L" 0") + (options.memory ? L" 1" : L" 0");

    auto request = pipe(true);
    if (!request) {
        return std::unexpected(std::move(request.error()));
    }
    auto response = pipe(false);
    if (!response) {
        return std::unexpected(std::move(response.error()));
    }
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    const auto null = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (null == INVALID_HANDLE_VALUE) {
        return win_error("Opening the sensor bridge diagnostic stream failed");
    }
    const Handle stderr_sink(null);
    std::array handles{request->read.get(), response->write.get(), stderr_sink.get()};
    InheritedHandles inherited;
    if (auto setup = inherited.init(handles); !setup) {
        return std::unexpected(std::move(setup.error()));
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = handles[0];
    startup.StartupInfo.hStdOutput = handles[1];
    startup.StartupInfo.hStdError = handles[2];
    startup.lpAttributeList = inherited.list;
    PROCESS_INFORMATION process{};
    auto state = std::make_unique<State>();
    if (!CreateProcessW(executable->c_str(), command.data(), nullptr, nullptr, TRUE,
                        EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr, directory.c_str(),
                        &startup.StartupInfo, &process)) {
        return win_error("Could not start CyHWInfo.SensorBridge.exe. Deploy the cyhwinfo-lhm directory");
    }
    const Handle thread(process.hThread);
    state->process.reset(process.hProcess);
    state->input = std::move(request->write);
    state->output = std::move(response->read);
    state->timeout = options.timeout;
    request->read.reset();
    response->write.reset();

    auto hello = state->frame();
    if (!hello) {
        return std::unexpected(std::move(hello.error()));
    }
    try {
        FrameReader body(*hello);
        const auto kind = body.number<std::uint8_t>();
        if (kind == error_frame) {
            return managed_error(body);
        }
        if (kind != ready_frame || body.number<std::uint32_t>() != protocol_version || body.remaining() != 0) {
            throw InvalidFrame{};
        }
    } catch (const InvalidFrame&) {
        return error(Status::error, "Incompatible LibreHardwareMonitor sensor bridge protocol");
    }
    return Sampler(std::move(state));
}

Result<Sample> Sampler::sample() {
    if (!state_) {
        return error(Status::error, "Sensor sampler has been moved");
    }
    const std::lock_guard lock(state_->mutex);
    if (!state_->process) {
        return error(Status::error, "Sensor sampler is closed");
    }
    constexpr char request = 'S';
    DWORD written = 0;
    if (!WriteFile(state_->input.get(), &request, sizeof(request), &written, nullptr) || written != sizeof(request)) {
        auto failure = win_error("Sending the sensor bridge request failed");
        state_->stop();
        return failure;
    }
    auto bytes = state_->frame();
    if (!bytes) {
        state_->stop();
        return std::unexpected(std::move(bytes.error()));
    }
    try {
        FrameReader body(*bytes);
        const auto kind = body.number<std::uint8_t>();
        if (kind == error_frame) {
            return managed_error(body);
        }
        if (kind != sample_frame) {
            throw InvalidFrame{};
        }
        Sample result{
            std::chrono::system_clock::now(), body.flag(), body.flag(), body.flag(), group(body), group(body), group(body)
        };
        if (body.remaining() != 0) {
            throw InvalidFrame{};
        }
        return result;
    } catch (const InvalidFrame&) {
        state_->stop();
        return error(Status::error, "Invalid LibreHardwareMonitor sensor response");
    }
}

} // namespace cyhwinfo::sensors
