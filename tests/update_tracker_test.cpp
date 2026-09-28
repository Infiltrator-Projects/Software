// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/update_tracker.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

int main()
{
    using namespace infiltrator::software;

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("software-update-tracker-" +
         std::to_string(
             static_cast<unsigned long long>(getpid())) +
         ".tsv");

    SoftwarePreferences preferences;
    preferences.notifications_enabled = true;
    preferences.notifications_security_only = true;
    preferences.notify_max_days = 2U;
    preferences.notify_max_age_days = 30U;
    preferences.notify_grace_period_days = 0U;
    preferences.notify_days_between = 2U;

    PackageRecord security;
    security.package_name = "openssl";
    security.source_package = "openssl";
    security.security_update = true;

    PackageRecord ordinary;
    ordinary.package_name = "example";
    ordinary.source_package = "example";

    UpdateNotificationResult result;
    std::string error;

    assert(evaluate_update_notification(
        {security, ordinary},
        preferences,
        86400,
        0,
        path.string(),
        result,
        error));
    assert(!result.notify);
    assert(result.relevant_updates == 1U);

    assert(evaluate_update_notification(
        {security, ordinary},
        preferences,
        86400 * 2,
        0,
        path.string(),
        result,
        error));
    assert(result.notify);
    assert(result.maximum_seen_days == 2U);

    assert(evaluate_update_notification(
        {security},
        preferences,
        86400 * 3,
        0,
        path.string(),
        result,
        error));
    assert(!result.notify);

    assert(evaluate_update_notification(
        {},
        preferences,
        86400 * 4,
        0,
        path.string(),
        result,
        error));
    assert(!result.notify);
    assert(result.relevant_updates == 0U);

    {
        std::ofstream corrupt(path, std::ios::trunc);
        assert(corrupt);
        corrupt
            << "version\t1\n"
            << "last-notified\tnot-a-number\n";
    }
    error.clear();
    assert(!evaluate_update_notification(
        {security},
        preferences,
        86400 * 5,
        0,
        path.string(),
        result,
        error));
    assert(
        error.find("last-notified") !=
        std::string::npos);

    std::filesystem::remove(path);
    return 0;
}
