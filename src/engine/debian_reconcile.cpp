// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/debian_reconcile.hpp"

#include "engine/debian_installed_state.hpp"
#include "engine/debian_preferences.hpp"
#include "engine/package_policy.hpp"
#include "engine/debian_repository.hpp"
#include "engine/debian_source_configuration.hpp"

#include <glib.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <tuple>
#include <unistd.h>
#include <utility>
#include <vector>

namespace infiltrator::software {
namespace {

bool architecture_enabled(
    const DebianRepositorySource &source,
    const std::string_view architecture)
{
    if (source.architectures.empty()) return true;
    return std::find(
               source.architectures.begin(),
               source.architectures.end(),
               architecture) != source.architectures.end();
}

void checksum_field(GChecksum *checksum, const std::string_view value)
{
    if (checksum == nullptr) return;
    g_checksum_update(
        checksum,
        reinterpret_cast<const guchar *>(value.data()),
        value.size());
    static constexpr guchar separator = 0U;
    g_checksum_update(checksum, &separator, 1U);
}

std::string build_fingerprint(
    const std::vector<DebianRepositorySource> &sources,
    const std::vector<DebianPackageVersion> &available,
    const std::string_view architecture)
{
    GChecksum *checksum = g_checksum_new(G_CHECKSUM_SHA256);
    if (checksum == nullptr) return {};

    checksum_field(checksum, architecture);
    for (const DebianRepositorySource &source : sources) {
        checksum_field(checksum, source.id);
        checksum_field(checksum, source.uri);
        checksum_field(checksum, source.suite);
        for (const std::string &component : source.components) checksum_field(checksum, component);
        for (const std::string &keyring : source.keyrings) checksum_field(checksum, keyring);
    }
    for (const DebianPackageVersion &package : available) {
        checksum_field(checksum, package.package);
        checksum_field(checksum, package.version);
        checksum_field(checksum, package.architecture);
        checksum_field(checksum, package.source);
        checksum_field(checksum, package.filename);
        checksum_field(checksum, package.sha256);
        checksum_field(
            checksum,
            std::to_string(package.pin_priority));
    }

    const char *digest = g_checksum_get_string(checksum);
    const std::string result = digest == nullptr ? "" : digest;
    g_checksum_free(checksum);
    return result;
}

bool same_available(
    const DebianPackageVersion &left,
    const DebianPackageVersion &right)
{
    return std::tie(
               left.package, left.version, left.architecture, left.source, left.filename) ==
           std::tie(
               right.package, right.version, right.architecture, right.source, right.filename);
}

} // namespace

bool DebianReconciler::reconcile(
    PackageStateStore &store,
    const std::string_view target_architecture,
    const std::string &cache_directory,
    std::uint64_t &published_generation,
    std::string &error)
{
    published_generation = 0U;
    error.clear();
    if (target_architecture.empty()) {
        error = "Unable to determine the native Debian architecture.";
        return false;
    }

    std::vector<DebianRepositorySource> configured =
        DebianSourceConfiguration::read(error);
    if (!error.empty()) return false;

    std::string preferences_error;
    const DebianAptPreferences host_preferences =
        DebianAptPreferences::read(preferences_error);
    if (!preferences_error.empty()) {
        error =
            "Unable to read APT package preferences: " +
            preferences_error;
        return false;
    }

    /*
     * The engine consumes an ordered policy stack rather than depending on
     * APT preferences as its permanent policy model. Today the host APT
     * adapter is the only explicit provider. Infiltrator distribution policy
     * can later be inserted ahead of it for migrated packages while the host
     * adapter continues to protect packages that still belong to the base OS.
     */
    DebianPolicyStack policy;
    policy.add(host_preferences);

    std::vector<DebianRepositorySource> active;
    std::vector<DebianPackageVersion> available;
    for (const DebianRepositorySource &source : configured) {
        if (!architecture_enabled(source, target_architecture)) continue;

        std::string refresh_error;
        DebianRepositorySnapshot snapshot =
            DebianRepositoryRefresh::refresh(
                source, target_architecture, cache_directory, refresh_error);
        if (!refresh_error.empty()) {
            error = "Unable to reconcile " + source.uri + " " +
                source.suite + ": " + refresh_error;
            return false;
        }
        for (DebianPackageVersion &package : snapshot.packages) {
            const DebianPolicyDecision decision =
                policy.evaluate(package);
            package.pin_priority = decision.priority;
        }

        active.emplace_back(source);
        available.insert(
            available.end(),
            std::make_move_iterator(snapshot.packages.begin()),
            std::make_move_iterator(snapshot.packages.end()));
    }

    if (active.empty()) {
        error = "No configured Debian repository applies to architecture " +
            std::string(target_architecture) + ".";
        return false;
    }

    std::sort(
        available.begin(),
        available.end(),
        [](const DebianPackageVersion &left, const DebianPackageVersion &right) {
            return std::tie(
                       left.package, left.version, left.architecture, left.source, left.filename) <
                   std::tie(
                       right.package, right.version, right.architecture, right.source, right.filename);
        });
    available.erase(
        std::unique(available.begin(), available.end(), same_available),
        available.end());

    std::string installed_error;
    std::vector<PackageRecord> installed =
        DebianInstalledState::read(installed_error);
    if (!installed_error.empty()) {
        error = installed_error;
        return false;
    }

    const std::string fingerprint =
        build_fingerprint(active, available, target_architecture);
    if (fingerprint.empty()) {
        error = "Unable to calculate package-state source fingerprint.";
        return false;
    }

    return store.publish(
        installed, available, fingerprint, published_generation, error);
}

std::string default_repository_cache_path()
{
    const char *override_path =
        std::getenv("INFILTRATOR_SOFTWARE_REPOSITORY_CACHE");
    if (override_path != nullptr && *override_path != '\0') return override_path;

    const char *xdg_cache = std::getenv("XDG_CACHE_HOME");
    if (xdg_cache != nullptr && *xdg_cache != '\0') {
        return (std::filesystem::path(xdg_cache) /
            "infiltrator/software/repositories").string();
    }

    const char *home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        return (std::filesystem::path(home) /
            ".cache/infiltrator/software/repositories").string();
    }

    return (std::filesystem::temp_directory_path() /
        ("infiltrator-software-" + std::to_string(getuid())) /
        "repositories").string();
}

} // namespace infiltrator::software
