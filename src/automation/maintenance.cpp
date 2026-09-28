// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/update_policy.hpp"
#include "core/transaction_history.hpp"
#include "engine/engine_service_core.hpp"

#include <glib.h>

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

using namespace infiltrator::software;

constexpr const char *kPreferences =
    "/etc/infiltrator-software/automatic-updates.conf";

bool run_capture(
    std::vector<std::string> arguments,
    std::string &output,
    std::string &error)
{
    std::vector<gchar *> argv;
    argv.reserve(arguments.size() + 1U);
    for (std::string &argument : arguments) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);

    gchar *stdout_text = nullptr;
    gchar *stderr_text = nullptr;
    gint wait_status = 0;
    GError *gerror = nullptr;
    const gboolean spawned =
        g_spawn_sync(
            nullptr,
            argv.data(),
            nullptr,
            G_SPAWN_SEARCH_PATH,
            nullptr,
            nullptr,
            &stdout_text,
            &stderr_text,
            &wait_status,
            &gerror);
    bool success = spawned != FALSE;
    if (success) {
        success =
            g_spawn_check_wait_status(
                wait_status, &gerror) != FALSE;
    }

    if (stdout_text != nullptr) {
        output = stdout_text;
    }
    if (!success) {
        if (stderr_text != nullptr &&
            *stderr_text != '\0') {
            error = stderr_text;
        } else if (
            gerror != nullptr &&
            gerror->message != nullptr) {
            error = gerror->message;
        } else {
            error = "Maintenance command failed.";
        }
    }

    g_free(stdout_text);
    g_free(stderr_text);
    g_clear_error(&gerror);
    return success;
}

bool run_command(
    std::vector<std::string> arguments,
    std::string &error)
{
    std::string ignored;
    return run_capture(
        std::move(arguments),
        ignored,
        error);
}

struct SimulatedRemoval {
    std::string identity;
    std::string package;
    std::string version;
};

bool parse_simulated_removals(
    const std::string &output,
    std::vector<SimulatedRemoval> &removals,
    std::string &error)
{
    removals.clear();
    error.clear();

    std::istringstream input(output);
    std::string line;
    while (std::getline(input, line)) {
        if (line.rfind("Remv ", 0U) != 0U) {
            continue;
        }

        const std::size_t identity_start = 5U;
        const std::size_t identity_end =
            line.find(' ', identity_start);
        const std::size_t version_open =
            line.find('[', identity_end);
        const std::size_t version_close =
            version_open == std::string::npos
                ? std::string::npos
                : line.find(']', version_open + 1U);
        if (identity_end == std::string::npos ||
            identity_end <= identity_start ||
            version_open == std::string::npos ||
            version_close == std::string::npos ||
            version_close <= version_open + 1U) {
            error =
                "Unable to parse simulated maintenance removal: " +
                line;
            removals.clear();
            return false;
        }

        SimulatedRemoval removal;
        removal.identity =
            line.substr(
                identity_start,
                identity_end - identity_start);
        removal.version =
            line.substr(
                version_open + 1U,
                version_close - version_open - 1U);
        const std::size_t colon =
            removal.identity.find(':');
        removal.package =
            removal.identity.substr(0U, colon);

        if (removal.identity.empty() ||
            removal.package.empty() ||
            removal.version.empty()) {
            error =
                "Simulated maintenance returned an incomplete removal: " +
                line;
            removals.clear();
            return false;
        }

        const auto duplicate =
            std::find_if(
                removals.begin(),
                removals.end(),
                [&](const SimulatedRemoval &existing) {
                    return existing.identity ==
                        removal.identity;
                });
        if (duplicate != removals.end()) {
            error =
                "Simulated maintenance returned duplicate package removal " +
                removal.identity + ".";
            removals.clear();
            return false;
        }
        removals.emplace_back(std::move(removal));
    }
    return true;
}

bool proposed_kernel_removal_is_safe(
    const std::vector<SimulatedRemoval> &removals,
    const std::vector<KernelRecord> &kernels,
    std::string &error)
{
    for (const KernelRecord &kernel : kernels) {
        bool touches_kernel = false;
        for (const std::string &package :
             kernel.remove_package_ids) {
            if (std::any_of(
                    removals.begin(),
                    removals.end(),
                    [&](const SimulatedRemoval &removal) {
                        return removal.package == package;
                    })) {
                touches_kernel = true;
                break;
            }
        }
        if (!touches_kernel) {
            continue;
        }

        if (kernel.active) {
            error =
                "Automatic maintenance refused to remove the running kernel " +
                kernel.version + kernel.kernel_type + ".";
            return false;
        }
        if (!kernel.safe_to_remove) {
            error =
                "Automatic maintenance refused to remove protected kernel " +
                kernel.version + kernel.kernel_type +
                "; Software retains one older fallback kernel.";
            return false;
        }
    }
    return true;
}

void record_maintenance_history(
    const TransactionPlan &plan,
    const bool success,
    const std::string_view message)
{
    if (plan.items.empty()) {
        return;
    }
    const std::string path =
        system_transaction_history_path();
    TransactionHistoryStore store(path);
    std::string history_error;
    if (!store.append(
            plan,
            success,
            message,
            history_error)) {
        g_warning(
            "Unable to record automatic-maintenance history: %s",
            history_error.c_str());
        return;
    }
    if (chmod(path.c_str(), 0644) != 0) {
        g_warning(
            "Automatic-maintenance history was written but could not be made "
            "readable by the Software history view.");
    }
}

TransactionPlan maintenance_history_plan(
    const std::vector<SimulatedRemoval> &removals)
{
    TransactionPlan plan;
    for (const SimulatedRemoval &removal :
         removals) {
        TransactionItem item;
        item.package_id =
            removal.identity;
        item.action =
            TransactionAction::remove;
        item.from_version =
            removal.version;
        item.source =
            "APT autoremove";
        item.requested = true;
        plan.items.emplace_back(
            std::move(item));
    }
    return plan;
}

} // namespace

int main()
{
    if (geteuid() != 0) {
        g_printerr(
            "infiltrator-software-maintenance must run as root.\n");
        return 1;
    }
    if (!std::filesystem::exists(kPreferences)) {
        return 0;
    }

    (void)setenv(
        "INFILTRATOR_SOFTWARE_PREFERENCES_PATH",
        kPreferences,
        1);
    (void)setenv(
        "INFILTRATOR_SOFTWARE_STATE_DB",
        "/var/lib/infiltrator/software/maintenance-packages.db",
        1);
    (void)setenv(
        "INFILTRATOR_SOFTWARE_REPOSITORY_CACHE",
        "/var/cache/infiltrator/software/repositories",
        1);

    SoftwarePreferences preferences;
    std::string error;
    if (!load_software_preferences(
            preferences, error)) {
        g_printerr("%s\n", error.c_str());
        return 1;
    }
    if (!preferences.auto_remove_obsolete) {
        return 0;
    }
    if (system_on_battery()) {
        g_message(
            "Automatic Software maintenance deferred while on battery power.");
        return 0;
    }

    gchar *apt = g_find_program_in_path("apt-get");
    if (apt == nullptr) {
        g_printerr(
            "Automatic maintenance requires apt-get as the Debian package executor.\n");
        return 1;
    }
    g_free(apt);

    /*
     * Mint Update Manager's automatic-maintenance contract is apt autoremove
     * --purge.  Software preserves that behaviour but adds a fail-closed
     * preview: the exact simulated removals are checked against Software's
     * native kernel lifecycle model before the privileged mutation is allowed.
     */
    std::string simulation;
    if (!run_capture(
            {"apt-get", "-s", "autoremove", "--purge"},
            simulation,
            error)) {
        g_printerr(
            "Unable to simulate automatic maintenance: %s\n",
            error.c_str());
        return 1;
    }

    std::vector<SimulatedRemoval> removals;
    if (!parse_simulated_removals(
            simulation,
            removals,
            error)) {
        g_printerr(
            "Automatic maintenance refused an unparseable removal plan: %s\n",
            error.c_str());
        return 1;
    }
    if (removals.empty()) {
        g_message(
            "Automatic maintenance found no obsolete packages.");
        return 0;
    }

    EngineServiceCore core(default_package_state_path());
    if (!core.refresh(error)) {
        g_printerr(
            "Unable to refresh kernel safety state before automatic maintenance: %s\n",
            error.c_str());
        return 1;
    }

    const std::vector<KernelRecord> kernels =
        core.kernels(
            KernelInventory::default_kernel_type());
    if (!proposed_kernel_removal_is_safe(
            removals,
            kernels,
            error)) {
        g_printerr("%s\n", error.c_str());
        return 1;
    }

    gchar *inhibit =
        g_find_program_in_path("systemd-inhibit");
    if (inhibit == nullptr) {
        g_printerr(
            "Automatic maintenance refused because systemd-inhibit is unavailable.\n");
        return 1;
    }
    g_free(inhibit);

    std::vector<std::string> command{
        "systemd-inhibit",
        "--what=shutdown:sleep",
        "--who=Infiltrator Software",
        "--why=Removing obsolete kernels and dependencies",
        "--mode=block",
        "/usr/libexec/infiltrator-software-update-helper",
        "apply-plan",
        "--purge-removals"};
    for (const SimulatedRemoval &removal : removals) {
        command.emplace_back(
            "remove:" + removal.identity + "=" +
            removal.version);
    }

    const TransactionPlan history_plan =
        maintenance_history_plan(
            removals);

    /*
     * Do not execute a second unconstrained autoremove. The privileged helper
     * refreshes metadata, re-simulates this exact version-pinned purge plan and
     * aborts if APT proposes any additional, missing or changed mutation.
     */
    if (!run_command(
            std::move(command),
            error)) {
        record_maintenance_history(
            history_plan,
            false,
            "Automatic maintenance failed: " +
                error);
        g_printerr(
            "Automatic maintenance failed: %s\n",
            error.c_str());
        return 1;
    }

    record_maintenance_history(
        history_plan,
        true,
        "Automatic maintenance removed obsolete packages after native kernel-safety verification.");

    g_message(
        "Automatic maintenance completed after native kernel-safety verification.");
    return 0;
}
