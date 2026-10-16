// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SOFTWARE_UPDATES_VIEW_HPP
#define SOFTWARE_UPDATES_VIEW_HPP

#include "app/view_intents.hpp"

#include <gtk/gtk.h>

namespace infiltrator::software::app {

struct WindowState;

GtkWidget *make_updates_page(WindowState *state);
void refresh_updates(WindowState *state, bool refresh_metadata = false);
void refresh_updates_internal(
    WindowState *state,
    bool refresh_metadata,
    bool refresh_external);

} // namespace infiltrator::software::app

#endif
