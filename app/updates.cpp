#include "updates.hpp"

#include <windows.h>
#include <bcrypt.h>
#include <objbase.h>
#include <winhttp.h>

#include <nlohmann/json.hpp>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace cyhwinfo::updates {
namespace {

namespace fs = std::filesystem;
using namespace std::chrono_literals;

constexpr std::string_view release_path = "/MrMybal/CyHWInfo/releases/download/";
constexpr std::size_t metadata_limit = 1024 * 1024;
constexpr std::size_t installer_limit = 128 * 1024 * 1024;

struct HttpCloser {
    void operator()(void* handle) const { WinHttpCloseHandle(handle); }
};
using HttpHandle = std::unique_ptr<void, HttpCloser>;

struct HandleCloser {
    void operator()(void* handle) const { CloseHandle(handle); }
};
using FileHandle = std::unique_ptr<void, HandleCloser>;

struct Response {
    DWORD status;
    std::string body;
};

Response get(const wchar_t* host, const wchar_t* path, const wchar_t* headers, std::size_t limit) {
    HttpHandle session(WinHttpOpen(L"CyHWInfo", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session || !WinHttpSetTimeouts(session.get(), 5000, 5000, 5000, 8000)) {
        throw std::runtime_error("WinHTTP initialization failed");
    }
    HttpHandle connection(WinHttpConnect(session.get(), host, INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection) {
        throw std::runtime_error("WinHTTP connection failed");
    }
    HttpHandle request(WinHttpOpenRequest(connection.get(), L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                          WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    DWORD no_downgrade = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    DWORD no_auto_logon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
    if (!request || !WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &no_downgrade, sizeof(no_downgrade)) ||
        !WinHttpSetOption(request.get(), WINHTTP_OPTION_AUTOLOGON_POLICY, &no_auto_logon, sizeof(no_auto_logon)) ||
        !WinHttpSendRequest(request.get(), headers, headers ? static_cast<DWORD>(-1) : 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.get(), nullptr)) {
        throw std::runtime_error("GitHub request failed");
    }
    DWORD status = 0;
    DWORD bytes = sizeof(status);
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &bytes, WINHTTP_NO_HEADER_INDEX)) {
        throw std::runtime_error("Missing GitHub response status");
    }
    Response response{status, {}};
    if (status != HTTP_STATUS_OK) {
        return response;
    }
    const auto deadline = std::chrono::steady_clock::now() + 3min;
    std::array<char, 64 * 1024> buffer;
    do {
        if (!WinHttpReadData(request.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &bytes) ||
            bytes > limit - response.body.size() || std::chrono::steady_clock::now() > deadline) {
            throw std::runtime_error("GitHub response incomplete or too large");
        }
        response.body.append(buffer.data(), bytes);
    } while (bytes != 0);
    return response;
}

std::optional<std::array<unsigned, 3>> version_number(std::string_view version) {
    std::array<unsigned, 3> parts{};
    for (auto& part : parts) {
        const auto separator = version.find('.');
        const auto token = version.substr(0, separator);
        const bool last = &part == &parts.back();
        if (token.empty() || (token.size() > 1 && token.front() == '0') ||
            last != (separator == std::string_view::npos)) {
            return std::nullopt;
        }
        const auto [end, error] = std::from_chars(token.data(), token.data() + token.size(), part);
        if (error != std::errc{} || end != token.data() + token.size()) {
            return std::nullopt;
        }
        if (!last) {
            version.remove_prefix(separator + 1);
        }
    }
    return parts;
}

std::array<unsigned char, 32> parse_digest(std::string_view digest) {
    std::array<unsigned char, 32> bytes{};
    constexpr std::string_view prefix = "sha256:";
    if (!digest.starts_with(prefix) || digest.size() != prefix.size() + bytes.size() * 2) {
        throw std::runtime_error("Missing or invalid GitHub asset SHA-256");
    }
    digest.remove_prefix(prefix.size());
    for (auto& byte : bytes) {
        const auto [end, error] = std::from_chars(digest.data(), digest.data() + 2, byte, 16);
        if (error != std::errc{} || end != digest.data() + 2) {
            throw std::runtime_error("Invalid GitHub asset SHA-256");
        }
        digest.remove_prefix(2);
    }
    return bytes;
}

CheckResult parse_release(const std::string& body) {
    const auto metadata = nlohmann::json::parse(body);
    if (metadata.at("draft").get<bool>() || metadata.at("prerelease").get<bool>()) {
        return {CheckStatus::no_release, std::nullopt};
    }
    const auto tag = metadata.at("tag_name").get<std::string>();
    const auto version = tag.starts_with('v') ? tag.substr(1) : std::string{};
    const auto latest = version_number(version);
    const auto current = version_number(CYHWINFO_APP_VERSION);
    if (!latest || !current) {
        throw std::runtime_error("Invalid release version");
    }
    if (*latest <= *current) {
        return {CheckStatus::up_to_date, std::nullopt};
    }

    const auto filename = "CyHWInfo-" + version + "-windows-x64-setup.exe";
    const auto path = std::string(release_path) + tag + "/" + filename;
    const auto url = "https://github.com" + path;
    const auto& assets = metadata.at("assets");
    for (const auto& asset : assets) {
        if (asset.at("name") != filename || asset.at("state") != "uploaded") {
            continue;
        }
        const auto size = asset.at("size").get<std::int64_t>();
        if (size <= 0 || static_cast<std::uint64_t>(size) > installer_limit || asset.at("browser_download_url") != url) {
            throw std::runtime_error("Invalid Windows installer asset");
        }
        return {CheckStatus::available,
                Release{version, std::wstring(path.begin(), path.end()),
                        parse_digest(asset.at("digest").get<std::string>()), static_cast<std::size_t>(size)}};
    }
    throw std::runtime_error("No Windows installer in the latest GitHub release");
}

bool hash_matches(std::string& contents, const Release& release) {
    std::array<unsigned char, 32> digest{};
    BCRYPT_ALG_HANDLE provider = nullptr;
    if (BCryptOpenAlgorithmProvider(&provider, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
        return false;
    }
    const auto status = BCryptHash(provider, nullptr, 0, reinterpret_cast<unsigned char*>(contents.data()),
                                   static_cast<ULONG>(contents.size()), digest.data(), static_cast<ULONG>(digest.size()));
    BCryptCloseAlgorithmProvider(provider, 0);
    return status >= 0 && digest == release.sha256;
}

struct TempInstaller {
    TempInstaller() {
        GUID id{};
        wchar_t name[40]{};
        if (FAILED(CoCreateGuid(&id)) || StringFromGUID2(id, name, static_cast<int>(std::size(name))) == 0) {
            throw std::runtime_error("Installer directory creation failed");
        }
        directory = fs::temp_directory_path() / (std::wstring(L"CyHWInfo-update-") + name);
        executable = directory / L"CyHWInfo-setup.exe";
        if (!fs::create_directory(directory)) {
            throw std::runtime_error("Installer directory is already in use");
        }
    }

    ~TempInstaller() {
        if (!launched) {
            std::error_code error;
            fs::remove(executable, error);
            fs::remove(directory, error);
        }
    }

    fs::path directory;
    fs::path executable;
    bool launched = false;
};

void launch_verified(const Release& release, std::string contents, bool installed, bool french) {
    TempInstaller installer;
    {
        const auto raw = CreateFileW(installer.executable.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                     FILE_ATTRIBUTE_NORMAL, nullptr);
        if (raw == INVALID_HANDLE_VALUE) {
            throw std::runtime_error("Could not create the Windows installer file");
        }
        FileHandle output(raw);
        DWORD written = 0;
        if (!WriteFile(output.get(), contents.data(), static_cast<DWORD>(contents.size()), &written, nullptr) ||
            written != contents.size()) {
            throw std::runtime_error("Could not save the Windows installer");
        }
    }
    const auto raw = CreateFileW(installer.executable.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                 FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (raw == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Could not open the Windows installer for verification");
    }
    FileHandle verified_file(raw);
    LARGE_INTEGER size{};
    DWORD read = 0;
    if (!GetFileSizeEx(verified_file.get(), &size) || size.QuadPart != static_cast<LONGLONG>(release.size) ||
        !ReadFile(verified_file.get(), contents.data(), static_cast<DWORD>(contents.size()), &read, nullptr) ||
        read != contents.size() || !hash_matches(contents, release)) {
        throw std::runtime_error("Windows installer SHA-256 verification failed");
    }

    auto command = L"\"" + installer.executable.wstring() + L"\" /NORESTART /LANG=" + (french ? L"french" : L"english");
    if (installed) {
        command += L" /SP- /SILENT /CYHWINFOUPDATE=1";
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(installer.executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                         installer.directory.c_str(), &startup, &process)) {
        throw std::runtime_error("Could not start the Windows installer");
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    installer.launched = true;
}

} // namespace

CheckResult check() {
#if !defined(_M_X64) && !defined(__x86_64__)
    return {CheckStatus::no_release, std::nullopt};
#else
    const auto response = get(L"api.github.com", L"/repos/MrMybal/CyHWInfo/releases/latest",
                              L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n", metadata_limit);
    if (response.status == HTTP_STATUS_NOT_FOUND) {
        return {CheckStatus::no_release, std::nullopt};
    }
    if (response.status != HTTP_STATUS_OK) {
        throw std::runtime_error("GitHub release check failed");
    }
    return parse_release(response.body);
#endif
}

bool is_installed() {
    std::wstring install_dir(32768, L'\0');
    DWORD size = static_cast<DWORD>(install_dir.size() * sizeof(wchar_t));
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\CyHWInfo", L"InstallDir", RRF_RT_REG_SZ,
                     nullptr, install_dir.data(), &size) != ERROR_SUCCESS || size <= sizeof(wchar_t)) {
        return false;
    }
    install_dir.resize(size / sizeof(wchar_t) - 1);
    std::wstring running_executable(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, running_executable.data(), static_cast<DWORD>(running_executable.size()));
    if (length == 0 || length >= running_executable.size()) {
        return false;
    }
    running_executable.resize(length);
    std::error_code error;
    return fs::equivalent(running_executable, fs::path(install_dir) / L"bin" / L"CyHWInfo.exe", error);
}

void launch_installer(const Release& release, bool installed, bool french) {
    auto response = get(L"github.com", release.download_path.c_str(), WINHTTP_NO_ADDITIONAL_HEADERS, release.size);
    if (response.status != HTTP_STATUS_OK || response.body.size() != release.size) {
        throw std::runtime_error("Windows installer download failed");
    }
    launch_verified(release, std::move(response.body), installed, french);
}

} // namespace cyhwinfo::updates
