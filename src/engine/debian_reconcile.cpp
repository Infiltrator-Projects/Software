// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/debian_reconcile.hpp"

#include "engine/debian_installed_state.hpp"
#include "engine/debian_phased_updates.hpp"
#include "engine/debian_preferences.hpp"
#include "engine/package_policy.hpp"
#include "engine/debian_repository.hpp"
#include "engine/debian_source_configuration.hpp"

#include <glib.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <sstream>
#include <tuple>
#include <unistd.h>
#include <utility>
#include <vector>

namespace infiltrator::software {
namespace {

bool security_candidate(
    const DebianPackageVersion &package)
{
    auto lower = [](std::string value) {
        std::transform(
            value.begin(), value.end(), value.begin(),
            [](const unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
        return value;
    };

    const std::string origin = lower(package.release_origin);
    const std::string label = lower(package.release_label);
    const std::string archive = lower(package.release_archive);
    const std::string source = lower(package.source_package);

    if (origin == "ubuntu" &&
        archive.find("-security") != std::string::npos) {
        return true;
    }
    if (origin == "debian" &&
        label.find("security") != std::string::npos) {
        return true;
    }

    // Match Mint Update Manager's long-standing browser-source rule.
    return source == "firefox" ||
           source == "thunderbird" ||
           source == "chromium" ||
           source == "chromium-browser";
}

bool architecture_enabled(
    const DebianRepositorySource &source,
    const std::string_view architecture)
{
    bool enabled =
        source.architectures.empty() ||
        std::find(
            source.architectures.begin(),
            source.architectures.end(),
            architecture) != source.architectures.end();

    if (std::find(
            source.architecture_additions.begin(),
            source.architecture_additions.end(),
            architecture) != source.architecture_additions.end()) {
        enabled = true;
    }
    if (std::find(
            source.architecture_removals.begin(),
            source.architecture_removals.end(),
            architecture) != source.architecture_removals.end()) {
        enabled = false;
    }
    return enabled;
}

void checksum_field(GChecksum *checksum, const std::string_view value)
{
    if (checksum == nullptr) return;
    g_checksum_update(
        checksum,
        reinterpret_cast<const guchar *>(value.data()),
        static_cast<gssize>(value.size()));
    static constexpr guchar separator = 0U;
    g_checksum_update(checksum, &separator, 1U);
}

std::string build_fingerprint(
    const std::vector<DebianRepositorySource> &sources,
    const std::vector<DebianPackageVersion> &available,
    const std::vector<std::string> &architectures)
{
    GChecksum *checksum = g_checksum_new(G_CHECKSUM_SHA256);
    if (checksum == nullptr) return {};

    for (const std::string &architecture : architectures) {
        checksum_field(checksum, architecture);
    }
    for (const DebianRepositorySource &source : sources) {
        checksum_field(checksum, source.id);
        checksum_field(checksum, source.uri);
        checksum_field(checksum, source.suite);
        for (const std::string &component : source.components) checksum_field(checksum, component);
        for (const std::string &keyring : source.keyrings) checksum_field(checksum, keyring);
        for (const std::string &inline_key : source.inline_keys) checksum_field(checksum, inline_key);
        for (const std::string &fingerprint : source.allowed_fingerprints) checksum_field(checksum, fingerprint);
        for (const std::string &item : source.architectures) checksum_field(checksum, item);
        for (const std::string &item : source.architecture_additions) checksum_field(checksum, item);
        for (const std::string &item : source.architecture_removals) checksum_field(checksum, item);
        checksum_field(checksum, source.verify_signatures ? "verify" : "trusted");
        checksum_field(checksum, source.check_valid_until ? "valid-until" : "no-valid-until");
        checksum_field(checksum, source.check_date ? "check-date" : "no-check-date");
        checksum_field(checksum, std::to_string(source.valid_until_min_seconds));
        checksum_field(checksum, std::to_string(source.valid_until_max_seconds));
        checksum_field(checksum, std::to_string(source.date_max_future_seconds));
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
        checksum_field(checksum, package.policy_provider);
        checksum_field(checksum, package.policy_reason);
        checksum_field(checksum, package.release_origin);
        checksum_field(checksum, package.release_archive);
        checksum_field(checksum, package.supported);
        checksum_field(checksum, package.site);
    }

    const char *digest = g_checksum_get_string(checksum);
    const std::string result = digest == nullptr ? "" : digest;
    g_checksum_free(checksum);
    return result;
}

bool configured_architectures(
    const std::string_view native_architecture,
    std::vector<std::string> &architectures,
    std::string &error)
{
    architectures.clear();
    if (native_architecture.empty()) {
        error = "Unable to determine the native Debian architecture.";
        return false;
    }
    architectures.emplace_back(native_architecture);

    const char *override_value =
        std::getenv("INFILTRATOR_SOFTWARE_FOREIGN_ARCHITECTURES");
    std::string foreign;
    if (override_value != nullptr) {
        foreign = override_value;
    } else {
        gchar *program = g_find_program_in_path("dpkg");
        if (program == nullptr) {
            return true;
        }
        gchar *argv[] = {
            program,
            const_cast<gchar *>("--print-foreign-architectures"),
            nullptr};
        gchar *standard_output = nullptr;
        gchar *standard_error = nullptr;
        gint wait_status = 0;
        GError *spawn_error = nullptr;
        const gboolean spawned =
            g_spawn_sync(
                nullptr,
                argv,
                nullptr,
                G_SPAWN_DEFAULT,
                nullptr,
                nullptr,
                &standard_output,
                &standard_error,
                &wait_status,
                &spawn_error);
        const bool exited_ok =
            spawned != FALSE &&
            g_spawn_check_wait_status(
                wait_status, &spawn_error) != FALSE;
        if (!exited_ok) {
            error =
                spawn_error != nullptr &&
                spawn_error->message != nullptr
                    ? std::string(spawn_error->message)
                    : "dpkg --print-foreign-architectures failed.";
            g_free(program);
            g_free(standard_output);
            g_free(standard_error);
            g_clear_error(&spawn_error);
            return false;
        }
        foreign =
            standard_output == nullptr
                ? std::string{}
                : std::string(standard_output);
        g_free(program);
        g_free(standard_output);
        g_free(standard_error);
        g_clear_error(&spawn_error);
    }

    std::istringstream input(foreign);
    std::string architecture;
    while (input >> architecture) {
        if (architecture.empty() ||
            architecture == native_architecture ||
            std::find(
                architectures.begin(),
                architectures.end(),
                architecture) != architectures.end()) {
            continue;
        }
        architectures.emplace_back(std::move(architecture));
    }
    std::sort(architectures.begin(), architectures.end());
    return true;
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

    std::vector<std::string> architectures;
    if (!configured_architectures(
            target_architecture,
            architectures,
            error)) {
        return false;
    }

    std::vector<DebianRepositorySource> configured =
        DebianSourceConfiguration::read(error);
    if (!error.empty()) return false;

    std::string installed_error;
    std::vector<PackageRecord> installed =
        DebianInstalledState::read(installed_error);
    if (!installed_error.empty()) {
        error = installed_error;
        return false;
    }

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
    std::string phased_error;
    const DebianPhasedUpdatesPolicy phased_updates =
        DebianPhasedUpdatesPolicy::read(phased_error);
    if (!phased_error.empty()) {
        error =
            "Unable to read phased-update policy: " +
            phased_error;
        return false;
    }

    DebianPolicyStack policy;
    policy.add(phased_updates);
    policy.add(host_preferences);

    std::vector<DebianRepositorySource> active;
    std::vector<DebianPackageVersion> available;
    for (const DebianRepositorySource &source : configured) {
        bool source_active = false;
        for (const std::string &architecture : architectures) {
            if (!architecture_enabled(source, architecture)) {
                continue;
            }

            std::string refresh_error;
            DebianRepositorySnapshot snapshot =
                DebianRepositoryRefresh::refresh(
                    source,
                    architecture,
                    cache_directory,
                    refresh_error);
            if (!refresh_error.empty()) {
                error = "Unable to reconcile " + source.uri + " " +
                    source.suite + " for " + architecture + ": " +
                    refresh_error;
                return false;
            }
            if (snapshot.verified_indexes.empty() &&
                snapshot.packages.empty()) {
                continue;
            }

            source_active = true;
            for (DebianPackageVersion &package : snapshot.packages) {
                package.security_update =
                    security_candidate(package);
                const DebianPolicyDecision decision =
                    policy.evaluate(package);
                package.pin_priority = decision.priority;
                package.policy_provider = decision.provider;
                package.policy_reason = decision.reason;
            }

            available.insert(
                available.end(),
                std::make_move_iterator(snapshot.packages.begin()),
                std::make_move_iterator(snapshot.packages.end()));
        }
        if (source_active) {
            active.emplace_back(source);
        }
    }

    if (active.empty()) {
        error =
            "No configured Debian repository applies to any configured "
            "architecture.";
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

    const std::string fingerprint =
        build_fingerprint(active, available, architectures);
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
