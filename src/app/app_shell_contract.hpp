// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_APP_SHELL_CONTRACT_HPP
#define INFILTRATOR_SOFTWARE_APP_SHELL_CONTRACT_HPP

#include "app/runtime_state.hpp"
#include "app/transaction_review.hpp"

namespace infiltrator::software::app {

struct WindowState;

/*
 * Transitional contract for the few page refresh operations still implemented
 * by the legacy app shell. Application-wide consequences belong in
 * AppCoordinator. Runtime state and transaction review now have independent
 * owners; compatibility aliases below keep existing call sites source-stable
 * while removing their dependency on main.cpp implementations.
 */
void refresh_updates(WindowState *state, bool refresh_metadata);
void refresh_discover(WindowState *state, bool force_refresh);
void rebuild_discover_repository_preview(WindowState *state);

inline std::filesystem::path update_runtime_state_path()
{
    return software_update_runtime_state_path();
}

inline void set_update_runtime_state(const std::string_view value)
{
    set_software_update_runtime_state(value);
}

inline GtkWidget *make_transaction_confirmation_dialog(
    GtkWindow *parent,
    const char *title,
    const std::string &heading,
    const char *accept_label,
    const TransactionPlan &plan)
{
    return make_transaction_review_dialog(
        parent, title, heading, accept_label, plan);
}

} // namespace infiltrator::software::app

#endif
