// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/theme.hpp"
#include "backends/apt/apt_backend.hpp"
#include "catalogue/repository_catalogue.hpp"
#include "core/model.hpp"

#include <gtk/gtk.h>

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef INFILTRATOR_SOFTWARE_VERSION
#define INFILTRATOR_SOFTWARE_VERSION "0.0.0"
#endif

namespace {

using infiltrator::software::AptBackend;
using infiltrator::software::CatalogueSnapshot;
using infiltrator::software::PackageRecord;
using infiltrator::software::RepositoryCatalogue;
using infiltrator::software::ThemeController;

struct WindowState {
    GtkWindow *window{};
    GtkStack *stack{};
    ThemeController theme;
    GtkWidget *theme_button{};
    GtkStringList *installed_strings{};
    GtkWidget *installed_status{};
    GtkWidget *installed_count{};
    GtkWidget *backend_state{};

    GtkWidget *discover_flow{};
    GtkWidget *discover_search{};
    GtkWidget *discover_category{};
    GtkStringList *discover_categories{};
    GtkListBox *navigation_list{};
    GtkWidget *discover_status{};
    GtkWidget *discover_count{};
    GtkWidget *discover_source{};
    GtkWidget *discover_state{};
    std::vector<PackageRecord> discover_records;
    unsigned int discover_generation{0U};
};

GtkWidget *make_icon(const char *name, int size)
{
    GtkWidget *image = gtk_image_new_from_icon_name(name);
    gtk_image_set_pixel_size(GTK_IMAGE(image), size);
    return image;
}

GtkWidget *make_label(
    const char *text, const char *css_class, float xalign = 0.0F)
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
    GtkWidget **value_out = nullptr)
{
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(card, "stat-card");
    if (semantic_class != nullptr) {
        gtk_widget_add_css_class(card, semantic_class);
    }

    GtkWidget *caption_label = make_label(caption, "stat-caption");
    gtk_box_append(GTK_BOX(card), caption_label);

    GtkWidget *value_label = make_label(value, "stat-value");
    gtk_label_set_ellipsize(GTK_LABEL(value_label), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(card), value_label);

    if (value_out != nullptr) {
        *value_out = value_label;
    }
    return card;
}

GtkWidget *make_page_intro(
    const char *icon_name, const char *title, const char *subtitle)
{
    GtkWidget *hero = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    gtk_widget_add_css_class(hero, "page-hero");

    GtkWidget *icon_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(icon_box, "page-icon");
    GtkWidget *icon = make_icon(icon_name, 28);
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(icon_box), icon);
    gtk_box_append(GTK_BOX(hero), icon_box);

    GtkWidget *identity = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *heading = make_label(title, "hero-title");
    GtkWidget *copy = make_label(subtitle, "hero-subtitle");
    gtk_label_set_wrap(GTK_LABEL(copy), true);
    gtk_box_append(GTK_BOX(identity), heading);
    gtk_box_append(GTK_BOX(identity), copy);
    gtk_box_append(GTK_BOX(hero), identity);

    return hero;
}

GtkWidget *make_foundation_page(
    const char *icon_name,
    const char *title,
    const char *subtitle,
    const char *section_title,
    const char *section_copy,
    const char *semantic_class)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(page, "content");
    if (semantic_class != nullptr) {
        gtk_widget_add_css_class(page, semantic_class);
    }

    gtk_box_append(
        GTK_BOX(page),
        make_page_intro(icon_name, title, subtitle));

    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(card, "card");

    GtkWidget *kicker = make_label("CURRENT MILESTONE", "kicker");
    GtkWidget *heading = make_label(section_title, "card-title");
    GtkWidget *copy = make_label(section_copy, "card-copy");
    gtk_label_set_wrap(GTK_LABEL(copy), true);

    gtk_box_append(GTK_BOX(card), kicker);
    gtk_box_append(GTK_BOX(card), heading);
    gtk_box_append(GTK_BOX(card), copy);
    gtk_box_append(GTK_BOX(page), card);

    return page;
}


struct DiscoverResult {
    CatalogueSnapshot snapshot;
    std::string warning;
};

struct DiscoverTaskData {
    unsigned int generation{0U};
};

struct IconHydrationResult {
    std::vector<PackageRecord> records;
    std::string warning;
};

struct IconHydrationTaskData {
    unsigned int generation{0U};
    std::vector<PackageRecord> records;
};

std::string folded(const std::string_view value)
{
    gchar *text = g_utf8_casefold(
        value.data(), static_cast<gssize>(value.size()));
    if (text == nullptr) {
        return {};
    }
    std::string result(text);
    g_free(text);
    return result;
}

std::string display_size(const std::uint64_t bytes)
{
    std::ostringstream text;
    if (bytes >= 1024U * 1024U) {
        const double mib =
            static_cast<double>(bytes) / (1024.0 * 1024.0);
        text.setf(std::ios::fixed);
        text.precision(mib >= 10.0 ? 0 : 1);
        text << mib << " MiB";
    } else if (bytes >= 1024U) {
        text << (bytes / 1024U) << " KiB";
    } else {
        text << bytes << " B";
    }
    return text.str();
}

const char *category_icon(const std::string_view category) noexcept
{
    if (category == "Productivity") return "accessories-calculator-symbolic";
    if (category == "Automotive") return "applications-engineering-symbolic";
    if (category == "Filesystems") return "drive-harddisk-symbolic";
    if (category == "Development") return "applications-development-symbolic";
    if (category == "Infrastructure") return "network-workgroup-symbolic";
    return "application-x-executable-symbolic";
}

GtkWidget *catalogue_icon(const PackageRecord &record, const int size)
{
    GtkWidget *icon = nullptr;
    if (!record.cached_icon_path.empty()) {
        icon = gtk_image_new_from_file(record.cached_icon_path.c_str());
    } else {
        icon = gtk_image_new_from_icon_name(category_icon(record.category));
    }
    gtk_image_set_pixel_size(GTK_IMAGE(icon), size);
    gtk_widget_add_css_class(icon, "discover-app-icon");
    return icon;
}

void package_record_destroy(gpointer data)
{
    delete static_cast<PackageRecord *>(data);
}

GtkWidget *detail_row(const char *caption, const std::string &value)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *left = make_label(caption, "detail-label");
    gtk_widget_set_size_request(left, 130, -1);
    GtkWidget *right = make_label(
        value.empty() ? "—" : value.c_str(), "detail-value");
    gtk_label_set_wrap(GTK_LABEL(right), true);
    gtk_widget_set_hexpand(right, true);
    gtk_box_append(GTK_BOX(row), left);
    gtk_box_append(GTK_BOX(row), right);
    return row;
}

void discover_details_clicked(GtkButton *button, gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    auto *record = static_cast<PackageRecord *>(
        g_object_get_data(G_OBJECT(button), "discover-record"));
    if (state == nullptr || state->window == nullptr || record == nullptr) {
        return;
    }

    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), record->name.c_str());
    gtk_window_set_default_size(GTK_WINDOW(dialog), 680, 560);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), state->window);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), true);
    gtk_window_set_modal(GTK_WINDOW(dialog), true);

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(outer, "detail-page");
    gtk_widget_set_margin_start(outer, 24);
    gtk_widget_set_margin_end(outer, 24);
    gtk_widget_set_margin_top(outer, 24);
    gtk_widget_set_margin_bottom(outer, 24);
    gtk_window_set_child(GTK_WINDOW(dialog), outer);

    GtkWidget *hero = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_add_css_class(hero, "detail-hero");
    gtk_box_append(GTK_BOX(hero), catalogue_icon(*record, 64));

    GtkWidget *identity = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    GtkWidget *name = make_label(record->name.c_str(), "hero-title");
    GtkWidget *summary = make_label(
        record->description.c_str(), "hero-subtitle");
    gtk_label_set_wrap(GTK_LABEL(summary), true);
    gtk_box_append(GTK_BOX(identity), name);
    gtk_box_append(GTK_BOX(identity), summary);
    gtk_box_append(GTK_BOX(hero), identity);
    gtk_box_append(GTK_BOX(outer), hero);

    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 9);
    gtk_widget_add_css_class(card, "card");
    gtk_widget_add_css_class(card, "card-info");
    gtk_box_append(
        GTK_BOX(card),
        detail_row("Package", record->package_name));
    gtk_box_append(
        GTK_BOX(card),
        detail_row("Version", record->available_version));
    gtk_box_append(
        GTK_BOX(card),
        detail_row("Category", record->category));
    gtk_box_append(
        GTK_BOX(card),
        detail_row("Architecture", record->architecture));
    gtk_box_append(
        GTK_BOX(card),
        detail_row("Publisher", record->publisher));
    gtk_box_append(
        GTK_BOX(card),
        detail_row("Download", display_size(record->download_size_bytes)));
    gtk_box_append(
        GTK_BOX(card),
        detail_row("SHA-256", record->package_sha256));
    gtk_box_append(GTK_BOX(outer), card);

    GtkWidget *status = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *badge = make_label(
        record->state == infiltrator::software::InstallState::installed
            ? "Installed"
            : "Available",
        record->state == infiltrator::software::InstallState::installed
            ? "state-installed"
            : "state-available");
    gtk_box_append(GTK_BOX(status), badge);

    if (!record->source_url.empty()) {
        GtkWidget *source = gtk_link_button_new_with_label(
            record->source_url.c_str(), "Source");
        gtk_box_append(GTK_BOX(status), source);
    }
    if (!record->release_url.empty()) {
        GtkWidget *release = gtk_link_button_new_with_label(
            record->release_url.c_str(), "Release");
        gtk_box_append(GTK_BOX(status), release);
    }
    gtk_box_append(GTK_BOX(outer), status);

    GtkWidget *note = make_label(
        "Installation remains disabled until the transaction planner can show "
        "the complete dependency and system change set before authorization.",
        "detail-note");
    gtk_label_set_wrap(GTK_LABEL(note), true);
    gtk_box_append(GTK_BOX(outer), note);

    gtk_window_present(GTK_WINDOW(dialog));
}

GtkWidget *make_discover_card(
    WindowState *state, const PackageRecord &record)
{
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(card, "discover-card");
    gtk_widget_set_size_request(card, 290, 210);

    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_append(GTK_BOX(header), catalogue_icon(record, 52));

    GtkWidget *identity = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(identity, true);
    GtkWidget *name = make_label(record.name.c_str(), "discover-name");
    GtkWidget *meta = make_label(
        (record.category + "  •  " + record.available_version).c_str(),
        "discover-meta");
    gtk_box_append(GTK_BOX(identity), name);
    gtk_box_append(GTK_BOX(identity), meta);
    gtk_box_append(GTK_BOX(header), identity);
    gtk_box_append(GTK_BOX(card), header);

    GtkWidget *description =
        make_label(record.description.c_str(), "discover-description");
    gtk_label_set_wrap(GTK_LABEL(description), true);
    gtk_label_set_lines(GTK_LABEL(description), 3);
    gtk_label_set_ellipsize(GTK_LABEL(description), PANGO_ELLIPSIZE_END);
    gtk_widget_set_vexpand(description, true);
    gtk_box_append(GTK_BOX(card), description);

    GtkWidget *footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *state_label = nullptr;
    if (record.state == infiltrator::software::InstallState::installed) {
        const std::string installed =
            record.installed_version.empty()
                ? "Installed"
                : "Installed " + record.installed_version;
        state_label =
            make_label(installed.c_str(), "state-installed");
    } else {
        state_label = make_label("Available", "state-available");
    }
    gtk_widget_set_hexpand(state_label, true);
    gtk_box_append(GTK_BOX(footer), state_label);

    GtkWidget *details = gtk_button_new_with_label("Details");
    gtk_widget_add_css_class(details, "discover-details");
    g_object_set_data_full(
        G_OBJECT(details),
        "discover-record",
        new PackageRecord(record),
        package_record_destroy);
    g_signal_connect(
        details, "clicked",
        G_CALLBACK(discover_details_clicked), state);
    gtk_box_append(GTK_BOX(footer), details);
    gtk_box_append(GTK_BOX(card), footer);

    return card;
}

std::string selected_category(WindowState *state)
{
    if (state == nullptr || state->discover_category == nullptr) {
        return "All";
    }

    gpointer item = gtk_drop_down_get_selected_item(
        GTK_DROP_DOWN(state->discover_category));
    if (item == nullptr || !GTK_IS_STRING_OBJECT(item)) {
        return "All";
    }

    const char *value =
        gtk_string_object_get_string(GTK_STRING_OBJECT(item));
    return value == nullptr ? std::string("All") : std::string(value);
}

void rebuild_discover(WindowState *state)
{
    if (state == nullptr || state->discover_flow == nullptr) {
        return;
    }

    GtkWidget *child =
        gtk_widget_get_first_child(state->discover_flow);
    while (child != nullptr) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_flow_box_remove(GTK_FLOW_BOX(state->discover_flow), child);
        child = next;
    }

    const char *search_text =
        state->discover_search == nullptr
            ? ""
            : gtk_editable_get_text(
                  GTK_EDITABLE(state->discover_search));
    const std::string query =
        folded(search_text == nullptr ? "" : search_text);
    const std::string category = selected_category(state);

    std::size_t visible = 0U;
    for (const PackageRecord &record : state->discover_records) {
        if (category != "All" && record.category != category) {
            continue;
        }

        if (!query.empty()) {
            const std::string haystack = folded(
                record.name + "\n" + record.description + "\n" +
                record.category + "\n" + record.package_name);
            if (haystack.find(query) == std::string::npos) {
                continue;
            }
        }

        gtk_flow_box_append(
            GTK_FLOW_BOX(state->discover_flow),
            make_discover_card(state, record));
        ++visible;
    }

    if (state->discover_status != nullptr) {
        std::ostringstream status;
        status << visible << " of " << state->discover_records.size()
               << " applications shown.";
        gtk_label_set_text(
            GTK_LABEL(state->discover_status), status.str().c_str());
    }
}

void discover_filter_changed(GtkWidget *, gpointer user_data)
{
    rebuild_discover(static_cast<WindowState *>(user_data));
}

void discover_category_changed(
    GObject *, GParamSpec *, gpointer user_data)
{
    rebuild_discover(static_cast<WindowState *>(user_data));
}

std::string package_key(std::string value)
{
    const std::size_t colon = value.find(':');
    if (colon != std::string::npos) {
        value.erase(colon);
    }
    return value;
}

void discover_worker(
    GTask *task,
    gpointer,
    gpointer,
    GCancellable *)
{
    auto *result = new DiscoverResult{};

    RepositoryCatalogue catalogue;
    result->snapshot = catalogue.refresh(result->warning);

    AptBackend apt;
    std::string apt_error;
    const std::vector<PackageRecord> installed =
        apt.list_installed(apt_error);

    std::unordered_map<std::string, std::string> versions;
    versions.reserve(installed.size());
    for (const PackageRecord &package : installed) {
        versions.emplace(
            package_key(package.package_name),
            package.installed_version);
    }

    for (PackageRecord &record : result->snapshot.records) {
        const auto found =
            versions.find(package_key(record.package_name));
        if (found != versions.end()) {
            record.state =
                infiltrator::software::InstallState::installed;
            record.installed_version = found->second;
        }
    }

    if (!apt_error.empty() && result->warning.empty()) {
        result->warning =
            "Catalogue loaded, but installed-state detection failed: " +
            apt_error;
    }

    g_task_return_pointer(
        task,
        result,
        [](gpointer data) {
            delete static_cast<DiscoverResult *>(data);
        });
}

void discover_icons_worker(
    GTask *task,
    gpointer,
    gpointer task_data,
    GCancellable *)
{
    auto *data = static_cast<IconHydrationTaskData *>(task_data);
    auto *result = new IconHydrationResult{};
    if (data != nullptr) {
        result->records = data->records;
    }

    RepositoryCatalogue catalogue;
    catalogue.hydrate_icons(result->records, result->warning);

    g_task_return_pointer(
        task,
        result,
        [](gpointer value) {
            delete static_cast<IconHydrationResult *>(value);
        });
}

void discover_icons_complete(
    GObject *source_object,
    GAsyncResult *async_result,
    gpointer)
{
    auto *window = GTK_WINDOW(source_object);
    auto *state = static_cast<WindowState *>(
        g_object_get_data(
            G_OBJECT(window), "infiltrator-window-state"));
    if (state == nullptr) {
        return;
    }

    auto *task_data = static_cast<IconHydrationTaskData *>(
        g_task_get_task_data(G_TASK(async_result)));
    GError *error = nullptr;
    auto *result = static_cast<IconHydrationResult *>(
        g_task_propagate_pointer(G_TASK(async_result), &error));

    if (error != nullptr) {
        g_clear_error(&error);
        return;
    }
    if (result == nullptr || task_data == nullptr ||
        task_data->generation != state->discover_generation) {
        delete result;
        return;
    }

    state->discover_records = std::move(result->records);
    rebuild_discover(state);

    if (!result->warning.empty() &&
        state->discover_status != nullptr) {
        const std::string message =
            "Applications are available; some icons could not be verified: " +
            result->warning;
        gtk_label_set_text(
            GTK_LABEL(state->discover_status), message.c_str());
    }

    delete result;
}

void start_discover_icon_hydration(
    WindowState *state,
    const unsigned int generation)
{
    if (state == nullptr || state->window == nullptr ||
        state->discover_records.empty()) {
        return;
    }

    bool has_remote_icons = false;
    for (const PackageRecord &record : state->discover_records) {
        if (!record.icon_url.empty()) {
            has_remote_icons = true;
            break;
        }
    }
    if (!has_remote_icons) {
        return;
    }

    if (state->discover_status != nullptr) {
        const std::string message =
            std::to_string(state->discover_records.size()) +
            " applications loaded; verifying icons in the background…";
        gtk_label_set_text(
            GTK_LABEL(state->discover_status), message.c_str());
    }

    GTask *task = g_task_new(
        G_OBJECT(state->window),
        nullptr,
        discover_icons_complete,
        nullptr);
    auto *task_data = new IconHydrationTaskData{};
    task_data->generation = generation;
    task_data->records = state->discover_records;
    g_task_set_task_data(
        task,
        task_data,
        [](gpointer value) {
            delete static_cast<IconHydrationTaskData *>(value);
        });
    g_task_run_in_thread(task, discover_icons_worker);
    g_object_unref(task);
}

void discover_complete(
    GObject *source_object,
    GAsyncResult *async_result,
    gpointer)
{
    auto *window = GTK_WINDOW(source_object);
    auto *state = static_cast<WindowState *>(
        g_object_get_data(
            G_OBJECT(window), "infiltrator-window-state"));
    if (state == nullptr) {
        return;
    }

    GError *error = nullptr;
    auto *result = static_cast<DiscoverResult *>(
        g_task_propagate_pointer(G_TASK(async_result), &error));
    if (error != nullptr) {
        if (state->discover_state != nullptr) {
            gtk_label_set_text(
                GTK_LABEL(state->discover_state), "Unavailable");
        }
        if (state->discover_status != nullptr) {
            gtk_label_set_text(
                GTK_LABEL(state->discover_status),
                error->message);
        }
        g_clear_error(&error);
        return;
    }
    if (result == nullptr) {
        return;
    }

    auto *task_data = static_cast<DiscoverTaskData *>(
        g_task_get_task_data(G_TASK(async_result)));
    if (task_data == nullptr ||
        task_data->generation != state->discover_generation) {
        delete result;
        return;
    }

    state->discover_records = std::move(result->snapshot.records);

    std::set<std::string> categories;
    for (const PackageRecord &record : state->discover_records) {
        categories.insert(record.category);
    }

    if (state->discover_categories != nullptr) {
        while (g_list_model_get_n_items(
                   G_LIST_MODEL(state->discover_categories)) > 0U) {
            gtk_string_list_remove(state->discover_categories, 0U);
        }
        gtk_string_list_append(state->discover_categories, "All");
        for (const std::string &category : categories) {
            gtk_string_list_append(
                state->discover_categories, category.c_str());
        }
        gtk_drop_down_set_selected(
            GTK_DROP_DOWN(state->discover_category), 0U);
    }

    if (state->discover_count != nullptr) {
        const std::string count =
            std::to_string(state->discover_records.size());
        gtk_label_set_text(
            GTK_LABEL(state->discover_count), count.c_str());
    }
    if (state->discover_source != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->discover_source),
            result->snapshot.from_cache ? "Repository cache" : "Repository live");
    }
    if (state->discover_state != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->discover_state),
            result->snapshot.from_cache ? "Cached" : "Ready");
    }
    if (state->discover_status != nullptr &&
        !result->warning.empty()) {
        gtk_label_set_text(
            GTK_LABEL(state->discover_status),
            result->warning.c_str());
    }

    rebuild_discover(state);
    start_discover_icon_hydration(
        state, task_data->generation);
    delete result;
}

void refresh_discover(WindowState *state)
{
    if (state == nullptr || state->window == nullptr ||
        state->discover_state == nullptr) {
        return;
    }

    ++state->discover_generation;
    gtk_label_set_text(
        GTK_LABEL(state->discover_state), "Loading");
    if (state->discover_status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->discover_status),
            "Refreshing verified repository metadata…");
    }

    GTask *task = g_task_new(
        G_OBJECT(state->window),
        nullptr,
        discover_complete,
        nullptr);
    auto *task_data = new DiscoverTaskData{};
    task_data->generation = state->discover_generation;
    g_task_set_task_data(
        task,
        task_data,
        [](gpointer data) {
            delete static_cast<DiscoverTaskData *>(data);
        });
    g_task_run_in_thread(task, discover_worker);
    g_object_unref(task);
}

GtkWidget *make_discover_page(WindowState *state)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(page, "content");
    gtk_widget_add_css_class(page, "page-discover");

    gtk_box_append(
        GTK_BOX(page),
        make_page_intro(
            "system-search-symbolic",
            "Discover",
            "Browse verified applications from the Infiltrator repository."));

    GtkWidget *stats = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(stats), 10);
    gtk_grid_set_column_homogeneous(GTK_GRID(stats), true);
    gtk_grid_attach(
        GTK_GRID(stats),
        make_stat_card("APPLICATIONS", "0", "stat-info",
                       &state->discover_count),
        0, 0, 1, 1);
    gtk_grid_attach(
        GTK_GRID(stats),
        make_stat_card("SOURCE", "Repository", "stat-operation",
                       &state->discover_source),
        1, 0, 1, 1);
    gtk_grid_attach(
        GTK_GRID(stats),
        make_stat_card("STATE", "Loading", "stat-success",
                       &state->discover_state),
        2, 0, 1, 1);
    gtk_box_append(GTK_BOX(page), stats);

    GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(controls, "discover-controls");

    state->discover_search = gtk_search_entry_new();
    gtk_search_entry_set_placeholder_text(
        GTK_SEARCH_ENTRY(state->discover_search),
        "Search applications…");
    gtk_widget_set_hexpand(state->discover_search, true);
    g_signal_connect(
        state->discover_search,
        "search-changed",
        G_CALLBACK(discover_filter_changed),
        state);
    gtk_box_append(GTK_BOX(controls), state->discover_search);

    state->discover_categories = gtk_string_list_new(nullptr);
    gtk_string_list_append(state->discover_categories, "All");
    state->discover_category =
        gtk_drop_down_new(
            G_LIST_MODEL(state->discover_categories), nullptr);
    gtk_widget_set_size_request(state->discover_category, 190, -1);
    g_signal_connect(
        state->discover_category,
        "notify::selected",
        G_CALLBACK(discover_category_changed),
        state);
    gtk_box_append(GTK_BOX(controls), state->discover_category);
    gtk_box_append(GTK_BOX(page), controls);

    state->discover_status = make_label(
        "Refreshing verified repository metadata…",
        "discover-status");
    gtk_box_append(GTK_BOX(page), state->discover_status);

    state->discover_flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(
        GTK_FLOW_BOX(state->discover_flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_row_spacing(
        GTK_FLOW_BOX(state->discover_flow), 12U);
    gtk_flow_box_set_column_spacing(
        GTK_FLOW_BOX(state->discover_flow), 12U);
    gtk_flow_box_set_min_children_per_line(
        GTK_FLOW_BOX(state->discover_flow), 1U);
    gtk_flow_box_set_max_children_per_line(
        GTK_FLOW_BOX(state->discover_flow), 3U);
    gtk_widget_set_valign(
        state->discover_flow, GTK_ALIGN_START);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, true);
    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER,
        GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(
        GTK_SCROLLED_WINDOW(scroll),
        state->discover_flow);
    gtk_box_append(GTK_BOX(page), scroll);

    refresh_discover(state);
    return page;
}

void list_item_setup(GtkSignalListItemFactory *, GtkListItem *item, gpointer)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(row, "package-row");

    GtkWidget *icon = make_icon("application-x-executable-symbolic", 20);
    gtk_widget_add_css_class(icon, "package-icon");
    gtk_box_append(GTK_BOX(row), icon);

    GtkWidget *label = gtk_label_new(nullptr);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0F);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(label, true);
    gtk_box_append(GTK_BOX(row), label);

    gtk_list_item_set_child(item, row);
}

void list_item_bind(GtkSignalListItemFactory *, GtkListItem *item, gpointer)
{
    GObject *object = G_OBJECT(gtk_list_item_get_item(item));
    GtkWidget *row = gtk_list_item_get_child(item);
    if (object == nullptr || row == nullptr) {
        return;
    }

    GtkWidget *label = gtk_widget_get_last_child(row);
    if (label == nullptr) {
        return;
    }

    const char *text = gtk_string_object_get_string(GTK_STRING_OBJECT(object));
    gtk_label_set_text(GTK_LABEL(label), text);
}

void refresh_installed(WindowState *state)
{
    if (state == nullptr || state->installed_strings == nullptr) {
        return;
    }

    while (g_list_model_get_n_items(G_LIST_MODEL(state->installed_strings)) > 0U) {
        gtk_string_list_remove(state->installed_strings, 0U);
    }

    AptBackend backend;
    std::string error;
    const std::vector<PackageRecord> packages = backend.list_installed(error);

    for (const PackageRecord &package : packages) {
        const std::string row =
            package.name + "    " + package.installed_version;
        gtk_string_list_append(state->installed_strings, row.c_str());
    }

    if (state->installed_status != nullptr) {
        std::ostringstream message;
        if (!error.empty()) {
            message << "Installed inventory unavailable: " << error;
        } else {
            message << packages.size()
                    << " installed packages reported by the read-only APT backend.";
        }
        gtk_label_set_text(
            GTK_LABEL(state->installed_status), message.str().c_str());
    }

    if (state->installed_count != nullptr) {
        const std::string count = std::to_string(packages.size());
        gtk_label_set_text(GTK_LABEL(state->installed_count), count.c_str());
    }

    if (state->backend_state != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->backend_state),
            error.empty() ? "Ready" : "Unavailable");
    }
}

GtkWidget *make_installed_page(WindowState *state)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(page, "content");
    gtk_widget_add_css_class(page, "page-installed");

    gtk_box_append(
        GTK_BOX(page),
        make_page_intro(
            "view-list-symbolic",
            "Installed",
            "Software currently present on this system."));

    GtkWidget *stats = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(stats), 10);
    gtk_grid_set_column_homogeneous(GTK_GRID(stats), true);
    gtk_grid_attach(
        GTK_GRID(stats),
        make_stat_card("PACKAGES", "0", "stat-info", &state->installed_count),
        0, 0, 1, 1);
    gtk_grid_attach(
        GTK_GRID(stats),
        make_stat_card("BACKEND", "APT/.deb", "stat-operation"),
        1, 0, 1, 1);
    gtk_grid_attach(
        GTK_GRID(stats),
        make_stat_card("STATE", "Loading", "stat-success", &state->backend_state),
        2, 0, 1, 1);
    gtk_box_append(GTK_BOX(page), stats);

    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(card, "card");
    gtk_widget_add_css_class(card, "card-info");
    gtk_widget_set_vexpand(card, true);

    GtkWidget *heading = make_label("Installed packages", "card-title");
    gtk_box_append(GTK_BOX(card), heading);

    state->installed_status = make_label(
        "Reading installed package inventory…", "card-copy");
    gtk_label_set_wrap(GTK_LABEL(state->installed_status), true);
    gtk_box_append(GTK_BOX(card), state->installed_status);

    state->installed_strings = gtk_string_list_new(nullptr);

    GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(list_item_setup), nullptr);
    g_signal_connect(factory, "bind", G_CALLBACK(list_item_bind), nullptr);

    GtkSelectionModel *selection = GTK_SELECTION_MODEL(
        gtk_single_selection_new(G_LIST_MODEL(state->installed_strings)));
    GtkWidget *list = gtk_list_view_new(selection, factory);
    gtk_widget_add_css_class(list, "package-list");
    gtk_widget_set_vexpand(list, true);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, true);
    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);
    gtk_box_append(GTK_BOX(card), scroll);
    gtk_box_append(GTK_BOX(page), card);

    refresh_installed(state);
    return page;
}

GtkWidget *make_nav_row(
    const char *icon_name, const char *text, const char *semantic_class)
{
    GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 11);
    GtkWidget *icon = make_icon(icon_name, 18);
    GtkWidget *label = make_label(text, "nav-label");
    gtk_widget_set_hexpand(label, true);
    gtk_box_append(GTK_BOX(row_box), icon);
    gtk_box_append(GTK_BOX(row_box), label);

    GtkWidget *row = gtk_list_box_row_new();
    gtk_widget_add_css_class(row, "nav-row");
    if (semantic_class != nullptr) {
        gtk_widget_add_css_class(row, semantic_class);
    }
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), row_box);
    return row;
}

void navigation_changed(
    GtkListBox *, GtkListBoxRow *row, gpointer user_data)
{
    if (row == nullptr || user_data == nullptr) {
        return;
    }

    static constexpr const char *page_names[] = {
        "discover", "installed", "updates", "system",
        "repositories", "history", "repair"
    };

    const int index = gtk_list_box_row_get_index(row);
    if (index < 0 || index >= 7) {
        return;
    }

    auto *state = static_cast<WindowState *>(user_data);
    gtk_stack_set_visible_child_name(state->stack, page_names[index]);
}

GtkWidget *make_navigation(WindowState *state)
{
    static constexpr const char *labels[] = {
        "Discover", "Installed", "Updates", "System",
        "Repositories", "History", "Repair"
    };
    static constexpr const char *icons[] = {
        "system-search-symbolic",
        "view-list-symbolic",
        "software-update-available-symbolic",
        "computer-symbolic",
        "network-workgroup-symbolic",
        "document-open-recent-symbolic",
        "dialog-warning-symbolic"
    };
    static constexpr const char *semantic_classes[] = {
        "nav-discover", "nav-installed", "nav-updates", "nav-system",
        "nav-repositories", "nav-history", "nav-repair"
    };

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(outer, 248, -1);
    gtk_widget_add_css_class(outer, "sidebar");

    GtkWidget *section = make_label("SOFTWARE", "sidebar-title");
    gtk_widget_set_margin_start(section, 16);
    gtk_widget_set_margin_end(section, 16);
    gtk_widget_set_margin_top(section, 17);
    gtk_widget_set_margin_bottom(section, 9);
    gtk_box_append(GTK_BOX(outer), section);

    GtkWidget *list = gtk_list_box_new();
    state->navigation_list = GTK_LIST_BOX(list);
    gtk_widget_add_css_class(list, "nav-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_SINGLE);
    gtk_widget_set_margin_start(list, 8);
    gtk_widget_set_margin_end(list, 8);
    gtk_widget_set_vexpand(list, true);
    gtk_box_append(GTK_BOX(outer), list);

    for (int i = 0; i < 7; ++i) {
        gtk_list_box_append(
            GTK_LIST_BOX(list),
            make_nav_row(icons[i], labels[i], semantic_classes[i]));
    }

    g_signal_connect(
        list, "row-selected", G_CALLBACK(navigation_changed), state);

    GtkWidget *footer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(footer, "sidebar-footer");
    GtkWidget *backend = make_label("APT/.deb backend", "sidebar-note");
    GtkWidget *common = make_label("Common 1.19.10", "sidebar-note");
    gtk_box_append(GTK_BOX(footer), backend);
    gtk_box_append(GTK_BOX(footer), common);
    gtk_box_append(GTK_BOX(outer), footer);

    return outer;
}

void update_theme_button(WindowState *state)
{
    if (state == nullptr || state->theme_button == nullptr) {
        return;
    }

    const std::string label =
        std::string("Theme: ") + state->theme.mode_name();
    gtk_button_set_label(GTK_BUTTON(state->theme_button), label.c_str());
}

void theme_clicked(GtkButton *, gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    if (state == nullptr) {
        return;
    }

    state->theme.cycle_mode();
    update_theme_button(state);
}

void refresh_clicked(GtkButton *, gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    refresh_installed(state);
    refresh_discover(state);
}

void about_clicked(GtkButton *, gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    if (state == nullptr || state->window == nullptr) {
        return;
    }

    GtkWidget *dialog = gtk_about_dialog_new();
    gtk_about_dialog_set_program_name(
        GTK_ABOUT_DIALOG(dialog), "Infiltrator Software");
    gtk_about_dialog_set_version(
        GTK_ABOUT_DIALOG(dialog), INFILTRATOR_SOFTWARE_VERSION);
    gtk_about_dialog_set_comments(
        GTK_ABOUT_DIALOG(dialog),
        "Unified software management and updates for the Infiltrator project family.");
    gtk_about_dialog_set_website(
        GTK_ABOUT_DIALOG(dialog),
        "https://github.com/Infiltrator-Projects/Software");
    gtk_about_dialog_set_license_type(
        GTK_ABOUT_DIALOG(dialog), GTK_LICENSE_GPL_3_0);

    static const char *authors[] = {"Shannon Smith", nullptr};
    gtk_about_dialog_set_authors(GTK_ABOUT_DIALOG(dialog), authors);

    gtk_window_set_transient_for(GTK_WINDOW(dialog), state->window);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), true);
    gtk_window_set_modal(GTK_WINDOW(dialog), true);
    gtk_window_present(GTK_WINDOW(dialog));
}

GtkWidget *make_header_bar(WindowState *state)
{
    GtkWidget *bar = gtk_header_bar_new();
    gtk_widget_add_css_class(bar, "infiltrator-titlebar");
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(bar), true);

    GtkWidget *title_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_halign(title_box, GTK_ALIGN_CENTER);

    GtkWidget *title = make_label(
        "Infiltrator Software", "titlebar-title", 0.5F);
    GtkWidget *subtitle = make_label(
        "Software management & updates", "titlebar-subtitle", 0.5F);
    gtk_box_append(GTK_BOX(title_box), title);
    gtk_box_append(GTK_BOX(title_box), subtitle);
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(bar), title_box);

    GtkWidget *refresh =
        gtk_button_new_from_icon_name("view-refresh-symbolic");
    gtk_widget_set_tooltip_text(
        refresh, "Refresh installed software information");
    gtk_widget_add_css_class(refresh, "titlebar-button");
    g_signal_connect(
        refresh, "clicked", G_CALLBACK(refresh_clicked), state);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(bar), refresh);

    state->theme_button = gtk_button_new_with_label("Theme: System");
    gtk_widget_set_tooltip_text(
        state->theme_button,
        "Cycle Follow OS, Day and Night themes");
    gtk_widget_add_css_class(state->theme_button, "titlebar-button");
    g_signal_connect(
        state->theme_button, "clicked", G_CALLBACK(theme_clicked), state);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(bar), state->theme_button);

    GtkWidget *about = gtk_button_new_from_icon_name("help-about-symbolic");
    gtk_widget_set_tooltip_text(about, "About Infiltrator Software");
    gtk_widget_add_css_class(about, "titlebar-button");
    g_signal_connect(
        about, "clicked", G_CALLBACK(about_clicked), state);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(bar), about);

    update_theme_button(state);
    return bar;
}

GtkWidget *make_status_bar()
{
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(bar, "statusbar");

    GtkWidget *ready = make_label("Ready", "statusbar-text");
    gtk_widget_set_hexpand(ready, true);
    gtk_box_append(GTK_BOX(bar), ready);

    const std::string version =
        std::string("Version ") + INFILTRATOR_SOFTWARE_VERSION;
    GtkWidget *version_label =
        make_label(version.c_str(), "statusbar-text", 1.0F);
    gtk_box_append(GTK_BOX(bar), version_label);

    return bar;
}

void destroy_window_state(gpointer data)
{
    auto *state = static_cast<WindowState *>(data);
    if (state == nullptr) {
        return;
    }

    if (state->installed_strings != nullptr) {
        g_object_unref(state->installed_strings);
        state->installed_strings = nullptr;
    }
    if (state->discover_categories != nullptr) {
        g_object_unref(state->discover_categories);
        state->discover_categories = nullptr;
    }
    delete state;
}

void activate(GtkApplication *application, gpointer)
{
    GtkWidget *window = gtk_application_window_new(application);
    gtk_window_set_title(GTK_WINDOW(window), "Infiltrator Software");
    gtk_window_set_default_size(GTK_WINDOW(window), 1220, 780);
    gtk_widget_set_size_request(window, 940, 620);

    auto *state = new WindowState{};
    state->window = GTK_WINDOW(window);
    state->theme.initialise();

    g_object_set_data_full(
        G_OBJECT(window), "infiltrator-window-state",
        state, destroy_window_state);

    GtkWidget *header = make_header_bar(state);
    gtk_window_set_titlebar(GTK_WINDOW(window), header);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(window), root);

    GtkWidget *body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_vexpand(body, true);
    gtk_box_append(GTK_BOX(root), body);

    GtkWidget *stack = gtk_stack_new();
    state->stack = GTK_STACK(stack);
    gtk_stack_set_transition_type(
        state->stack, GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_widget_set_hexpand(stack, true);
    gtk_widget_set_vexpand(stack, true);

    gtk_box_append(GTK_BOX(body), make_navigation(state));
    gtk_box_append(GTK_BOX(body), stack);

    gtk_stack_add_named(
        state->stack,
        make_discover_page(state),
        "discover");

    gtk_stack_add_named(
        state->stack,
        make_installed_page(state),
        "installed");

    gtk_stack_add_named(
        state->stack,
        make_foundation_page(
            "software-update-available-symbolic",
            "Updates",
            "Application and system updates in one place.",
            "Transaction planning first",
            "Updates remain read-only until dependency resolution, complete "
            "change-set presentation and privilege separation are proven.",
            "page-updates"),
        "updates");

    gtk_stack_add_named(
        state->stack,
        make_foundation_page(
            "computer-symbolic",
            "System",
            "Kernels, drivers and core operating-system components.",
            "System changes stay distinct",
            "System-critical updates will remain visually and operationally "
            "distinct without forcing a second updater application.",
            "page-system"),
        "system");

    gtk_stack_add_named(
        state->stack,
        make_foundation_page(
            "network-workgroup-symbolic",
            "Repositories",
            "Sources, priorities and Stable/Beta/Alpha channels.",
            "Repository policy",
            "Repository and channel management will be exposed here without "
            "requiring manual editing of package-source files.",
            "page-repositories"),
        "repositories");

    gtk_stack_add_named(
        state->stack,
        make_foundation_page(
            "document-open-recent-symbolic",
            "History",
            "Exact software-management operations and outcomes.",
            "Durable transaction history",
            "Future write operations will record exact before/after versions, "
            "source provenance and recovery linkage.",
            "page-history"),
        "history");

    gtk_stack_add_named(
        state->stack,
        make_foundation_page(
            "dialog-warning-symbolic",
            "Repair",
            "Diagnose interrupted package and repository state.",
            "Repair is explicit",
            "Broken dependencies, interrupted transactions and inconsistent "
            "repository state will be diagnosed here rather than hidden.",
            "page-repair"),
        "repair");

    gtk_stack_set_visible_child_name(state->stack, "discover");
    if (state->navigation_list != nullptr) {
        GtkListBoxRow *first = gtk_list_box_get_row_at_index(
            state->navigation_list, 0);
        gtk_list_box_select_row(state->navigation_list, first);
    }
    gtk_box_append(GTK_BOX(root), make_status_bar());

    gtk_window_present(GTK_WINDOW(window));
}

} // namespace

int main(int argc, char **argv)
{
    GtkApplication *application = gtk_application_new(
        "net.ssmith.infiltrator.software", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(application, "activate", G_CALLBACK(activate), nullptr);

    const int status =
        g_application_run(G_APPLICATION(application), argc, argv);
    g_object_unref(application);
    return status;
}
