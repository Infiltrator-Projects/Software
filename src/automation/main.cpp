// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/update_policy.hpp"
#include "engine/engine_service_core.hpp"

#include <glib.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {
using namespace infiltrator::software;

constexpr const char *kPreferences =
    "/etc/infiltrator-software/automatic-updates.conf";
constexpr const char *kSuccessStamp =
    "/var/lib/infiltrator/software/automatic-updates.last";
constexpr const char *kAttemptStamp =
    "/var/lib/infiltrator/software/automatic-updates.attempt";

std::int64_t now_unix()
{
    return static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

std::int64_t read_stamp(const char *path)
{
    std::ifstream input(path);
    std::int64_t value=0;
    if (input) input>>value;
    return value;
}

bool write_stamp(
    const char *path,
    const char *label,
    std::string &error)
{
    static constexpr const char *kDirectory =
        "/var/lib/infiltrator/software";

    std::error_code ec;
    std::filesystem::create_directories(
        kDirectory, ec);
    if (ec) {
        error =
            "Unable to create automatic-update state directory: " +
            ec.message();
        return false;
    }

    std::string pattern =
        std::string(kDirectory) +
        "/.automatic-updates-stamp-XXXXXX";
    std::vector<char> writable(
        pattern.begin(),
        pattern.end());
    writable.push_back('\0');
    const int fd = mkstemp(writable.data());
    if (fd < 0) {
        error =
            std::string("Unable to stage the automatic-update ") +
            label + " timestamp.";
        return false;
    }

    const std::string value =
        std::to_string(now_unix()) + "\n";
    std::size_t offset = 0U;
    bool ok = fchmod(fd, 0644) == 0;
    while (ok && offset < value.size()) {
        const ssize_t written =
            write(
                fd,
                value.data() + offset,
                value.size() - offset);
        if (written > 0) {
            offset += static_cast<std::size_t>(written);
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        ok = false;
    }
    if (ok && fsync(fd) != 0) {
        ok = false;
    }
    if (close(fd) != 0) {
        ok = false;
    }

    const std::filesystem::path temporary(
        writable.data());
    if (!ok ||
        rename(temporary.c_str(), path) != 0) {
        std::filesystem::remove(temporary, ec);
        error =
            std::string("Unable to durably publish the automatic-update ") +
            label + " timestamp.";
        return false;
    }

    const int directory_fd =
        open(
            kDirectory,
            O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory_fd < 0) {
        error =
            std::string("Unable to verify automatic-update ") +
            label + " timestamp durability.";
        return false;
    }
    const bool synced =
        fsync(directory_fd) == 0;
    const bool closed =
        close(directory_fd) == 0;
    if (!synced || !closed) {
        error =
            std::string("Unable to durably publish the automatic-update ") +
            label + " timestamp.";
        return false;
    }
    error.clear();
    return true;
}

bool due(const SoftwarePreferences &prefs)
{
    const std::int64_t last =
        std::max(
            read_stamp(kSuccessStamp),
            read_stamp(kAttemptStamp));
    if (last<=0) {
        std::ifstream uptime("/proc/uptime");
        double seconds_since_boot = 0.0;
        if (uptime &&
            (uptime >> seconds_since_boot)) {
            const double first =
                static_cast<double>(
                    std::max(
                        prefs.first_refresh_minutes,
                        1U)) *
                60.0;
            return seconds_since_boot >= first;
        }
        return true;
    }
    const std::int64_t seconds=
        static_cast<std::int64_t>(
            std::max(
                prefs.recurring_refresh_minutes,
                1U)) *
        60;
    return now_unix()-last>=seconds;
}

std::vector<std::string> exact_specs(const TransactionPlan &plan)
{
    std::vector<std::string> result;
    for (const TransactionItem &item:plan.items) {
        if (item.action==TransactionAction::remove)
            result.push_back("remove:"+item.package_id+"="+item.from_version);
        else
            result.push_back(item.package_id+"="+item.to_version);
    }
    return result;
}

bool run_command(std::vector<std::string> args,std::string &error)
{
    std::vector<gchar*> argv;
    argv.reserve(args.size()+1U);
    for (std::string &arg:args) argv.push_back(arg.data());
    argv.push_back(nullptr);
    gchar *out=nullptr,*err=nullptr;
    gint status=0;
    GError *gerror=nullptr;
    const gboolean spawned=g_spawn_sync(nullptr,argv.data(),nullptr,G_SPAWN_SEARCH_PATH,
        nullptr,nullptr,&out,&err,&status,&gerror);
    bool ok=spawned!=FALSE;
    if (ok) ok=g_spawn_check_wait_status(status,&gerror)!=FALSE;
    if (!ok) {
        if (err!=nullptr && *err!='\0') error=err;
        else if (gerror!=nullptr && gerror->message!=nullptr) error=gerror->message;
        else error="Automatic update command failed.";
    }
    g_free(out); g_free(err); g_clear_error(&gerror);
    return ok;
}

bool create_snapshot(std::string &error)
{
    gchar *path=g_find_program_in_path("timeshift");
    if (path==nullptr) {
        error="A pre-update snapshot was requested, but Timeshift is not installed.";
        return false;
    }
    g_free(path);
    return run_command(
        {"timeshift","--create","--comments","Infiltrator Software automatic update","--tags","D"},
        error);
}
}

int main()
{
    using namespace infiltrator::software;
    if (geteuid()!=0) {
        g_printerr("infiltrator-software-auto-updater must run as root.\n");
        return 1;
    }
    if (!std::filesystem::exists(kPreferences)) return 0;

    (void)setenv("INFILTRATOR_SOFTWARE_PREFERENCES_PATH",kPreferences,1);
    (void)setenv("INFILTRATOR_SOFTWARE_STATE_DB",
        "/var/lib/infiltrator/software/automatic-packages.db",1);
    (void)setenv("INFILTRATOR_SOFTWARE_REPOSITORY_CACHE",
        "/var/cache/infiltrator/software/repositories",1);

    SoftwarePreferences prefs;
    std::string error;
    if (!load_software_preferences(prefs,error)) {
        g_printerr("%s\n",error.c_str());
        return 1;
    }
    if (!prefs.auto_update_packages || !due(prefs)) return 0;
    if (system_on_battery()) {
        g_message("Automatic system updates deferred while on battery power.");
        return 0;
    }

    /*
     * Persist an attempt before doing network or package work.  The timer
     * wakes every five minutes, but due() uses the newer of success/attempt
     * timestamps, so a persistent failure observes the configured recurring
     * interval instead of hammering repositories on every timer activation.
     */
    if (!write_stamp(
            kAttemptStamp,
            "attempt",
            error)) {
        g_printerr("%s\n", error.c_str());
        return 1;
    }

    EngineServiceCore core(default_package_state_path());
    if (!core.refresh(error)) {
        g_printerr("Unable to refresh automatic update state: %s\n",error.c_str());
        return 1;
    }

    std::vector<PackageRecord> updates=core.updates();
    updates.erase(std::remove_if(updates.begin(),updates.end(),
        [&](const PackageRecord &p){return update_is_ignored(p,prefs);}),updates.end());
    if (updates.empty()) {
        if (!write_stamp(kSuccessStamp, "success", error)) {
            g_printerr("%s\n", error.c_str());
            return 1;
        }
        return 0;
    }

    TransactionRequest request;
    request.action=TransactionAction::upgrade;
    request.install_recommends =
        prefs.install_recommends;
    for (const PackageRecord &p:updates)
        request.package_ids.push_back(p.package_name.empty()?p.id:p.package_name);

    const auto plan=core.plan(request,{},error);
    if (!plan.has_value()) {
        g_printerr("Unable to plan automatic updates: %s\n",error.c_str());
        return 1;
    }

    if (prefs.snapshot_before_system_updates && plan->touches_system &&
        !create_snapshot(error)) {
        g_printerr("Automatic update stopped before mutation: %s\n",error.c_str());
        return 1;
    }

    std::vector<std::string> command;
    gchar *inhibit=g_find_program_in_path("systemd-inhibit");
    if (inhibit==nullptr) {
        g_printerr(
            "Automatic system update refused because systemd-inhibit is unavailable; "
            "Software will not mutate packages without shutdown/sleep inhibition.\n");
        return 1;
    }
    g_free(inhibit);
    command={"systemd-inhibit","--what=shutdown:sleep",
        "--who=Infiltrator Software","--why=Installing automatic software updates",
        "--mode=block","/usr/libexec/infiltrator-software-update-helper","apply-plan"};
    if (prefs.keep_configuration) command.push_back("--force-confold");

    const auto specs=exact_specs(*plan);
    command.insert(command.end(),specs.begin(),specs.end());
    if (!run_command(std::move(command),error)) {
        g_printerr("Automatic system update failed: %s\n",error.c_str());
        return 1;
    }

    if (!core.refresh_installed(error)) {
        g_printerr(
            "Packages were applied, but the resulting installed state "
            "could not be verified: %s\n",
            error.c_str());
        return 1;
    }
    if (!write_stamp(kSuccessStamp, "success", error)) {
        g_printerr("%s\n", error.c_str());
        return 1;
    }
    return 0;
}
