#pragma once

#include "updates.hpp"

#include <future>

namespace cyhwinfo::updates {

class Panel {
public:
    Panel();
    bool draw();

private:
    enum class Phase { checking, no_release, current, available, check_failed, opening, opened, install_failed };

    void start_check();

    const bool installed_ = is_installed();
    Phase phase_ = Phase::checking;
    std::optional<Release> release_;
    std::future<CheckResult> check_;
    std::future<void> installer_;
};

} // namespace cyhwinfo::updates
