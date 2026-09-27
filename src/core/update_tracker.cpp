// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/update_tracker.hpp"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace infiltrator::software {
namespace {

struct TrackedUpdate {
    std::int64_t first_day{0};
    std::int64_t last_day{0};
    unsigned days_seen{0U};
};

struct TrackerState {
    std::int64_t last_notified_unix{0};
    std::map<std::string, TrackedUpdate> updates;
};

bool parse_i64(
    const std::string_view value,
    std::int64_t &out)
{
    const auto parsed =
        std::from_chars(
            value.data(),
            value.data() + value.size(),
            out);
    return parsed.ec == std::errc{} &&
           parsed.ptr == value.data() + value.size();
}

bool parse_unsigned(
    const std::string_view value,
    unsigned &out)
{
    const auto parsed =
        std::from_chars(
            value.data(),
            value.data() + value.size(),
            out);
    return parsed.ec == std::errc{} &&
           parsed.ptr == value.data() + value.size();
}

std::vector<std::string_view> split_tabs(
    const std::string &line)
{
    std::vector<std::string_view> fields;
    std::size_t start = 0U;
    for (;;) {
        const std::size_t tab = line.find('\t', start);
        if (tab == std::string::npos) {
            fields.emplace_back(
                line.data() + start,
                line.size() - start);
            break;
        }
        fields.emplace_back(
            line.data() + start,
            tab - start);
        start = tab + 1U;
    }
    return fields;
}

bool load_tracker(
    const std::string &path,
    TrackerState &state,
    std::string &error)
{
    state = TrackerState{};
    error.clear();

    std::ifstream input(path);
    if (!input) {
        if (!std::filesystem::exists(path)) {
            return true;
        }
        error = "Unable to read update notification tracker.";
        return false;
    }

    std::string line;
    bool saw_version = false;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        const auto fields = split_tabs(line);
        if (!saw_version) {
            saw_version = true;
            if (fields.size() != 2U ||
                fields[0] != "version" ||
                fields[1] != "1") {
                error =
                    "Unsupported update notification tracker format.";
                return false;
            }
            continue;
        }

        if (fields[0] == "last-notified" &&
            fields.size() == 2U) {
            (void)parse_i64(
                fields[1],
                state.last_notified_unix);
            continue;
        }

        if (fields[0] != "package" ||
            fields.size() != 5U ||
            fields[1].empty()) {
            continue;
        }

        TrackedUpdate tracked;
        if (!parse_i64(fields[2], tracked.first_day) ||
            !parse_i64(fields[3], tracked.last_day) ||
            !parse_unsigned(fields[4], tracked.days_seen)) {
            continue;
        }
        state.updates.emplace(
            std::string(fields[1]),
            tracked);
    }

    return true;
}

bool save_tracker(
    const std::string &path,
    const TrackerState &state,
    std::string &error)
{
    error.clear();
    const std::filesystem::path target(path);
    std::error_code ec;
    std::filesystem::create_directories(
        target.parent_path(), ec);
    if (ec) {
        error =
            "Unable to create update notification tracker directory: " +
            ec.message();
        return false;
    }

    const std::filesystem::path temporary =
        target.string() + ".tmp." +
        std::to_string(
            static_cast<unsigned long long>(getpid()));
    std::ofstream output(
        temporary,
        std::ios::out | std::ios::trunc);
    if (!output) {
        error = "Unable to write update notification tracker.";
        return false;
    }

    output << "version\t1\n";
    output
        << "last-notified\t"
        << state.last_notified_unix
        << '\n';
    for (const auto &[identity, tracked] : state.updates) {
        if (identity.find('\t') != std::string::npos ||
            identity.find('\n') != std::string::npos ||
            identity.find('\r') != std::string::npos) {
            continue;
        }
        output
            << "package\t"
            << identity << '\t'
            << tracked.first_day << '\t'
            << tracked.last_day << '\t'
            << tracked.days_seen << '\n';
    }

    output.close();
    if (!output) {
        std::filesystem::remove(temporary, ec);
        error =
            "Unable to finish writing update notification tracker.";
        return false;
    }

    std::filesystem::rename(temporary, target, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        error =
            "Unable to publish update notification tracker: " +
            ec.message();
        return false;
    }
    return true;
}

std::string update_identity(const PackageRecord &package)
{
    if (!package.source_package.empty()) {
        return package.source_package;
    }
    if (!package.package_name.empty()) {
        return package.package_name;
    }
    return package.id;
}

bool relevant_update(
    const PackageRecord &package,
    const SoftwarePreferences &preferences)
{
    if (!preferences.notifications_security_only) {
        return true;
    }
    return package.security_update ||
           package.kind == PackageKind::kernel;
}

unsigned saturating_days(
    const std::int64_t earlier_day,
    const std::int64_t current_day)
{
    if (current_day <= earlier_day) {
        return 0U;
    }
    const std::int64_t difference =
        current_day - earlier_day;
    return difference >
           static_cast<std::int64_t>(
               std::numeric_limits<unsigned>::max())
        ? std::numeric_limits<unsigned>::max()
        : static_cast<unsigned>(difference);
}

} // namespace

std::string update_tracker_path()
{
    const char *override_path =
        std::getenv("INFILTRATOR_SOFTWARE_UPDATE_TRACKER_PATH");
    if (override_path != nullptr && *override_path != '\0') {
        return override_path;
    }

    const char *state_home =
        std::getenv("XDG_STATE_HOME");
    if (state_home != nullptr &&
        *state_home != '\0') {
        return (
            std::filesystem::path(state_home) /
            "infiltrator-software" /
            "update-tracker.tsv").string();
    }

    const char *home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        return (
            std::filesystem::path(home) /
            ".local" / "state" /
            "infiltrator-software" /
            "update-tracker.tsv").string();
    }
    return {};
}

bool evaluate_update_notification(
    const std::vector<PackageRecord> &updates,
    const SoftwarePreferences &preferences,
    const std::int64_t now_unix,
    const std::int64_t last_successful_update_unix,
    const std::string &path,
    UpdateNotificationResult &result,
    std::string &error)
{
    result = UpdateNotificationResult{};
    error.clear();

    if (path.empty() || now_unix <= 0) {
        error =
            "Update notification tracker path or time is unavailable.";
        return false;
    }

    TrackerState state;
    if (!load_tracker(path, state, error)) {
        return false;
    }

    const std::int64_t current_day =
        now_unix / 86400;
    std::set<std::string> current;

    for (const PackageRecord &package : updates) {
        if (!relevant_update(package, preferences)) {
            continue;
        }

        const std::string identity =
            update_identity(package);
        if (identity.empty()) {
            continue;
        }
        current.insert(identity);

        auto [position, inserted] =
            state.updates.emplace(
                identity,
                TrackedUpdate{
                    current_day,
                    current_day,
                    1U});
        TrackedUpdate &tracked = position->second;
        if (!inserted &&
            tracked.last_day < current_day) {
            tracked.last_day = current_day;
            if (tracked.days_seen <
                std::numeric_limits<unsigned>::max()) {
                ++tracked.days_seen;
            }
        }

        result.oldest_age_days =
            std::max(
                result.oldest_age_days,
                saturating_days(
                    tracked.first_day,
                    current_day));
        result.maximum_seen_days =
            std::max(
                result.maximum_seen_days,
                tracked.days_seen);
    }

    for (auto it = state.updates.begin();
         it != state.updates.end();) {
        if (current.find(it->first) == current.end()) {
            it = state.updates.erase(it);
        } else {
            ++it;
        }
    }

    result.relevant_updates = current.size();

    bool notify =
        preferences.notifications_enabled &&
        result.relevant_updates > 0U &&
        (result.maximum_seen_days >=
             preferences.notify_max_days ||
         result.oldest_age_days >=
             preferences.notify_max_age_days);

    const std::int64_t seconds_per_day = 86400;
    if (notify &&
        last_successful_update_unix > 0 &&
        now_unix - last_successful_update_unix <
            static_cast<std::int64_t>(
                preferences.notify_grace_period_days) *
                seconds_per_day) {
        notify = false;
    }

    if (notify &&
        state.last_notified_unix > 0 &&
        now_unix - state.last_notified_unix <
            static_cast<std::int64_t>(
                preferences.notify_days_between) *
                seconds_per_day) {
        notify = false;
    }

    if (notify) {
        state.last_notified_unix = now_unix;
        result.notify = true;
    }

    return save_tracker(path, state, error);
}

} // namespace infiltrator::software
