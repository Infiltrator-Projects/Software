// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/installed_controller.hpp"

#include "app/installed_inventory.hpp"
#include "app/ui_components.hpp"
#include "catalogue/system_catalogue.hpp"
#include "core/model.hpp"

#include <algorithm>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace infiltrator::software {
namespace {

struct InstalledResult {
    unsigned int generation{0U};
    std::vector<PackageRecord> records;
    std::string error;
    std::string flatpak_warning;
    bool from_engine{false};
    std::size_t flatpak_count{0U};
};

struct InstalledTaskData {
    unsigned int generation{0U};
};

void list_item_setup(
    GtkSignalListItemFactory *,
    GtkListItem *item,
    gpointer)
{
    GtkWidget *row =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(row, "package-row");

    GtkWidget *icon =
        make_icon(
            "application-x-executable-symbolic", 20);
    gtk_widget_add_css_class(icon, "package-icon");
    gtk_box_append(GTK_BOX(row), icon);

    GtkWidget *label = gtk_label_new(nullptr);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0F);
    gtk_label_set_ellipsize(
        GTK_LABEL(label),
        PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(label, true);
    gtk_box_append(GTK_BOX(row), label);

    gtk_list_item_set_child(item, row);
}

void list_item_bind(
    GtkSignalListItemFactory *,
    GtkListItem *item,
    gpointer)
{
    GObject *object =
        G_OBJECT(gtk_list_item_get_item(item));
    GtkWidget *row =
        gtk_list_item_get_child(item);
    if (object == nullptr || row == nullptr) {
        return;
    }

    GtkWidget *label =
        gtk_widget_get_last_child(row);
    if (label == nullptr) return;

    const char *text =
        gtk_string_object_get_string(
            GTK_STRING_OBJECT(object));
    gtk_label_set_text(GTK_LABEL(label), text);
}

void installed_worker(
    GTask *task,
    gpointer,
    gpointer task_data,
    GCancellable *)
{
    auto *data =
        static_cast<InstalledTaskData *>(task_data);
    auto *result = new InstalledResult{};
    result->generation =
        data == nullptr ? 0U : data->generation;
    result->records =
        read_installed_packages(
            result->error,
            &result->from_engine);

    SystemCatalogue system_catalogue;
    std::string flatpak_error;
    std::vector<PackageRecord> flatpaks =
        system_catalogue.installed_flatpaks(
            flatpak_error);
    result->flatpak_count = flatpaks.size();
    result->flatpak_warning = flatpak_error;
    result->records.insert(
        result->records.end(),
        std::make_move_iterator(flatpaks.begin()),
        std::make_move_iterator(flatpaks.end()));
    std::sort(
        result->records.begin(),
        result->records.end(),
        [](const PackageRecord &left,
           const PackageRecord &right) {
            if (left.name != right.name) {
                return left.name < right.name;
            }
            return left.source < right.source;
        });

    g_task_return_pointer(
        task,
        result,
        [](gpointer pointer) {
            delete static_cast<InstalledResult *>(pointer);
        });
}

void installed_complete(
    GObject *source_object,
    GAsyncResult *async_result,
    gpointer user_data)
{
    auto *controller =
        static_cast<InstalledController *>(user_data);
    auto *result =
        static_cast<InstalledResult *>(
            g_task_propagate_pointer(
                G_TASK(async_result), nullptr));

    if (controller == nullptr || result == nullptr) {
        delete result;
        return;
    }
    if (source_object != G_OBJECT(controller->window) ||
        result->generation != controller->generation) {
        delete result;
        return;
    }

    controller->busy = false;

    std::vector<std::string> rows;
    rows.reserve(result->records.size());
    for (const PackageRecord &package : result->records) {
        rows.emplace_back(
            package.name + "    " +
            package.installed_version);
    }

    std::vector<const char *> additions;
    additions.reserve(rows.size() + 1U);
    for (const std::string &row : rows) {
        additions.push_back(row.c_str());
    }
    additions.push_back(nullptr);

    gtk_string_list_splice(
        controller->strings,
        0U,
        g_list_model_get_n_items(
            G_LIST_MODEL(controller->strings)),
        additions.data());

    if (controller->status != nullptr) {
        std::ostringstream message;
        if (!result->error.empty()) {
            message
                << "Installed inventory unavailable: "
                << result->error;
        } else {
            message
                << result->records.size()
                << " installed software items read from "
                << (result->from_engine
                        ? "the shared native package engine"
                        : "Debian package state")
                << " and Flatpak.";
            if (!result->flatpak_warning.empty()) {
                message
                    << " Flatpak inventory warning: "
                    << result->flatpak_warning;
            }
        }
        gtk_label_set_text(
            GTK_LABEL(controller->status),
            message.str().c_str());
    }
    if (controller->count != nullptr) {
        const std::string count =
            std::to_string(result->records.size());
        gtk_label_set_text(
            GTK_LABEL(controller->count),
            count.c_str());
    }
    if (controller->backend != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(controller->backend),
            result->from_engine
                ? (result->flatpak_count > 0U
                       ? "Native + Flatpak"
                       : "Native engine")
                : (result->flatpak_count > 0U
                       ? "Debian + Flatpak"
                       : "Debian state"));
    }
    if (controller->backend_state != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(controller->backend_state),
            result->error.empty()
                ? "Ready"
                : "Unavailable");
    }

    delete result;
}

} // namespace

void refresh_installed_controller(
    InstalledController *controller)
{
    if (controller == nullptr ||
        controller->strings == nullptr ||
        controller->window == nullptr ||
        controller->busy) {
        return;
    }

    controller->loaded = true;
    controller->busy = true;
    ++controller->generation;

    if (controller->status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(controller->status),
            "Loading installed packages from shared state…");
    }
    if (controller->backend_state != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(controller->backend_state),
            "Loading");
    }

    auto *data = new InstalledTaskData{
        controller->generation};
    GTask *task =
        g_task_new(
            G_OBJECT(controller->window),
            nullptr,
            installed_complete,
            controller);
    g_task_set_task_data(
        task,
        data,
        [](gpointer pointer) {
            delete static_cast<InstalledTaskData *>(
                pointer);
        });
    g_task_run_in_thread(task, installed_worker);
    g_object_unref(task);
}

GtkWidget *create_installed_page(
    InstalledController *controller,
    GtkWindow *window)
{
    if (controller == nullptr) {
        return gtk_box_new(
            GTK_ORIENTATION_VERTICAL, 0);
    }
    controller->window = window;

    GtkWidget *page =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(page, "content");
    gtk_widget_add_css_class(
        page, "page-installed");

    gtk_box_append(
        GTK_BOX(page),
        make_page_intro(
            "view-list-symbolic",
            "Installed",
            "Software currently present on this system."));

    GtkWidget *stats = gtk_grid_new();
    gtk_grid_set_column_spacing(
        GTK_GRID(stats), 10);
    gtk_grid_set_column_homogeneous(
        GTK_GRID(stats), true);
    gtk_grid_attach(
        GTK_GRID(stats),
        make_stat_card(
            "PACKAGES",
            "0",
            "stat-info",
            &controller->count),
        0, 0, 1, 1);
    gtk_grid_attach(
        GTK_GRID(stats),
        make_stat_card(
            "BACKEND",
            "Loading",
            "stat-operation",
            &controller->backend),
        1, 0, 1, 1);
    gtk_grid_attach(
        GTK_GRID(stats),
        make_stat_card(
            "STATE",
            "Loading",
            "stat-success",
            &controller->backend_state),
        2, 0, 1, 1);
    gtk_box_append(GTK_BOX(page), stats);

    GtkWidget *card =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(card, "card");
    gtk_widget_add_css_class(card, "card-info");
    gtk_widget_set_vexpand(card, true);

    gtk_box_append(
        GTK_BOX(card),
        make_label(
            "Installed packages", "card-title"));

    controller->status =
        make_label(
            "Reading installed package inventory…",
            "card-copy");
    gtk_label_set_wrap(
        GTK_LABEL(controller->status), true);
    gtk_box_append(
        GTK_BOX(card), controller->status);

    controller->strings =
        gtk_string_list_new(nullptr);

    GtkListItemFactory *factory =
        gtk_signal_list_item_factory_new();
    g_signal_connect(
        factory,
        "setup",
        G_CALLBACK(list_item_setup),
        nullptr);
    g_signal_connect(
        factory,
        "bind",
        G_CALLBACK(list_item_bind),
        nullptr);

    GtkSelectionModel *selection =
        GTK_SELECTION_MODEL(
            gtk_single_selection_new(
                G_LIST_MODEL(
                    controller->strings)));
    GtkWidget *list =
        gtk_list_view_new(selection, factory);
    gtk_widget_add_css_class(
        list, "package-list");
    gtk_widget_set_vexpand(list, true);

    GtkWidget *scroll =
        gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, true);
    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER,
        GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(
        GTK_SCROLLED_WINDOW(scroll), list);
    gtk_box_append(GTK_BOX(card), scroll);
    gtk_box_append(GTK_BOX(page), card);

    return page;
}

} // namespace infiltrator::software
