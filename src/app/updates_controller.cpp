// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/updates_controller.hpp"

#include "client/engine_client.hpp"

#include <iterator>
#include <utility>

namespace infiltrator::software::app {

UpdatesRefreshResult refresh_updates_data(
    const UpdatesRefreshRequest &request)
{
    UpdatesRefreshResult result;
    result.generation = request.generation;
    result.external_refreshed = request.refresh_external;

    EngineClient engine;

    /*
     * Ordinary checks reconcile authoritative installed state against the
     * current verified repository generation. Explicit refreshes reconcile
     * repository metadata as well. The GUI should not need to know either
     * mechanism; it consumes one coherent result from this controller.
     */
    if (request.refresh_metadata) {
        result.refreshed_metadata = true;
        (void)engine.refresh(result.error);
    } else {
        (void)engine.refresh_installed(result.error);
    }

    if (result.error.empty()) {
        (void)engine.list_updates(
            result.records,
            result.error);
    }

    if (request.refresh_external && request.show_flatpak) {
        std::vector<ExternalUpdate> flatpak;
        std::string external_error;
        if (discover_flatpak_updates(flatpak, external_error)) {
            result.external_records.insert(
                result.external_records.end(),
                std::make_move_iterator(flatpak.begin()),
                std::make_move_iterator(flatpak.end()));
        } else {
            result.external_error =
                "Flatpak: " + external_error;
        }
    }

    if (request.refresh_external && request.show_cinnamon) {
        std::vector<ExternalUpdate> cinnamon;
        std::string external_error;
        if (discover_cinnamon_updates(cinnamon, external_error)) {
            result.external_records.insert(
                result.external_records.end(),
                std::make_move_iterator(cinnamon.begin()),
                std::make_move_iterator(cinnamon.end()));
        } else {
            if (!result.external_error.empty()) {
                result.external_error += "\n";
            }
            result.external_error +=
                "Cinnamon: " + external_error;
        }
    }

    return result;
}

UpdatePlanResult plan_updates(
    const UpdatePlanRequest &request)
{
    UpdatePlanResult result;
    if (request.package_ids.empty()) {
        result.error = "No updates are available to plan.";
        return result;
    }

    TransactionRequest transaction;
    transaction.action = TransactionAction::upgrade;
    transaction.package_ids = request.package_ids;
    transaction.install_recommends =
        request.install_recommends;

    EngineClient engine;
    result.plan =
        engine.plan(transaction, result.error);
    return result;
}

} // namespace infiltrator::software::app
