// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_REPOSITORY_VIEW_HPP
#define INFILTRATOR_SOFTWARE_REPOSITORY_VIEW_HPP

#include <gtk/gtk.h>

namespace infiltrator::software::app {

struct WindowState;

GtkWidget *make_repositories_page(WindowState *state);
void refresh_repositories(WindowState *state);

} // namespace infiltrator::software::app

#endif
