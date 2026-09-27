// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_EXTERNAL_UPDATES_HPP
#define INFILTRATOR_SOFTWARE_EXTERNAL_UPDATES_HPP

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace infiltrator::software {

enum class ExternalUpdateKind {
    flatpak_application,
    flatpak_runtime,
    cinnamon_applet,
    cinnamon_desklet,
    cinnamon_extension,
    cinnamon_theme,
    nemo_action
};

struct ExternalUpdate {
    ExternalUpdateKind kind{ExternalUpdateKind::flatpak_application};
    std::string backend;
    std::string id;
    std::string name;
    std::string version;
    std::string detail;
    std::string ref;
    bool user_installation{false};
    std::uint64_t download_bytes{0U};
};

using ExternalProgressCallback =
    std::function<void(std::string_view)>;

std::string_view external_update_kind_name(
    ExternalUpdateKind kind) noexcept;

bool discover_flatpak_updates(
    std::vector<ExternalUpdate> &updates,
    std::string &error);

bool discover_cinnamon_updates(
    std::vector<ExternalUpdate> &updates,
    std::string &error);

bool apply_flatpak_updates(
    bool remove_unused,
    bool match_host_theme,
    std::string &error,
    ExternalProgressCallback progress = {});

bool apply_flatpak_updates_selected(
    const std::vector<ExternalUpdate> &selected,
    std::string &error,
    ExternalProgressCallback progress = {});

bool apply_cinnamon_updates(
    std::string &error,
    ExternalProgressCallback progress = {});

} // namespace infiltrator::software

#endif
