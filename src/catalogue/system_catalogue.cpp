// SPDX-License-Identifier: GPL-3.0-or-later
#include "catalogue/system_catalogue.hpp"

#include <appstream.h>

#include <algorithm>
#include <cstdlib>
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

std::unordered_set<std::string> flatpak_installed_ids()
{
    std::unordered_set<std::string> ids;

    const auto collect = [&ids](const std::filesystem::path &root) {
        std::error_code ec;
        if (!std::filesystem::is_directory(root, ec)) {
            return;
        }

        for (const auto &entry :
             std::filesystem::directory_iterator(root, ec)) {
            if (ec || !entry.is_directory()) {
                continue;
            }
            ids.insert(entry.path().filename().string());
        }
    };

    collect("/var/lib/flatpak/app");

    const char *home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        collect(
            std::filesystem::path(home) /
            ".local/share/flatpak/app");
    }

    return ids;
}

PackageRecord convert_component(
    AsComponent *component,
    const std::unordered_set<std::string> &installed_flatpaks)
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

    AsBundle *flatpak_bundle =
        as_component_get_bundle(component, AS_BUNDLE_KIND_FLATPAK);
    const bool is_flatpak = flatpak_bundle != nullptr;

    record.name = name;
    record.category = category_for(component);
    record.summary = text_or_empty(as_component_get_summary(component));
    record.description = record.summary;
    record.publisher =
        text_or_empty(as_component_get_project_group(component));
    record.source_url =
        text_or_empty(as_component_get_url(component, AS_URL_KIND_HOMEPAGE));
    record.kind = PackageKind::application;
    record.channel = Channel::stable;
    record.state = InstallState::not_installed;

    const std::string origin =
        text_or_empty(as_component_get_origin(component));

    if (is_flatpak) {
        const std::string bundle_id =
            text_or_empty(as_bundle_get_id(flatpak_bundle));
        record.id = "flatpak:" + component_id;
        record.package_name = component_id;
        record.asset = bundle_id;
        record.source = origin.empty()
            ? "Flatpak"
            : "Flatpak · " + origin;
        record.available_version =
            text_or_empty(as_component_get_branch(component));
        if (installed_flatpaks.find(component_id) !=
            installed_flatpaks.end()) {
            record.state = InstallState::installed;
            record.installed_version =
                record.available_version.empty()
                    ? "Flatpak"
                    : record.available_version;
        }
    } else {
        gchar *package_name = as_component_get_pkgname(component);
        if (package_name == nullptr || *package_name == '\0') {
            g_free(package_name);
            return PackageRecord{};
        }

        record.package_name = package_name;
        record.id = "apt:" + record.package_name;
        g_free(package_name);

        record.source = origin.empty()
            ? "APT repository"
            : "APT · " + origin;
    }

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
    snapshot.source = "System AppStream + Flatpak";

    AsPool *pool = as_pool_new();
    if (pool == nullptr) {
        error = "Unable to create the system AppStream pool.";
        return snapshot;
    }

    as_pool_set_flags(
        pool,
        static_cast<AsPoolFlags>(
            AS_POOL_FLAG_LOAD_OS_CATALOG |
            AS_POOL_FLAG_LOAD_OS_METAINFO |
            AS_POOL_FLAG_LOAD_FLATPAK));

    GError *load_error = nullptr;
    if (!as_pool_load(pool, nullptr, &load_error)) {
        error = load_error != nullptr
            ? load_error->message
            : "Unable to load system AppStream metadata.";
        g_clear_error(&load_error);
        g_object_unref(pool);
        return snapshot;
    }

    AsComponentBox *components = as_pool_get_components(pool);
    if (components == nullptr) {
        g_object_unref(pool);
        return snapshot;
    }

    const std::unordered_set<std::string> installed_flatpaks =
        flatpak_installed_ids();
    std::unordered_set<std::string> seen;
    const guint count = as_component_box_get_size(components);
    snapshot.records.reserve(static_cast<std::size_t>(count));

    for (guint i = 0U; i < count; ++i) {
        AsComponent *component =
            as_component_box_index_safe(components, i);
        PackageRecord record =
            convert_component(component, installed_flatpaks);
        if (!valid_identity(record) || record.package_name.empty()) {
            continue;
        }
        if (!seen.insert(record.id).second) {
            continue;
        }
        snapshot.records.emplace_back(std::move(record));
    }

    g_object_unref(components);
    g_object_unref(pool);

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

} // namespace infiltrator::software
