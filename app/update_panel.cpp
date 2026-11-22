#include "update_panel.hpp"

#include <cyui/Core.h>

#include <chrono>
#include <utility>

namespace cyhwinfo::updates {
namespace {

using namespace std::chrono_literals;

void message(const char* text) {
    ImGui::TextWrapped("%s", cyui::tr(text).c_str());
}

} // namespace

Panel::Panel() {
    cyui::registerTranslations("fr", {
        {"Updates", "Mises à jour"}, {"Update available", "Mise à jour disponible"},
        {"Installed version", "Version actuelle"}, {"Available version", "Version disponible"},
        {"Check for updates", "Vérifier les mises à jour"}, {"Close", "Fermer"},
        {"Checking GitHub for updates…", "Recherche de mises à jour sur GitHub…"},
        {"No stable release is available on GitHub.", "Aucune version stable n’est disponible sur GitHub."},
        {"CyHWInfo is up to date.", "CyHWInfo est à jour."},
        {"Could not check for updates. Try again later.", "La recherche a échoué. Réessayez plus tard."},
        {"Downloading and verifying the Windows installer…", "Téléchargement et vérification de l’installateur Windows…"},
        {"Windows Setup has opened.", "L’installateur Windows est ouvert."},
        {"Could not download, verify or start the installer.", "Impossible de télécharger, de vérifier ou de lancer l’installateur."},
        {"Download and install", "Télécharger et installer"},
        {"Open Windows installer", "Ouvrir l’installateur Windows"},
        {"CyHWInfo will close for the update and reopen when installation succeeds.",
         "CyHWInfo se fermera pendant la mise à jour et se rouvrira après l’installation."},
        {"Run the Windows installer to switch from this copy to an installed copy.",
         "Utilisez l’installateur Windows pour passer de cette copie à une version installée."},
    });
    start_check();
}

void Panel::start_check() {
    release_.reset();
    phase_ = Phase::checking;
    check_ = std::async(std::launch::async, check);
}

bool Panel::draw() {
    if (check_.valid() && check_.wait_for(0s) == std::future_status::ready) {
        try {
            auto result = check_.get();
            release_ = std::move(result.release);
            switch (result.status) {
            case CheckStatus::no_release: phase_ = Phase::no_release; break;
            case CheckStatus::up_to_date: phase_ = Phase::current; break;
            case CheckStatus::available: phase_ = Phase::available; break;
            }
        } catch (...) {
            phase_ = Phase::check_failed;
        }
    }
    if (installer_.valid() && installer_.wait_for(0s) == std::future_status::ready) {
        try {
            installer_.get();
            if (installed_) {
                return true;
            }
            phase_ = Phase::opened;
        } catch (...) {
            phase_ = Phase::install_failed;
        }
    }

    if (ImGui::BeginChild("##updateBar", {0, cyui::dp(37)}, ImGuiChildFlags_AlwaysUseWindowPadding)) {
        const auto label = cyui::tr(release_ ? "Update available" : "Updates") + "##appUpdates";
        if (cyui::button(label.c_str())) {
            ImGui::OpenPopup("appUpdateStatus");
        }
        ImGui::SetNextWindowSize({cyui::dp(440), 0}, ImGuiCond_Appearing);
        if (ImGui::BeginPopup("appUpdateStatus")) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cyui::dp(410));
            ImGui::Text("%s %s", cyui::tr("Installed version").c_str(), CYHWINFO_APP_VERSION);
            if (release_) {
                ImGui::Text("%s %s", cyui::tr("Available version").c_str(), release_->version.c_str());
            }
            switch (phase_) {
            case Phase::checking: message("Checking GitHub for updates…"); break;
            case Phase::no_release: message("No stable release is available on GitHub."); break;
            case Phase::current: message("CyHWInfo is up to date."); break;
            case Phase::check_failed: message("Could not check for updates. Try again later."); break;
            case Phase::opening: message("Downloading and verifying the Windows installer…"); break;
            case Phase::opened: message("Windows Setup has opened."); break;
            case Phase::install_failed: message("Could not download, verify or start the installer."); break;
            case Phase::available:
                message(installed_ ? "CyHWInfo will close for the update and reopen when installation succeeds."
                                   : "Run the Windows installer to switch from this copy to an installed copy.");
                break;
            }
            ImGui::PopTextWrapPos();

            const bool busy = phase_ == Phase::checking || phase_ == Phase::opening;
            ImGui::BeginDisabled(busy);
            if (cyui::button(cyui::trLabel("Check for updates").c_str())) {
                start_check();
            }
            if (phase_ == Phase::available || phase_ == Phase::install_failed) {
                ImGui::SameLine();
                if (cyui::button(cyui::trLabel(installed_ ? "Download and install" : "Open Windows installer").c_str())) {
                    phase_ = Phase::opening;
                    installer_ = std::async(std::launch::async, launch_installer, *release_, installed_, cyui::locale() == "fr");
                }
            }
            ImGui::EndDisabled();
            if (cyui::button(cyui::trLabel("Close").c_str())) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();
    return false;
}

} // namespace cyhwinfo::updates
