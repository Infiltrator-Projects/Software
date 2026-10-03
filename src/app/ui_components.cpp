// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/ui_components.hpp"

#include <cstring>

namespace infiltrator::software {
namespace {

const char *stat_icon_name(const char *caption) noexcept
{
    if (caption == nullptr) {
        return "emblem-system-symbolic";
    }
    if (std::strstr(caption, "APPLICATION") != nullptr) {
        return "view-grid-symbolic";
    }
    if (std::strstr(caption, "SOURCE") != nullptr ||
        std::strstr(caption, "REPOSITOR") != nullptr) {
        return "network-workgroup-symbolic";
    }
    if (std::strstr(caption, "AVAILABLE") != nullptr ||
        std::strstr(caption, "UPDATE") != nullptr) {
        return "software-update-available-symbolic";
    }
    if (std::strstr(caption, "CRITICAL") != nullptr ||
        std::strstr(caption, "ISSUE") != nullptr) {
        return "dialog-warning-symbolic";
    }
    if (std::strstr(caption, "TRANSACTION") != nullptr ||
        std::strstr(caption, "HISTORY") != nullptr) {
        return "document-open-recent-symbolic";
    }
    if (std::strstr(caption, "DETAIL") != nullptr) {
        return "document-properties-symbolic";
    }
    if (std::strstr(caption, "BACKEND") != nullptr ||
        std::strstr(caption, "STORAGE") != nullptr) {
        return "drive-harddisk-symbolic";
    }
    if (std::strstr(caption, "PACKAGE") != nullptr ||
        std::strstr(caption, "COMPONENT") != nullptr) {
        return "application-x-executable-symbolic";
    }
    return "emblem-ok-symbolic";
}

} // namespace

GtkWidget *make_icon(const char *name, const int size)
{
    GtkWidget *image = gtk_image_new_from_icon_name(name);
    gtk_image_set_pixel_size(GTK_IMAGE(image), size);
    return image;
}

GtkWidget *make_label(
    const char *text,
    const char *css_class,
    const float xalign)
{
    GtkWidget *label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), xalign);
    if (css_class != nullptr) {
        gtk_widget_add_css_class(label, css_class);
    }
    return label;
}

GtkWidget *make_stat_card(
    const char *caption,
    const char *value,
    const char *semantic_class,
    GtkWidget **value_out)
{
    GtkWidget *card =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(card, "stat-card");
    if (semantic_class != nullptr) {
        gtk_widget_add_css_class(card, semantic_class);
    }

    GtkWidget *icon_well =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(icon_well, "stat-icon-well");
    GtkWidget *icon = make_icon(
        stat_icon_name(caption), 22);
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(icon_well), icon);
    gtk_box_append(GTK_BOX(card), icon_well);

    GtkWidget *copy =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(copy, true);

    gtk_box_append(
        GTK_BOX(copy),
        make_label(caption, "stat-caption"));

    GtkWidget *value_label =
        make_label(value, "stat-value");
    gtk_label_set_ellipsize(
        GTK_LABEL(value_label),
        PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(copy), value_label);
    gtk_box_append(GTK_BOX(card), copy);

    if (value_out != nullptr) {
        *value_out = value_label;
    }
    return card;
}

GtkWidget *make_page_intro(
    const char *icon_name,
    const char *title,
    const char *subtitle)
{
    GtkWidget *hero =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    gtk_widget_add_css_class(hero, "page-hero");
    gtk_widget_add_css_class(hero, "hero-panel");

    GtkWidget *icon_box =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(icon_box, "page-icon");
    gtk_widget_set_size_request(icon_box, 54, 54);
    GtkWidget *icon = make_icon(icon_name, 28);
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(icon_box), icon);
    gtk_box_append(GTK_BOX(hero), icon_box);

    GtkWidget *identity =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_set_hexpand(identity, true);
    gtk_box_append(
        GTK_BOX(identity),
        make_label(
            "INFLITRATOR SOFTWARE",
            "hero-kicker"));
    gtk_box_append(
        GTK_BOX(identity),
        make_label(title, "hero-title"));
    GtkWidget *copy =
        make_label(subtitle, "hero-subtitle");
    gtk_label_set_wrap(GTK_LABEL(copy), true);
    gtk_label_set_max_width_chars(
        GTK_LABEL(copy), 72);
    gtk_box_append(GTK_BOX(identity), copy);
    gtk_box_append(GTK_BOX(hero), identity);


    return hero;
}

} // namespace infiltrator::software
