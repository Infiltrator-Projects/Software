// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_SYSTEM_VIEW_HPP
#define INFILTRATOR_SOFTWARE_SYSTEM_VIEW_HPP

#include <gtk/gtk.h>

namespace infiltrator::software::app {

struct WindowState;

GtkWidget *make_system_page(WindowState *state);
void refresh_system(WindowState *state, bool refresh_metadata);
void system_manage_kernels_clicked(GtkButton *button, gpointer user_data);

} // namespace infiltrator::software::app

#endif
