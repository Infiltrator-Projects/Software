// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/transaction_review.hpp"

#include "app/ui_components.hpp"

#include <infiltratr/core.h>

#include <cstdint>
#include <sstream>
#include <string>

namespace infiltrator::software::app {
namespace {

std::string display_size(const std::uint64_t bytes)
{
    char text[32];
    const char *formatted =
        infiltratr_format_bytes(bytes, text, sizeof(text));
    return formatted != nullptr ? std::string(formatted) : std::string();
}

const char *transaction_action_label(const TransactionAction action) noexcept
{
    switch (action) {
    case TransactionAction::install: return "Install";
    case TransactionAction::upgrade: return "Upgrade";
    case TransactionAction::remove: return "Remove";
    }
    return "Change";
}

std::string display_disk_delta(const std::int64_t bytes)
{
    if (bytes == 0) return "0 B";
    const bool negative = bytes < 0;
    const std::uint64_t magnitude =
        negative
            ? static_cast<std::uint64_t>(-(bytes + 1)) + 1U
            : static_cast<std::uint64_t>(bytes);
    return std::string(negative ? "−" : "+") + display_size(magnitude);
}

std::string transaction_item_text(const TransactionItem &item)
{
    std::ostringstream text;
    text << transaction_action_label(item.action) << "  " << item.package_id;
    if (item.action == TransactionAction::upgrade &&
        !item.from_version.empty()) {
        text << "  " << item.from_version << " → " << item.to_version;
    } else if (item.action == TransactionAction::install &&
               !item.to_version.empty()) {
        text << "  → " << item.to_version;
    } else if (item.action == TransactionAction::remove &&
               !item.from_version.empty()) {
        text << "  " << item.from_version << " → removed";
    }
    text << (item.requested ? "  [requested]" : "  [dependency]");
    if (item.system_critical) text << "  [system-critical]";
    if (!item.source.empty()) text << "\nSource: " << item.source;
    return text.str();
}

} // namespace

GtkWidget *make_transaction_review_dialog(
    GtkWindow *parent,
    const char *title,
    const std::string &heading,
    const char *accept_label,
    const TransactionPlan &plan)
{
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        title,
        parent,
        static_cast<GtkDialogFlags>(
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "Cancel", GTK_RESPONSE_CANCEL,
        accept_label, GTK_RESPONSE_ACCEPT,
        nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 720, 560);

    GtkWidget *content =
        gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    G_GNUC_END_IGNORE_DEPRECATIONS
    gtk_box_set_spacing(GTK_BOX(content), 12);
    gtk_widget_set_margin_start(content, 18);
    gtk_widget_set_margin_end(content, 18);
    gtk_widget_set_margin_top(content, 16);
    gtk_widget_set_margin_bottom(content, 16);

    GtkWidget *heading_label =
        infiltrator::software::make_label(heading.c_str(), "hero-title");
    gtk_label_set_wrap(GTK_LABEL(heading_label), true);
    gtk_box_append(GTK_BOX(content), heading_label);

    std::ostringstream summary;
    summary << plan.items.size()
            << (plan.items.size() == 1U
                    ? " package change."
                    : " package changes.");
    if (plan.download_bytes > 0U) {
        summary << "  Download: " << display_size(plan.download_bytes) << ".";
    }
    if (plan.disk_delta_bytes != 0) {
        summary << "  Disk change: "
                << display_disk_delta(plan.disk_delta_bytes) << ".";
    }
    if (plan.touches_system) {
        summary << "\nThis transaction includes system-critical components.";
    }
    summary << "\nResolved by the native package engine against state "
            << "generation " << plan.state_generation << ".";

    GtkWidget *summary_label =
        infiltrator::software::make_label(
            summary.str().c_str(), "detail-note");
    gtk_label_set_wrap(GTK_LABEL(summary_label), true);
    gtk_label_set_selectable(GTK_LABEL(summary_label), true);
    gtk_box_append(GTK_BOX(content), summary_label);

    GtkWidget *changes =
        infiltrator::software::make_label(
            "Complete resolved change set", "card-title");
    gtk_box_append(GTK_BOX(content), changes);

    GtkWidget *scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(scroller),
        GTK_POLICY_AUTOMATIC,
        GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroller, true);
    gtk_widget_set_size_request(scroller, -1, 300);

    GtkWidget *list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    for (const auto &item : plan.items) {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
        gtk_widget_add_css_class(row, "card");
        GtkWidget *label =
            infiltrator::software::make_label(
                transaction_item_text(item).c_str(), "card-copy");
        gtk_label_set_wrap(GTK_LABEL(label), true);
        gtk_label_set_selectable(GTK_LABEL(label), true);
        gtk_box_append(GTK_BOX(row), label);
        gtk_box_append(GTK_BOX(list), row);
    }
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), list);
    gtk_box_append(GTK_BOX(content), scroller);
    return dialog;
}

} // namespace infiltrator::software::app
