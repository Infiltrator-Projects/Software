// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/repair_diagnostics.hpp"

#include "app/process_support.hpp"
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

    SubprocessResult process;
    if (!run_subprocess_sync({"dpkg", "--audit"}, process, error)) {
        if (error.empty()) {
            error = "Unable to run dpkg package-state diagnostics.";
        }
        return false;
    }
    if (!process.successful) {
        error =
            !process.standard_error.empty()
                ? single_line(process.standard_error)
                : "dpkg package-state diagnostics failed.";
        return false;
    }

    diagnostic = single_line(process.standard_output);
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
