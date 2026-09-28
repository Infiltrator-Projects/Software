// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/history_view.hpp"

#include "core/model.hpp"

#include <pango/pango.h>

#include <cstddef>
#include <string>

namespace infiltrator::software {
namespace {

GtkWidget *make_icon(const char *name, const int size)
{
    GtkWidget *image = gtk_image_new_from_icon_name(name);
    gtk_image_set_pixel_size(GTK_IMAGE(image), size);
    return image;
}

GtkWidget *make_label(
    const char *text,
    const char *css_class,
    const float xalign = 0.0F)
{
    GtkWidget *label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), xalign);
    if (css_class != nullptr) {
        gtk_widget_add_css_class(label, css_class);
    }
    return label;
}

GtkWidget *make_history_intro()
{
    GtkWidget *hero =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
    gtk_widget_add_css_class(hero, "page-hero");
    gtk_widget_add_css_class(hero, "hero-panel");

    GtkWidget *icon_box =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(icon_box, "page-icon");
    gtk_widget_set_size_request(icon_box, 60, 60);
    GtkWidget *icon =
        make_icon("document-open-recent-symbolic", 32);
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(icon_box), icon);
    gtk_box_append(GTK_BOX(hero), icon_box);

    GtkWidget *identity =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_set_hexpand(identity, true);
    gtk_box_append(
        GTK_BOX(identity),
        make_label("INFLITRATOR SOFTWARE", "hero-kicker"));
    gtk_box_append(
        GTK_BOX(identity),
        make_label("History", "hero-title"));
    GtkWidget *copy =
        make_label(
            "Exact software-management operations and outcomes.",
            "hero-subtitle");
    gtk_label_set_wrap(GTK_LABEL(copy), true);
    gtk_label_set_max_width_chars(GTK_LABEL(copy), 72);
    gtk_box_append(GTK_BOX(identity), copy);
    gtk_box_append(GTK_BOX(hero), identity);

    GtkWidget *ribbons =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_add_css_class(ribbons, "hero-ribbons");
    gtk_widget_set_halign(ribbons, GTK_ALIGN_END);
    gtk_widget_set_valign(ribbons, GTK_ALIGN_FILL);
    static constexpr const char *classes[] = {
        "hero-ribbon-a", "hero-ribbon-b", "hero-ribbon-c"};
    for (const char *css_class : classes) {
        GtkWidget *ribbon =
            gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_add_css_class(ribbon, "hero-ribbon");
        gtk_widget_add_css_class(ribbon, css_class);
        gtk_box_append(GTK_BOX(ribbons), ribbon);
    }
    gtk_box_append(GTK_BOX(hero), ribbons);
    return hero;
}

const char *stat_icon_name(const char *caption)
{
    if (caption == nullptr) return "emblem-ok-symbolic";
    if (g_strrstr(caption, "TRANSACTION") != nullptr) {
        return "document-open-recent-symbolic";
    }
    if (g_strrstr(caption, "STORAGE") != nullptr) {
        return "drive-harddisk-symbolic";
    }
    return "document-properties-symbolic";
}

GtkWidget *make_stat_card(
    const char *caption,
    const char *value,
    const char *semantic_class,
    GtkWidget **value_out = nullptr)
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
    GtkWidget *icon =
        make_icon(stat_icon_name(caption), 22);
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
        GTK_LABEL(value_label), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(copy), value_label);
    gtk_box_append(GTK_BOX(card), copy);

    if (value_out != nullptr) {
        *value_out = value_label;
    }
    return card;
}

GtkWidget *make_history_transaction_card(
    const std::vector<TransactionHistoryItem> &records,
    const std::size_t first,
    const std::size_t last)
{
    const TransactionHistoryItem &head = records[first];

    GtkWidget *card =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(card, "card");
    gtk_widget_add_css_class(
        card, "history-transaction-card");
    gtk_widget_add_css_class(
        card,
        head.success ? "card-info" : "card-warning");

    GtkWidget *header =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);

    GtkWidget *history_icon_well =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(
        history_icon_well, "history-icon-well");
    GtkWidget *history_icon =
        make_icon(
            head.success
                ? "emblem-ok-symbolic"
                : "dialog-warning-symbolic",
            20);
    gtk_widget_set_halign(
        history_icon, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(
        history_icon, GTK_ALIGN_CENTER);
    gtk_box_append(
        GTK_BOX(history_icon_well), history_icon);
    gtk_box_append(
        GTK_BOX(header), history_icon_well);

    const std::string title =
        "Transaction #" +
        std::to_string(head.transaction_id) +
        "  •  " +
        history_timestamp(head.completed_at_unix);
    GtkWidget *title_label =
        make_label(title.c_str(), "card-title");
    gtk_widget_set_hexpand(title_label, true);
    gtk_box_append(GTK_BOX(header), title_label);

    GtkWidget *outcome =
        make_label(
            head.success ? "Completed" : "Failed",
            head.success ? "state-installed" : "state-warning");
    gtk_box_append(GTK_BOX(header), outcome);
    gtk_box_append(GTK_BOX(card), header);

    const std::size_t item_count = last - first;
    std::string summary =
        std::to_string(item_count) +
        (item_count == 1U
             ? " package change"
             : " package changes");
    if (!head.message.empty()) {
        summary += "  •  " + head.message;
    }
    GtkWidget *summary_label =
        make_label(summary.c_str(), "card-copy");
    gtk_label_set_wrap(GTK_LABEL(summary_label), true);
    gtk_box_append(GTK_BOX(card), summary_label);

    for (std::size_t index = first; index < last; ++index) {
        const TransactionHistoryItem &entry = records[index];

        GtkWidget *row =
            gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_widget_add_css_class(row, "package-row");

        GtkWidget *identity =
            gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_set_hexpand(identity, true);

        std::string package_title = entry.package_id;
        if (entry.requested) {
            package_title += "  •  requested";
        }
        gtk_box_append(
            GTK_BOX(identity),
            make_label(package_title.c_str(), "source-name"));

        std::string versions;
        if (entry.action == TransactionAction::remove) {
            versions =
                entry.from_version.empty()
                    ? "removed"
                    : entry.from_version + "  →  removed";
        } else if (entry.from_version.empty()) {
            versions =
                "installed  →  " + entry.to_version;
        } else {
            versions =
                entry.from_version + "  →  " +
                entry.to_version;
        }
        gtk_box_append(
            GTK_BOX(identity),
            make_label(versions.c_str(), "card-copy"));

        if (!entry.source.empty()) {
            const std::string source =
                "Source: " + entry.source;
            GtkWidget *source_label =
                make_label(source.c_str(), "discover-meta");
            gtk_label_set_ellipsize(
                GTK_LABEL(source_label),
                PANGO_ELLIPSIZE_END);
            gtk_box_append(
                GTK_BOX(identity), source_label);
        }

        gtk_box_append(GTK_BOX(row), identity);

        GtkWidget *action =
            make_label(
                std::string(
                    transaction_action_name(
                        entry.action)).c_str(),
                entry.system_critical
                    ? "state-warning"
                    : "state-info");
        gtk_widget_set_valign(action, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(row), action);
        gtk_box_append(GTK_BOX(card), row);
    }

    return card;
}

} // namespace

std::string history_timestamp(
    const std::int64_t unix_time)
{
    GDateTime *value =
        g_date_time_new_from_unix_local(
            static_cast<gint64>(unix_time));
    if (value == nullptr) {
        return "Unknown time";
    }

    gchar *formatted =
        g_date_time_format(value, "%Y-%m-%d %H:%M:%S");
    std::string result =
        formatted == nullptr
            ? "Unknown time"
            : std::string(formatted);
    g_free(formatted);
    g_date_time_unref(value);
    return result;
}

void rebuild_history_view(
    GtkListBox *list,
    GtkWidget *count_label,
    const std::vector<TransactionHistoryItem> &records)
{
    if (list == nullptr) return;

    GtkWidget *child =
        gtk_widget_get_first_child(GTK_WIDGET(list));
    while (child != nullptr) {
        GtkWidget *next =
            gtk_widget_get_next_sibling(child);
        gtk_list_box_remove(list, child);
        child = next;
    }

    std::size_t transaction_count = 0U;
    std::size_t index = 0U;
    while (index < records.size()) {
        const std::int64_t transaction_id =
            records[index].transaction_id;
        std::size_t end = index + 1U;
        while (end < records.size() &&
               records[end].transaction_id ==
                   transaction_id) {
            ++end;
        }

        GtkWidget *row = gtk_list_box_row_new();
        gtk_list_box_row_set_child(
            GTK_LIST_BOX_ROW(row),
            make_history_transaction_card(
                records, index, end));
        gtk_list_box_append(list, row);

        ++transaction_count;
        index = end;
    }

    if (count_label != nullptr) {
        const std::string count =
            std::to_string(transaction_count);
        gtk_label_set_text(
            GTK_LABEL(count_label),
            count.c_str());
    }
}

GtkWidget *create_history_page(
    GtkListBox **list_out,
    GtkWidget **status_out,
    GtkWidget **count_out,
    GtkWidget **refresh_out,
    GCallback refresh_callback,
    gpointer user_data)
{
    GtkWidget *page =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(page, "content");
    gtk_widget_add_css_class(page, "page-history");

    gtk_box_append(
        GTK_BOX(page),
        make_history_intro());

    GtkWidget *stats = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(stats), 10);
    gtk_grid_set_column_homogeneous(
        GTK_GRID(stats), true);
    gtk_grid_attach(
        GTK_GRID(stats),
        make_stat_card(
            "RECENT TRANSACTIONS",
            "0",
            "stat-info",
            count_out),
        0, 0, 1, 1);
    gtk_grid_attach(
        GTK_GRID(stats),
        make_stat_card(
            "STORAGE",
            "Durable",
            "stat-success"),
        1, 0, 1, 1);
    gtk_grid_attach(
        GTK_GRID(stats),
        make_stat_card(
            "DETAIL",
            "Exact versions",
            "stat-info"),
        2, 0, 1, 1);
    gtk_box_append(GTK_BOX(page), stats);

    GtkWidget *controls =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(controls, "card");

    GtkWidget *status =
        make_label(
            "Transaction history has not been loaded yet.",
            "card-copy");
    gtk_label_set_wrap(GTK_LABEL(status), true);
    gtk_widget_set_hexpand(status, true);
    gtk_box_append(GTK_BOX(controls), status);
    if (status_out != nullptr) *status_out = status;

    GtkWidget *refresh =
        gtk_button_new_with_label("Refresh history");
    gtk_widget_add_css_class(
        refresh, "control-button");
    if (refresh_callback != nullptr) {
        g_signal_connect(
            refresh,
            "clicked",
            refresh_callback,
            user_data);
    }
    gtk_box_append(GTK_BOX(controls), refresh);
    if (refresh_out != nullptr) *refresh_out = refresh;

    gtk_box_append(GTK_BOX(page), controls);

    GtkWidget *list_widget = gtk_list_box_new();
    GtkListBox *list = GTK_LIST_BOX(list_widget);
    gtk_widget_add_css_class(
        list_widget, "package-list");
    gtk_list_box_set_selection_mode(
        list, GTK_SELECTION_NONE);
    if (list_out != nullptr) *list_out = list;

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, true);
    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER,
        GTK_POLICY_ALWAYS);
    gtk_scrolled_window_set_overlay_scrolling(
        GTK_SCROLLED_WINDOW(scroll), false);
    gtk_scrolled_window_set_child(
        GTK_SCROLLED_WINDOW(scroll),
        list_widget);
    gtk_box_append(GTK_BOX(page), scroll);

    return page;
}

} // namespace infiltrator::software
