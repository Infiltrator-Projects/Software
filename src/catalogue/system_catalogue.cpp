// SPDX-License-Identifier: GPL-3.0-or-later
#include "catalogue/system_catalogue.hpp"

#include <appstream.h>
#include <glib.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <filesystem>
#include <iterator>
#include <set>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace infiltrator::software {
namespace {

std::string text_or_empty(const gchar *text)
{
    return text == nullptr ? std::string{} : std::string{text};
}

std::string category_for(AsComponent *component)
{
    static constexpr const char *priority[] = {
        "Game", "Graphics", "AudioVideo", "Network", "Office",
        "Development", "Science", "Education", "Utility", "System",
        "Settings"
    };
    static constexpr const char *display[] = {
        "Games", "Graphics", "Sound & Video", "Internet", "Office",
        "Programming", "Science & Education", "Science & Education",
        "Accessories", "System Tools", "System Tools"
    };

    for (std::size_t i = 0U; i < std::size(priority); ++i) {
        if (as_component_has_category(component, priority[i])) {
            return display[i];
        }
    }

    return "Other";
}

void read_icon(AsComponent *component, PackageRecord &record)
{
    GPtrArray *icons = as_component_get_icons(component);
    if (icons == nullptr) {
        return;
    }

    for (guint i = 0U; i < icons->len; ++i) {
        auto *icon = AS_ICON(g_ptr_array_index(icons, i));
        if (icon == nullptr) {
            continue;
        }

        const AsIconKind kind = as_icon_get_kind(icon);
        const char *filename = as_icon_get_filename(icon);
        const char *name = as_icon_get_name(icon);

        if ((kind == AS_ICON_KIND_CACHED || kind == AS_ICON_KIND_LOCAL) &&
            filename != nullptr && *filename != '\0' &&
            std::filesystem::exists(filename)) {
            record.cached_icon_path = filename;
            return;
        }

        if (record.icon_name.empty() &&
            (kind == AS_ICON_KIND_STOCK || kind == AS_ICON_KIND_LOCAL) &&
            name != nullptr && *name != '\0') {
            record.icon_name = name;
        }
    }
}

bool supported_kind(const AsComponentKind kind) noexcept
{
    return kind == AS_COMPONENT_KIND_DESKTOP_APP ||
           kind == AS_COMPONENT_KIND_CONSOLE_APP ||
           kind == AS_COMPONENT_KIND_WEB_APP;
}

std::filesystem::path flatpak_app_root(const bool user)
{
    if (!user) {
        return "/var/lib/flatpak/app";
    }

    const char *data = g_get_user_data_dir();
    if (data == nullptr || *data == '\0') {
        return {};
    }
    return std::filesystem::path(data) / "flatpak/app";
}

std::unordered_set<std::string> flatpak_installed_ids(
    const bool user)
{
    std::unordered_set<std::string> ids;
    const std::filesystem::path root =
        flatpak_app_root(user);
    if (root.empty()) {
        return ids;
    }

    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec) || ec) {
        return ids;
    }

    for (const auto &entry :
         std::filesystem::directory_iterator(root, ec)) {
        if (ec) {
            break;
        }
        if (entry.is_directory()) {
            ids.insert(entry.path().filename().string());
        }
    }
    return ids;
}

std::vector<PackageRecord> flatpak_installed_records()
{
    std::vector<PackageRecord> result;
    for (const bool user : {false, true}) {
        const auto installed =
            flatpak_installed_ids(user);
        for (const std::string &id : installed) {
            PackageRecord record;
            record.id =
                std::string("flatpak:") +
                (user ? "user:" : "system:") +
                id;
            record.name = id;
            record.package_name = id;
            record.category = "Flatpak";
            record.kind = PackageKind::application;
            record.channel = Channel::stable;
            record.state = InstallState::installed;
            record.installed_version = "Flatpak";
            record.source =
                user ? "Flatpak User" : "Flatpak System";
            result.emplace_back(std::move(record));
        }
    }

    std::sort(
        result.begin(),
        result.end(),
        [](const PackageRecord &left,
           const PackageRecord &right) {
            if (left.name != right.name) {
                return left.name < right.name;
            }
            return left.source < right.source;
        });
    return result;
}


std::vector<std::string_view> split_tabs(const std::string_view line)
{
    std::vector<std::string_view> fields;
    std::size_t start = 0U;
    for (;;) {
        const std::size_t tab = line.find('\t', start);
        if (tab == std::string_view::npos) {
            fields.emplace_back(line.substr(start));
            return fields;
        }
        fields.emplace_back(line.substr(start, tab - start));
        start = tab + 1U;
    }
}

bool run_flatpak_remote_ls(
    const bool user,
    std::string &output)
{
    output.clear();

    if (access("/usr/bin/flatpak", X_OK) != 0 &&
        access("/bin/flatpak", X_OK) != 0) {
        return true;
    }

    int pipe_fd[2]{};
    if (pipe(pipe_fd) != 0) {
        return false;
    }

    const pid_t child = fork();
    if (child < 0) {
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return false;
    }

    if (child == 0) {
        close(pipe_fd[0]);
        if (dup2(pipe_fd[1], STDOUT_FILENO) < 0 ||
            dup2(pipe_fd[1], STDERR_FILENO) < 0) {
            _exit(127);
        }
        close(pipe_fd[1]);

        execlp(
            "flatpak",
            "flatpak",
            "remote-ls",
            user ? "--user" : "--system",
            "--app",
            "--columns=application,name,description,branch,origin",
            static_cast<char *>(nullptr));
        _exit(127);
    }

    close(pipe_fd[1]);
    std::array<char, 8192> buffer{};
    for (;;) {
        const ssize_t count =
            read(pipe_fd[0], buffer.data(), buffer.size());
        if (count > 0) {
            output.append(
                buffer.data(),
                static_cast<std::size_t>(count));
            continue;
        }
        if (count == 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        close(pipe_fd[0]);
        int ignored = 0;
        (void)waitpid(child, &ignored, 0);
        return false;
    }
    close(pipe_fd[0]);

    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) {
            continue;
        }
        return false;
    }

    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

void append_flatpak_remote_records(
    const std::string &output,
    const std::string_view installation,
    const std::unordered_set<std::string> &installed,
    std::vector<PackageRecord> &result)
{
    std::size_t start = 0U;
    while (start < output.size()) {
        const std::size_t end = output.find('\n', start);
        const std::string_view line{
            output.data() + start,
            (end == std::string::npos ? output.size() : end) - start};

        const auto fields = split_tabs(line);
        if (fields.size() >= 5U &&
            !fields[0].empty() &&
            !fields[1].empty()) {
            PackageRecord record;
            const bool user = installation == "User";
            record.id =
                std::string("flatpak:") +
                (user ? "user:" : "system:") +
                std::string(fields[0]);
            record.package_name.assign(fields[0]);
            record.name.assign(fields[1]);
            record.description.assign(fields[2]);
            record.summary = record.description;
            record.available_version.assign(fields[3]);
            record.source =
                "Flatpak " + std::string(installation);
            if (!fields[4].empty()) {
                record.repository_origin.assign(fields[4]);
                record.source +=
                    " · " + std::string(fields[4]);
            }
            record.category = "Flatpak";
            record.kind = PackageKind::application;
            record.channel = Channel::stable;

            if (installed.find(record.package_name) != installed.end()) {
                record.state = InstallState::installed;
                record.installed_version =
                    record.available_version.empty()
                        ? "Flatpak"
                        : record.available_version;
            }

            if (valid_identity(record)) {
                result.emplace_back(std::move(record));
            }
        }

        if (end == std::string::npos) {
            break;
        }
        start = end + 1U;
    }
}

std::vector<PackageRecord> flatpak_remote_records(
    std::string &warning)
{
    warning.clear();
    std::vector<PackageRecord> result;
    if (access("/usr/bin/flatpak", X_OK) != 0 &&
        access("/bin/flatpak", X_OK) != 0) {
        return result;
    }

    const auto system_installed =
        flatpak_installed_ids(false);
    const auto user_installed =
        flatpak_installed_ids(true);

    std::string system_output;
    std::string user_output;
    const bool system_ok =
        run_flatpak_remote_ls(false, system_output);
    const bool user_ok =
        run_flatpak_remote_ls(true, user_output);

    if (system_ok) {
        append_flatpak_remote_records(
            system_output,
            "System",
            system_installed,
            result);
    } else {
        warning =
            "System Flatpak catalogue is unavailable.";
    }
    if (user_ok) {
        append_flatpak_remote_records(
            user_output,
            "User",
            user_installed,
            result);
    } else {
        if (!warning.empty()) {
            warning += " ";
        }
        warning +=
            "User Flatpak catalogue is unavailable.";
    }

    /*
     * System and user installations are separate targets.  Keep both when
     * the same application is available in each scope, and only collapse
     * accidental duplicates within the same scoped identity.
     */
    std::unordered_set<std::string> seen;
    result.erase(
        std::remove_if(
            result.begin(),
            result.end(),
            [&](const PackageRecord &record) {
                return !seen.insert(record.id).second;
            }),
        result.end());
    return result;
}

PackageRecord convert_component(AsComponent *component)
{
    PackageRecord record;
    if (component == nullptr) {
        return record;
    }

    const AsComponentKind kind = as_component_get_kind(component);
    if (!supported_kind(kind)) {
        return record;
    }

    const std::string component_id =
        text_or_empty(as_component_get_id(component));
    const std::string name =
        text_or_empty(as_component_get_name(component));
    if (component_id.empty() || name.empty()) {
        return record;
    }

    const gchar *package_name =
        as_component_get_pkgname(component);
    if (package_name == nullptr || *package_name == '\0') {
        return record;
    }

    record.id = "apt:" + std::string(package_name);
    record.name = name;
    record.package_name = package_name;
    record.category = category_for(component);
    record.summary =
        text_or_empty(as_component_get_summary(component));
    record.description = record.summary;
    record.publisher =
        text_or_empty(as_component_get_project_group(component));
    record.source_url =
        text_or_empty(
            as_component_get_url(
                component, AS_URL_KIND_HOMEPAGE));
    record.kind = PackageKind::application;
    record.channel = Channel::stable;
    record.state = InstallState::not_installed;

    const std::string origin =
        text_or_empty(as_component_get_origin(component));
    record.source = origin.empty()
        ? "APT repository"
        : "APT · " + origin;

    if (record.publisher.empty()) {
        record.publisher = origin;
    }

    read_icon(component, record);
    return record;
}

} // namespace

std::string_view SystemCatalogue::name() const noexcept
{
    return "System AppStream";
}

CatalogueSnapshot SystemCatalogue::refresh(std::string &error)
{
    error.clear();
    CatalogueSnapshot snapshot;
    snapshot.source = "System AppStream + Flatpak CLI";

    std::unordered_set<std::string> seen;
    AsPool *pool = as_pool_new();
    if (pool == nullptr) {
        error = "Unable to create the system AppStream pool.";
    } else {
        as_pool_set_flags(
            pool,
            static_cast<AsPoolFlags>(
                AS_POOL_FLAG_LOAD_OS_CATALOG |
                AS_POOL_FLAG_LOAD_OS_METAINFO));

        GError *load_error = nullptr;
        if (!as_pool_load(pool, nullptr, &load_error)) {
            error = load_error != nullptr
                ? load_error->message
                : "Unable to load system AppStream metadata.";
            g_clear_error(&load_error);
        } else {
            AsComponentBox *components =
                as_pool_get_components(pool);
            if (components != nullptr) {
                const guint count =
                    as_component_box_get_size(components);
                snapshot.records.reserve(
                    static_cast<std::size_t>(count));

                for (guint i = 0U; i < count; ++i) {
                    AsComponent *component =
                        as_component_box_index_safe(
                            components, i);
                    PackageRecord record =
                        convert_component(component);
                    if (!valid_identity(record) ||
                        record.package_name.empty()) {
                        continue;
                    }
                    if (!seen.insert(record.id).second) {
                        continue;
                    }
                    snapshot.records.emplace_back(
                        std::move(record));
                }
            }
        }
        g_object_unref(pool);
    }

    std::string flatpak_warning;
    std::vector<PackageRecord> flatpaks =
        flatpak_remote_records(flatpak_warning);
    for (PackageRecord &record : flatpaks) {
        if (seen.insert(record.id).second) {
            snapshot.records.emplace_back(std::move(record));
        }
    }
    if (!flatpak_warning.empty()) {
        if (!error.empty()) {
            error += " ";
        }
        error += flatpak_warning;
    }

    std::sort(
        snapshot.records.begin(),
        snapshot.records.end(),
        [](const PackageRecord &left, const PackageRecord &right) {
            if (left.name != right.name) {
                return left.name < right.name;
            }
            return left.source < right.source;
        });

    return snapshot;
}

std::vector<PackageRecord>
SystemCatalogue::installed_flatpaks(
    std::string &error) const
{
    error.clear();
    return flatpak_installed_records();
}

} // namespace infiltrator::software
