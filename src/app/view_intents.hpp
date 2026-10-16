// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SOFTWARE_VIEW_INTENTS_HPP
#define SOFTWARE_VIEW_INTENTS_HPP
#include <string_view>

namespace infiltrator::software::app {
struct WindowState;

enum class UpdateSelectionPreset { all, security, kernel };

void set_discover_search_query(WindowState *state, std::string_view query);
void set_discover_health_state(WindowState *state, bool healthy);
void destroy_discover_page(WindowState *state);
void select_update_preset(WindowState *state, UpdateSelectionPreset preset);
void destroy_updates_page(WindowState *state);

} // namespace infiltrator::software::app
#endif
