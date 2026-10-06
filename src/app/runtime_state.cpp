// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/runtime_state.hpp"

#include <glib.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace infiltrator::software::app {

std::filesystem::path software_update_runtime_state_path()
{
    const char *runtime = g_get_user_runtime_dir();
    if (runtime == nullptr || *runtime == '\0') {
        return {};
    }
    return std::filesystem::path(runtime) /
           "infiltrator-software" / "update-state";
}

void set_software_update_runtime_state(const std::string_view value)
{
    const std::filesystem::path path =
        software_update_runtime_state_path();
    if (path.empty()) {
        return;
    }

    std::error_code ec;
    if (value.empty()) {
        std::filesystem::remove(path, ec);
        return;
    }

    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        return;
    }

    const std::filesystem::path temporary =
        path.string() + ".tmp." +
        std::to_string(
            static_cast<unsigned long long>(getpid()));
    std::ofstream output(
        temporary,
        std::ios::out | std::ios::trunc);
    if (!output) {
        return;
    }
    output << value << '\n';
    output.close();
    if (!output) {
        std::filesystem::remove(temporary, ec);
        return;
    }

    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
    }
}

} // namespace infiltrator::software::app
