// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_UI_COMPONENTS_HPP
#define INFILTRATOR_SOFTWARE_UI_COMPONENTS_HPP

#include <gtk/gtk.h>

namespace infiltrator::software {

GtkWidget *make_icon(const char *name, int size);

GtkWidget *make_label(
    const char *text,
    const char *css_class,
    float xalign = 0.0F);

GtkWidget *make_stat_card(
    const char *caption,
    const char *value,
    const char *semantic_class,
    GtkWidget **value_out = nullptr);

GtkWidget *make_page_intro(
    const char *icon_name,
    const char *title,
    const char *subtitle);

} // namespace infiltrator::software

#endif
