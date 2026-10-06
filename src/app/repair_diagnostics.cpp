// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/repair_diagnostics.hpp"

#include "app/text_utils.hpp"
#include "client/engine_client.hpp"
#include "core/model.hpp"
#include "sources/source_inventory.hpp"

#include <gio/gio.h>

#include <filesystem>
#include <string>
#include <vector>

namespace infiltrator::software::app {
namespace {

bool run_dpkg_audit(std::string &diagnostic, std::string &error)
{
    diagnostic.clear();
    error.clear();

    gchar *program = g_find_program_in_path("dpkg");
    if (program == nullptr) {
        error = "dpkg is not available for package-state diagnostics.";
        return false;
    }

    gchar *argv[] = {
        program,
        const_cast<gchar *>("--audit"),
        nullptr
    };
    gchar *standard_output = nullptr;
    gchar *standard_error = nullptr;
    gint wait_status = 0;
    GError *spawn_error = nullptr;
    const gboolean spawned =
        g_spawn_sync(
            nullptr,
            argv,
            nullptr,
            G_SPAWN_SEARCH_PATH,
            nullptr,
            nullptr,
            &standard_output,
            &standard_error,
            &wait_status,
            &spawn_error);
    g_free(program);

    if (!spawned) {
        error =
            spawn_error != nullptr && spawn_error->message != nullptr
                ? std::string(spawn_error->message)
                : "Unable to run dpkg package-state diagnostics.";
        g_clear_error(&spawn_error);
        g_free(standard_output);
        g_free(standard_error);
        return false;
    }

    GError *status_error = nullptr;
    const gboolean successful =
        g_spawn_check_wait_status(wait_status, &status_error);
    if (!successful) {
        error =
            standard_error != nullptr && *standard_error != '\0'
                ? single_line(standard_error)
                : status_error != nullptr && status_error->message != nullptr
                    ? std::string(status_error->message)
                    : "dpkg package-state diagnostics failed.";
        g_clear_error(&status_error);
        g_free(standard_output);
        g_free(standard_error);
        return false;
    }

    if (standard_output != nullptr) {
        diagnostic = single_line(standard_output);
    }
    g_free(standard_output);
    g_free(standard_error);
    return true;
}

bool pending_dpkg_update_fragments()
{
    const std::filesystem::path directory{"/var/lib/dpkg/updates"};
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec) || ec) {
        return false;
    }

    for (const auto &entry :
         std::filesystem::directory_iterator(directory, ec)) {
        if (ec) {
            return false;
        }
        if (!entry.is_regular_file(ec) || ec) {
            ec.clear();
            continue;
        }
        const std::uintmax_t size = entry.file_size(ec);
        if (!ec && size != 0U) {
            return true;
        }
        ec.clear();
    }
    return false;
}

} // namespace

RepairInspection inspect_repair_state(const bool refresh_metadata)
{
    RepairInspection result;
    result.refreshed_metadata = refresh_metadata;

    infiltrator::software::EngineClient engine;
    std::string engine_error;
    const bool reconciled =
        refresh_metadata
            ? engine.refresh(engine_error)
            : engine.refresh_installed(engine_error);

    if (!reconciled) {
        result.issues.emplace_back(
            "Native package state: " + single_line(engine_error));
    } else {
        std::vector<PackageRecord> updates;
        std::string update_error;
        if (!engine.list_updates(updates, update_error)) {
            result.issues.emplace_back(
                "Native update inventory: " + single_line(update_error));
        } else {
            result.engine_ready = true;
            result.update_count = updates.size();
        }
    }

    infiltrator::software::SourceInventory inventory;
    std::string source_error;
    const std::vector<SourceRecord> sources = inventory.list(source_error);
    if (!source_error.empty()) {
        result.issues.emplace_back(
            "Repository configuration: " + single_line(source_error));
    } else {
        result.source_count = sources.size();
    }

    std::string audit;
    std::string audit_error;
    if (!run_dpkg_audit(audit, audit_error)) {
        result.issues.emplace_back(
            "dpkg audit: " + single_line(audit_error));
    } else if (!audit.empty()) {
        result.interrupted = true;
        result.issues.emplace_back(
            "dpkg reports unfinished or inconsistent package state: " + audit);
    }

    if (pending_dpkg_update_fragments()) {
        result.interrupted = true;
        result.issues.emplace_back(
            "dpkg has pending update fragments in /var/lib/dpkg/updates.");
    }

    return result;
}

} // namespace infiltrator::software::app
