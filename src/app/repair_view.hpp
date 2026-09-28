// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_REPAIR_VIEW_HPP
#define INFILTRATOR_SOFTWARE_REPAIR_VIEW_HPP

#include <gtk/gtk.h>

namespace infiltrator::software::app {

struct WindowState;

GtkWidget *make_repair_page(WindowState *state);
void refresh_repair(WindowState *state, bool refresh_metadata);

} // namespace infiltrator::software::app

#endif
