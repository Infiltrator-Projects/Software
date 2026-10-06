// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_UPDATES_CONTROLLER_HPP
#define INFILTRATOR_SOFTWARE_UPDATES_CONTROLLER_HPP

#include "core/model.hpp"
#include "external/external_updates.hpp"

#include <gtk/gtk.h>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace infiltrator::software::app {

struct UpdatesController {
    GtkListBox *updates_list{};
    GtkWidget *updates_status{};
    GtkWidget *updates_count{};
    GtkWidget *updates_critical{};
    GtkWidget *updates_install{};
    GtkWidget *updates_security{};
    GtkWidget *updates_refresh{};
    GtkWidget *updates_reboot_banner{};
    GtkWidget *updates_reboot_detail{};
    GtkWidget *updates_backend{};

    GtkListBox *external_updates_list{};
    GtkWidget *external_updates_status{};
    GtkWidget *external_updates_spinner{};
    GtkWidget *external_flatpak_apply{};
    GtkWidget *external_cinnamon_apply{};
    std::vector<ExternalUpdate> external_update_records;
    std::unordered_set<std::string> selected_flatpak_refs;
    std::unordered_set<std::string> selected_cinnamon_refs;

    GtkWidget *updates_transaction_panel{};
    GtkWidget *updates_transaction_phase{};
    GtkWidget *updates_transaction_detail{};
    GtkWidget *updates_transaction_meta{};
    GtkWidget *updates_stage_labels[6]{};
    GtkWidget *updates_progress{};
    guint updates_progress_timer_id{0U};
    gint64 updates_progress_started_us{0};
    std::size_t updates_progress_items{0U};
    std::uint64_t updates_progress_download_bytes{0U};
    std::string updates_progress_token;
    std::string updates_progress_phase;
    bool updates_post_install_refresh{false};
    bool updates_restart_after_verify{false};

    std::vector<PackageRecord> update_records;
    std::unordered_set<std::string> selected_update_ids;
    std::string pending_update_selection;
    std::optional<TransactionPlan> pending_update_plan;
    unsigned int updates_generation{0U};
    bool updates_busy{false};
    bool external_updates_active{false};
    bool updates_auto_refresh_pending{true};
    gint64 updates_last_metadata_refresh_us{0};
    guint updates_refresh_timer_id{0U};
    bool updates_loaded{false};
};

inline bool updates_controller_loaded(
    const UpdatesController &controller) noexcept
{
    return controller.updates_loaded;
}

struct UpdatesRefreshRequest {
    unsigned int generation{0U};
    bool refresh_metadata{false};
    bool refresh_external{true};
    bool show_flatpak{true};
    bool show_cinnamon{true};
};

struct UpdatesRefreshResult {
    unsigned int generation{0U};
    bool refreshed_metadata{false};
    bool external_refreshed{false};
    std::vector<PackageRecord> records;
    std::vector<ExternalUpdate> external_records;
    std::string external_error;
    std::string error;
};

struct UpdatePlanRequest {
    std::vector<std::string> package_ids;
    bool install_recommends{false};
};

struct UpdatePlanResult {
    std::optional<TransactionPlan> plan;
    std::string error;
};

UpdatesRefreshResult refresh_updates_data(
    const UpdatesRefreshRequest &request);

UpdatePlanResult plan_updates(
    const UpdatePlanRequest &request);

} // namespace infiltrator::software::app

#endif
