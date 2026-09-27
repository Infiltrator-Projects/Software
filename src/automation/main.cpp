// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/update_policy.hpp"
#include "engine/engine_service_core.hpp"

#include <glib.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {
using namespace infiltrator::software;

constexpr const char *kPreferences =
    "/etc/infiltrator-software/automatic-updates.conf";
constexpr const char *kStamp =
    "/var/lib/infiltrator/software/automatic-updates.last";

std::int64_t now_unix()
{
    return static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

std::int64_t read_stamp()
{
    std::ifstream input(kStamp);
    std::int64_t value=0;
    if (input) input>>value;
    return value;
}

void write_stamp()
{
    std::error_code ec;
    std::filesystem::create_directories("/var/lib/infiltrator/software",ec);
    std::ofstream output(kStamp,std::ios::out|std::ios::trunc);
    if (output) output<<now_unix()<<'\n';
}

bool due(const SoftwarePreferences &prefs)
{
    const std::int64_t last=read_stamp();
    if (last<=0) return true;
    const std::int64_t seconds=
        static_cast<std::int64_t>(std::max(prefs.recurring_refresh_minutes,1U))*60;
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

    EngineServiceCore core(default_package_state_path());
    if (!core.refresh(error)) {
        g_printerr("Unable to refresh automatic update state: %s\n",error.c_str());
        return 1;
    }

    std::vector<PackageRecord> updates=core.updates();
    updates.erase(std::remove_if(updates.begin(),updates.end(),
        [&](const PackageRecord &p){return update_is_ignored(p,prefs);}),updates.end());
    if (updates.empty()) { write_stamp(); return 0; }

    TransactionRequest request;
    request.action=TransactionAction::upgrade;
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
    if (inhibit!=nullptr) {
        g_free(inhibit);
        command={"systemd-inhibit","--what=shutdown:sleep",
            "--who=Infiltrator Software","--why=Installing automatic software updates",
            "--mode=block","/usr/libexec/infiltrator-software-update-helper","apply-plan"};
    } else {
        command={"/usr/libexec/infiltrator-software-update-helper","apply-plan"};
    }
    if (prefs.keep_configuration) command.push_back("--force-confold");

    const auto specs=exact_specs(*plan);
    command.insert(command.end(),specs.begin(),specs.end());
    if (!run_command(std::move(command),error)) {
        g_printerr("Automatic system update failed: %s\n",error.c_str());
        return 1;
    }

    std::string ignored;
    (void)core.refresh_installed(ignored);
    write_stamp();
    return 0;
}
