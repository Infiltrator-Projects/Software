// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_NAVIGATION_CONTROLLER_HPP
#define INFILTRATOR_SOFTWARE_NAVIGATION_CONTROLLER_HPP

#include <gtk/gtk.h>

namespace infiltrator::software::app {

struct WindowState;

GtkWidget *make_navigation(WindowState *state);
void refresh_page_if_needed(WindowState *state, int index);

} // namespace infiltrator::software::app

#endif
