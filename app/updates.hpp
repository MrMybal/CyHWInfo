#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>

namespace cyhwinfo::updates {

struct Release {
    std::string version;
    std::wstring download_path;
    std::array<unsigned char, 32> sha256;
    std::size_t size;
};

enum class CheckStatus { no_release, up_to_date, available };

struct CheckResult {
    CheckStatus status;
    std::optional<Release> release;
};

CheckResult check();
bool is_installed();
void launch_installer(const Release& release, bool installed, bool french);

} // namespace cyhwinfo::updates
