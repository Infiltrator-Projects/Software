// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_PREFERENCES_DIALOG_HPP
#define INFILTRATOR_SOFTWARE_PREFERENCES_DIALOG_HPP

#include <gtk/gtk.h>

namespace infiltrator::software::app {

void settings_clicked(GtkButton *button, gpointer user_data);
void about_clicked(GtkButton *button, gpointer user_data);

} // namespace infiltrator::software::app

#endif
