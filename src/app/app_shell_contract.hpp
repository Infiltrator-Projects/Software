// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_APP_SHELL_CONTRACT_HPP
#define INFILTRATOR_SOFTWARE_APP_SHELL_CONTRACT_HPP

#include "core/model.hpp"

#include <gtk/gtk.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace infiltrator::software::app {

struct WindowState;

/*
 * Transitional contract for the few page operations still implemented by the
 * legacy app shell. New views must not forward-declare or directly fan out to
 * these functions; application-wide consequences belong in AppCoordinator.
 */
void refresh_updates(WindowState *state, bool refresh_metadata);
void refresh_discover(WindowState *state, bool force_refresh);
void rebuild_discover_repository_preview(WindowState *state);
std::filesystem::path update_runtime_state_path();
void set_update_runtime_state(std::string_view value);

GtkWidget *make_transaction_confirmation_dialog(
    GtkWindow *parent,
    const char *title,
    const std::string &heading,
    const char *accept_label,
    const TransactionPlan &plan);

} // namespace infiltrator::software::app

#endif
