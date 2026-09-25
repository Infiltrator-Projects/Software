// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/theme.hpp"
#include "backends/apt/apt_backend.hpp"
#include "catalogue/repository_catalogue.hpp"
#include "catalogue/catalogue_snapshot_store.hpp"
#include "catalogue/system_catalogue.hpp"
#include "client/engine_client.hpp"
#include "core/model.hpp"
#include "core/transaction_history.hpp"
#include "core/update_freshness.hpp"
#include "sources/source_inventory.hpp"

#include <gtk/gtk.h>
#include <infiltratr/core.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifndef INFILTRATOR_SOFTWARE_VERSION
#define INFILTRATOR_SOFTWARE_VERSION "0.0.0"
#endif

namespace {

using infiltrator::software::AptBackend;
using infiltrator::software::CatalogueSnapshot;
using infiltrator::software::CatalogueSnapshotStore;
using infiltrator::software::EngineClient;
using infiltrator::software::PackageRecord;
using infiltrator::software::RepositoryCatalogue;
using infiltrator::software::SourceInventory;
using infiltrator::software::SourceRecord;
using infiltrator::software::SystemCatalogue;
using infiltrator::software::ThemeController;
using infiltrator::software::TransactionAction;
using infiltrator::software::TransactionHistoryItem;
using infiltrator::software::TransactionHistoryStore;
using infiltrator::software::TransactionPlan;
using infiltrator::software::source_kind_name;
using infiltrator::software::update_metadata_refresh_due;

struct WindowState {
    GtkWindow *window{};
    GtkStack *stack{};
    ThemeController theme;
    GtkWidget *theme_button{};
    GtkStringList *installed_strings{};
    GtkWidget *installed_status{};
    GtkWidget *installed_count{};
    GtkWidget *installed_backend{};
    GtkWidget *backend_state{};
    unsigned int installed_generation{0U};
    bool installed_busy{false};

    GtkStringList *discover_visible{};
    GtkWidget *discover_search{};
    GtkWidget *discover_category{};
    GtkStringList *discover_categories{};
    GtkListBox *navigation_list{};
    GtkWidget *discover_status{};
    GtkWidget *discover_count{};
    GtkWidget *discover_source{};
    GtkWidget *discover_state{};
    std::vector<PackageRecord> discover_records;
    std::vector<std::string> discover_search_texts;
    unsigned int discover_generation{0U};

    GtkWidget *repository_flow{};
    GtkWidget *repository_count{};
    GtkWidget *repository_status{};
    unsigned int repositories_generation{0U};
    bool repositories_busy{false};

    GtkListBox *updates_list{};
    GtkWidget *updates_status{};
    GtkWidget *updates_count{};
    GtkWidget *updates_critical{};
    GtkWidget *updates_install{};
    GtkWidget *updates_refresh{};
    GtkWidget *updates_backend{};
    GtkWidget *updates_progress{};
    guint updates_progress_timer_id{0U};
    gint64 updates_progress_started_us{0};
    bool updates_post_install_refresh{false};
    std::vector<PackageRecord> update_records;
    std::unordered_set<std::string> selected_update_ids;
    std::optional<TransactionPlan> pending_update_plan;
    unsigned int updates_generation{0U};
    bool updates_busy{false};
    bool updates_from_engine{false};
    bool updates_auto_refresh_pending{true};
    gint64 updates_last_metadata_refresh_us{0};
    guint updates_refresh_timer_id{0U};

    GtkListBox *system_list{};
    GtkWidget *system_status{};
    GtkWidget *system_count{};
    GtkWidget *system_updates{};
    GtkWidget *system_critical{};
    GtkWidget *system_refresh{};
    GtkWidget *system_review_updates{};
    std::vector<PackageRecord> system_records;
    std::vector<PackageRecord> system_update_records;
    unsigned int system_generation{0U};
    bool system_busy{false};

    GtkListBox *history_list{};
    GtkWidget *history_status{};
    GtkWidget *history_count{};
    GtkWidget *history_refresh{};
    std::vector<TransactionHistoryItem> history_records;
    unsigned int history_generation{0U};
    bool history_busy{false};

    bool window_presented{false};
    bool discover_loaded{false};
    bool installed_loaded{false};
    bool updates_loaded{false};
    bool system_loaded{false};
    bool repositories_loaded{false};
    bool history_loaded{false};
};

void refresh_repositories(WindowState *state);
void refresh_updates(WindowState *state, bool refresh_metadata = false);
void refresh_discover(WindowState *state, bool force_refresh);
void refresh_installed(WindowState *state);
void refresh_system(WindowState *state, bool refresh_metadata = false);
void refresh_history(WindowState *state);
void discover_install_clicked(GtkButton *button, gpointer user_data);

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
    std::size_t added{0U};
    std::size_t removed{0U};
};

struct DiscoverTaskData {
    unsigned int generation{0U};
    bool force_refresh{false};
};

struct DiscoverInstalledResult {
    std::vector<PackageRecord> installed;
    std::string warning;
};

struct DiscoverInstalledTaskData {
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
    char text[32];
    const char *formatted =
        infiltratr_format_bytes(bytes, text, sizeof(text));
    return formatted != nullptr ? std::string(formatted) : std::string();
}

const char *category_icon(const std::string_view category) noexcept
{
    if (category == "Productivity") return "accessories-calculator-symbolic";
    if (category == "Automotive") return "applications-engineering-symbolic";
    if (category == "Filesystems") return "drive-harddisk-symbolic";
    if (category == "Development") return "applications-development-symbolic";
    if (category == "Infrastructure") return "network-workgroup-symbolic";
    if (category == "Accessories") return "applications-accessories-symbolic";
    if (category == "Games") return "applications-games-symbolic";
    if (category == "Graphics") return "applications-graphics-symbolic";
    if (category == "Internet") return "applications-internet-symbolic";
    if (category == "Office") return "applications-office-symbolic";
    if (category == "Programming") return "applications-development-symbolic";
    if (category == "Science & Education") return "applications-science-symbolic";
    if (category == "Sound & Video") return "applications-multimedia-symbolic";
    if (category == "System Tools") return "applications-system-symbolic";
    return "application-x-executable-symbolic";
}

GtkWidget *catalogue_icon(const PackageRecord &record, const int size)
{
    GtkWidget *icon = nullptr;
    if (!record.cached_icon_path.empty()) {
        icon = gtk_image_new_from_file(record.cached_icon_path.c_str());
    } else if (!record.icon_name.empty()) {
        icon = gtk_image_new_from_icon_name(record.icon_name.c_str());
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

bool exact_plan_specs(
    const TransactionPlan &plan,
    std::vector<std::string> &specs,
    std::string &error)
{
    specs.clear();
    error.clear();
    if (plan.items.empty()) {
        error = "The resolved transaction is empty.";
        return false;
    }
    specs.reserve(plan.items.size());
    for (const auto &item : plan.items) {
        if (item.package_id.empty()) {
            error =
                "The resolved transaction contains a package without a stable "
                "identity.";
            specs.clear();
            return false;
        }
        if (item.action == TransactionAction::remove) {
            if (item.from_version.empty()) {
                error =
                    "The resolved removal contains a package without its exact "
                    "installed version.";
                specs.clear();
                return false;
            }
            specs.emplace_back(
                "remove:" + item.package_id + "=" + item.from_version);
            continue;
        }
        if (item.to_version.empty()) {
            error =
                "The resolved transaction contains a package without an exact "
                "target version.";
            specs.clear();
            return false;
        }
        specs.emplace_back(item.package_id + "=" + item.to_version);
    }
    return true;
}

std::string transaction_item_text(
    const infiltrator::software::TransactionItem &item)
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

GtkWidget *make_transaction_confirmation_dialog(
    GtkWindow *parent,
    const char *title,
    const std::string &heading,
    const char *accept_label,
    const TransactionPlan &plan,
    const bool from_engine)
{
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
    gtk_box_set_spacing(GTK_BOX(content), 12);
    gtk_widget_set_margin_start(content, 18);
    gtk_widget_set_margin_end(content, 18);
    gtk_widget_set_margin_top(content, 16);
    gtk_widget_set_margin_bottom(content, 16);

    GtkWidget *heading_label = make_label(heading.c_str(), "hero-title");
    gtk_label_set_wrap(GTK_LABEL(heading_label), true);
    gtk_box_append(GTK_BOX(content), heading_label);

    std::ostringstream summary;
    summary << plan.items.size()
            << (plan.items.size() == 1U
                    ? " package change."
                    : " package changes.");
    if (plan.download_bytes > 0U) {
        summary << "  Download: " << display_size(plan.download_bytes) << ".";
    } else if (!from_engine) {
        summary << "  Download size: not reported by the compatibility planner.";
    }
    if (plan.disk_delta_bytes != 0) {
        summary << "  Disk change: "
                << display_disk_delta(plan.disk_delta_bytes) << ".";
    } else if (!from_engine) {
        summary << "  Disk change: not reported by the compatibility planner.";
    }
    if (plan.touches_system) {
        summary << "\nThis transaction includes system-critical components.";
    }
    if (from_engine) {
        summary << "\nResolved by the native package engine against state "
                << "generation " << plan.state_generation << ".";
    } else {
        summary << "\nResolved by the transitional APT compatibility planner.";
    }

    GtkWidget *summary_label =
        make_label(summary.str().c_str(), "detail-note");
    gtk_label_set_wrap(GTK_LABEL(summary_label), true);
    gtk_label_set_selectable(GTK_LABEL(summary_label), true);
    gtk_box_append(GTK_BOX(content), summary_label);

    GtkWidget *changes =
        make_label("Complete resolved change set", "card-title");
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
            make_label(transaction_item_text(item).c_str(), "card-copy");
        gtk_label_set_wrap(GTK_LABEL(label), true);
        gtk_label_set_selectable(GTK_LABEL(label), true);
        gtk_box_append(GTK_BOX(row), label);
        gtk_box_append(GTK_BOX(list), row);
    }
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), list);
    gtk_box_append(GTK_BOX(content), scroller);
    return dialog;
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
        detail_row("Source", record->source));
    gtk_box_append(
        GTK_BOX(card),
        detail_row("Download", display_size(record->download_size_bytes)));
    gtk_box_append(
        GTK_BOX(card),
        detail_row("SHA-256", record->package_sha256));
    gtk_box_append(GTK_BOX(outer), card);

    GtkWidget *status = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    const bool package_installed =
        record->state == infiltrator::software::InstallState::installed;
    const bool package_upgradable =
        record->state == infiltrator::software::InstallState::upgradable;
    GtkWidget *badge = make_label(
        package_installed
            ? "Installed"
            : (package_upgradable ? "Update available" : "Available"),
        package_installed
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
        package_installed
            ? "Removal is checked against installed reverse dependencies and "
              "the complete change set is shown before authorization."
            : "The complete dependency and system change set will be resolved "
              "and shown before administrator authorization is requested.",
        "detail-note");
    gtk_label_set_wrap(GTK_LABEL(note), true);
    gtk_box_append(GTK_BOX(outer), note);

    GtkWidget *actions =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *install = gtk_button_new_with_label(
        package_installed
            ? "Remove"
            : (package_upgradable ? "Update" : "Install"));
    gtk_widget_add_css_class(
        install,
        package_installed ? "destructive-action" : "suggested-action");
    g_object_set_data_full(
        G_OBJECT(install),
        "discover-install-record",
        new PackageRecord(*record),
        package_record_destroy);
    g_object_set_data_full(
        G_OBJECT(install),
        "discover-install-status",
        g_object_ref(note),
        g_object_unref);
    g_signal_connect(
        install, "clicked",
        G_CALLBACK(discover_install_clicked), state);
    gtk_box_append(GTK_BOX(actions), install);
    gtk_box_append(GTK_BOX(outer), actions);

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
    std::string meta_text = record.category;
    if (!record.available_version.empty()) {
        meta_text += "  •  " + record.available_version;
    }
    if (!record.source.empty()) {
        meta_text += "  •  " + record.source;
    }
    GtkWidget *meta = make_label(
        meta_text.c_str(), "discover-meta");
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
    if (state == nullptr || state->discover_visible == nullptr) {
        return;
    }

    const char *search_text =
        state->discover_search == nullptr
            ? ""
            : gtk_editable_get_text(
                  GTK_EDITABLE(state->discover_search));
    const std::string query =
        folded(search_text == nullptr ? "" : search_text);
    const std::string category = selected_category(state);

    std::vector<std::string> indices;
    indices.reserve(state->discover_records.size());

    const bool cached_search_text =
        state->discover_search_texts.size() ==
        state->discover_records.size();

    for (std::size_t index = 0U;
         index < state->discover_records.size();
         ++index) {
        const PackageRecord &record =
            state->discover_records[index];

        if (category != "All" && record.category != category) {
            continue;
        }

        if (!query.empty()) {
            const std::string haystack =
                cached_search_text
                    ? state->discover_search_texts[index]
                    : folded(
                          record.name + "\n" + record.description + "\n" +
                          record.category + "\n" + record.package_name + "\n" +
                          record.source);
            if (haystack.find(query) == std::string::npos) {
                continue;
            }
        }

        indices.emplace_back(std::to_string(index));
    }

    std::vector<const char *> additions;
    additions.reserve(indices.size() + 1U);
    for (const std::string &index : indices) {
        additions.push_back(index.c_str());
    }
    additions.push_back(nullptr);

    gtk_string_list_splice(
        state->discover_visible,
        0U,
        g_list_model_get_n_items(
            G_LIST_MODEL(state->discover_visible)),
        additions.data());

    if (state->discover_status != nullptr) {
        std::ostringstream status;
        status << indices.size() << " of "
               << state->discover_records.size()
               << " applications shown.";
        gtk_label_set_text(
            GTK_LABEL(state->discover_status), status.str().c_str());
    }
}

void discover_grid_setup(
    GtkSignalListItemFactory *,
    GtkListItem *item,
    gpointer)
{
    GtkWidget *holder =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(holder, true);
    gtk_widget_set_margin_top(holder, 6);
    gtk_widget_set_margin_bottom(holder, 6);
    gtk_widget_set_margin_start(holder, 6);
    gtk_widget_set_margin_end(holder, 6);
    gtk_list_item_set_child(item, holder);
}

void discover_grid_bind(
    GtkSignalListItemFactory *,
    GtkListItem *item,
    gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    GtkWidget *holder = gtk_list_item_get_child(item);
    GObject *object = G_OBJECT(gtk_list_item_get_item(item));
    if (state == nullptr || holder == nullptr ||
        object == nullptr || !GTK_IS_STRING_OBJECT(object)) {
        return;
    }

    GtkWidget *child = gtk_widget_get_first_child(holder);
    while (child != nullptr) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_box_remove(GTK_BOX(holder), child);
        child = next;
    }

    const char *text =
        gtk_string_object_get_string(GTK_STRING_OBJECT(object));
    if (text == nullptr || *text == '\0') {
        return;
    }

    gchar *end = nullptr;
    const guint64 index =
        g_ascii_strtoull(text, &end, 10);
    if (end == text || end == nullptr || *end != '\0' ||
        index >= state->discover_records.size()) {
        return;
    }

    gtk_box_append(
        GTK_BOX(holder),
        make_discover_card(
            state,
            state->discover_records[
                static_cast<std::size_t>(index)]));
}

void discover_grid_unbind(
    GtkSignalListItemFactory *,
    GtkListItem *item,
    gpointer)
{
    GtkWidget *holder = gtk_list_item_get_child(item);
    if (holder == nullptr) {
        return;
    }

    GtkWidget *child = gtk_widget_get_first_child(holder);
    while (child != nullptr) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_box_remove(GTK_BOX(holder), child);
        child = next;
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

/*
 * Read installed state from the shared package engine first. The compatibility
 * fallback is deliberately limited to AptBackend::list_installed(), which is
 * already an in-process /var/lib/dpkg/status parser and therefore does not
 * spawn an APT process.
 */
std::vector<PackageRecord> read_installed_packages(
    std::string &error,
    bool *from_engine = nullptr)
{
    if (from_engine != nullptr) {
        *from_engine = false;
    }

    EngineClient engine;
    std::vector<PackageRecord> packages;
    std::string engine_error;
    if (engine.list_installed(packages, engine_error)) {
        for (PackageRecord &package : packages) {
            infiltrator::software::classify_package_role(package);
        }
        if (from_engine != nullptr) {
            *from_engine = true;
        }
        error.clear();
        return packages;
    }

    AptBackend fallback;
    std::string fallback_error;
    packages = fallback.list_installed(fallback_error);
    if (fallback_error.empty()) {
        for (PackageRecord &package : packages) {
            infiltrator::software::classify_package_role(package);
        }
        error.clear();
        return packages;
    }

    error = fallback_error;
    if (!engine_error.empty()) {
        error =
            "Shared engine unavailable: " + engine_error +
            " Direct Debian-state fallback failed: " + fallback_error;
    }
    return {};
}

void discover_worker(
    GTask *task,
    gpointer,
    gpointer task_data,
    GCancellable *)
{
    auto *data =
        static_cast<DiscoverTaskData *>(task_data);
    const bool force_refresh =
        data != nullptr &&
        data->force_refresh;

    auto *result =
        new DiscoverResult{};
    CatalogueSnapshotStore store;

    if (!force_refresh) {
        std::string cache_error;
        if (store.load(
                result->snapshot,
                cache_error)) {
            /*
             * Startup must never wait for package-engine activation.  The
             * saved catalogue is already sufficient to paint Discover, so
             * publish it immediately and reconcile installed state in a
             * separate background task after the page is visible.
             */
            g_task_return_pointer(
                task,
                result,
                [](gpointer pointer) {
                    delete static_cast<DiscoverResult *>(
                        pointer);
                });
            return;
        }
    }

    CatalogueSnapshot previous;
    std::string previous_error;
    const bool had_previous =
        store.load(
            previous,
            previous_error);

    RepositoryCatalogue catalogue;
    std::string infiltrator_warning;
    result->snapshot =
        force_refresh
            ? catalogue.refresh(
                  infiltrator_warning)
            : catalogue.load(
                  infiltrator_warning);

    SystemCatalogue system_catalogue;
    std::string system_warning;
    CatalogueSnapshot system_snapshot =
        system_catalogue.refresh(
            system_warning);

    std::unordered_set<std::string>
        native_packages;
    native_packages.reserve(
        result->snapshot.records.size());
    for (const PackageRecord &record :
         result->snapshot.records) {
        if (!record.package_name.empty()) {
            native_packages.insert(
                package_key(
                    record.package_name));
        }
    }

    for (PackageRecord &record :
         system_snapshot.records) {
        const bool native_duplicate =
            record.id.rfind("apt:", 0U) == 0U &&
            native_packages.find(
                package_key(
                    record.package_name)) !=
                native_packages.end();
        if (!native_duplicate) {
            result->snapshot.records.emplace_back(
                std::move(record));
        }
    }

    std::sort(
        result->snapshot.records.begin(),
        result->snapshot.records.end(),
        [](const PackageRecord &left,
           const PackageRecord &right) {
            if (left.name != right.name) {
                return left.name < right.name;
            }
            return left.source < right.source;
        });

    result->snapshot.source =
        "Infiltrator + system";

    std::unordered_map<std::string, PackageRecord>
        previous_records;
    previous_records.reserve(
        previous.records.size());
    for (const PackageRecord &record :
         previous.records) {
        previous_records.emplace(
            record.id,
            record);
    }

    std::unordered_set<std::string>
        current_ids;
    current_ids.reserve(
        result->snapshot.records.size());

    for (PackageRecord &record :
         result->snapshot.records) {
        current_ids.insert(record.id);

        const auto old =
            previous_records.find(record.id);
        if (old ==
            previous_records.end()) {
            if (had_previous) {
                ++result->added;
            }
        } else if (
            record.icon_sha256 ==
                old->second.icon_sha256 &&
            !old->second.cached_icon_path.empty()) {
            record.cached_icon_path =
                old->second.cached_icon_path;
        }

    }

    if (had_previous) {
        for (const PackageRecord &record :
             previous.records) {
            if (current_ids.find(record.id) ==
                current_ids.end()) {
                ++result->removed;
            }
        }
    }

    std::string cache_error;
    CatalogueSnapshot to_save =
        result->snapshot;
    to_save.from_cache = false;
    if (!store.save(
            to_save,
            cache_error) &&
        result->warning.empty()) {
        result->warning =
            "Catalogue loaded but could not save local state: " +
            cache_error;
    }

    if (!infiltrator_warning.empty()) {
        if (!result->warning.empty()) {
            result->warning += " ";
        }
        result->warning +=
            infiltrator_warning;
    }
    if (!system_warning.empty()) {
        if (!result->warning.empty()) {
            result->warning += " ";
        }
        result->warning +=
            "System catalogue: " +
            system_warning;
    }
    g_task_return_pointer(
        task,
        result,
        [](gpointer pointer) {
            delete static_cast<DiscoverResult *>(
                pointer);
        });
}

void discover_installed_worker(
    GTask *task,
    gpointer,
    gpointer,
    GCancellable *)
{
    auto *result = new DiscoverInstalledResult{};
    result->installed =
        read_installed_packages(result->warning);

    g_task_return_pointer(
        task,
        result,
        [](gpointer value) {
            delete static_cast<DiscoverInstalledResult *>(value);
        });
}

void discover_installed_complete(
    GObject *source_object,
    GAsyncResult *async_result,
    gpointer)
{
    auto *window = GTK_WINDOW(source_object);
    auto *state = static_cast<WindowState *>(
        g_object_get_data(
            G_OBJECT(window), "infiltrator-window-state"));
    auto *task_data =
        static_cast<DiscoverInstalledTaskData *>(
            g_task_get_task_data(G_TASK(async_result)));

    GError *error = nullptr;
    auto *result = static_cast<DiscoverInstalledResult *>(
        g_task_propagate_pointer(
            G_TASK(async_result), &error));
    if (error != nullptr) {
        g_clear_error(&error);
        delete result;
        return;
    }
    if (state == nullptr || result == nullptr ||
        task_data == nullptr ||
        task_data->generation != state->discover_generation) {
        delete result;
        return;
    }

    std::unordered_map<std::string, std::string> versions;
    versions.reserve(result->installed.size());
    for (const PackageRecord &package : result->installed) {
        versions.emplace(
            package_key(package.package_name),
            package.installed_version);
    }

    for (PackageRecord &record : state->discover_records) {
        if (record.id.rfind("flatpak:", 0U) == 0U) {
            continue;
        }

        record.state =
            infiltrator::software::InstallState::not_installed;
        record.installed_version.clear();

        const auto found =
            versions.find(package_key(record.package_name));
        if (found != versions.end()) {
            record.state =
                infiltrator::software::InstallState::installed;
            record.installed_version = found->second;
        }
    }

    rebuild_discover(state);

    if (!result->warning.empty() &&
        state->discover_status != nullptr) {
        const std::string message =
            "Applications loaded; installed-state detection is unavailable: " +
            result->warning;
        gtk_label_set_text(
            GTK_LABEL(state->discover_status),
            message.c_str());
    }

    delete result;
}

void start_discover_installed_hydration(
    WindowState *state,
    const unsigned int generation)
{
    if (state == nullptr || state->window == nullptr ||
        state->discover_records.empty()) {
        return;
    }

    GTask *task = g_task_new(
        G_OBJECT(state->window),
        nullptr,
        discover_installed_complete,
        nullptr);
    auto *task_data = new DiscoverInstalledTaskData{};
    task_data->generation = generation;
    g_task_set_task_data(
        task,
        task_data,
        [](gpointer value) {
            delete static_cast<DiscoverInstalledTaskData *>(value);
        });
    g_task_run_in_thread(task, discover_installed_worker);
    g_object_unref(task);
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

    std::unordered_map<std::string, std::string> icons;
    icons.reserve(result->records.size());
    for (const PackageRecord &record : result->records) {
        if (!record.cached_icon_path.empty()) {
            icons.emplace(record.id, record.cached_icon_path);
        }
    }
    for (PackageRecord &record : state->discover_records) {
        const auto found = icons.find(record.id);
        if (found != icons.end()) {
            record.cached_icon_path = found->second;
        }
    }
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
        if (!record.icon_url.empty() &&
            record.cached_icon_path.empty()) {
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
    state->discover_search_texts.clear();
    state->discover_search_texts.reserve(
        state->discover_records.size());
    for (const PackageRecord &record : state->discover_records) {
        state->discover_search_texts.emplace_back(
            folded(
                record.name + "\n" + record.description + "\n" +
                record.category + "\n" + record.package_name + "\n" +
                record.source));
    }

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
            "Infiltrator + system");
    }
    if (state->discover_state != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->discover_state),
            result->snapshot.from_cache ? "Cached" : "Ready");
    }
    if (state->discover_status != nullptr) {
        if (!result->warning.empty()) {
            gtk_label_set_text(
                GTK_LABEL(state->discover_status),
                result->warning.c_str());
        } else if (task_data->force_refresh) {
            const std::string message =
                "Catalogue synchronized: " +
                std::to_string(result->added) +
                " added, " +
                std::to_string(result->removed) +
                " removed.";
            gtk_label_set_text(
                GTK_LABEL(state->discover_status),
                message.c_str());
        } else {
            gtk_label_set_text(
                GTK_LABEL(state->discover_status),
                result->snapshot.from_cache
                    ? "Loaded saved software catalogue."
                    : "Software catalogue initialized.");
        }
    }

    rebuild_discover(state);

    /*
     * Installed-state and icon enrichment are deliberately second-phase.
     * Neither is allowed to delay the first usable Discover paint.
     */
    start_discover_installed_hydration(
        state, task_data->generation);
    start_discover_icon_hydration(
        state, task_data->generation);
    delete result;
}

void refresh_discover(
    WindowState *state,
    const bool force_refresh = false)
{
    if (state == nullptr || state->window == nullptr ||
        state->discover_state == nullptr) {
        return;
    }

    state->discover_loaded = true;
    ++state->discover_generation;
    gtk_label_set_text(
        GTK_LABEL(state->discover_state), "Loading");
    if (state->discover_status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->discover_status),
            force_refresh
                ? "Synchronizing software catalogue…"
                : "Loading saved software catalogue…");
    }

    GTask *task = g_task_new(
        G_OBJECT(state->window),
        nullptr,
        discover_complete,
        nullptr);
    auto *task_data = new DiscoverTaskData{};
    task_data->generation = state->discover_generation;
    task_data->force_refresh = force_refresh;
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
            "Browse Infiltrator, system repository and Flatpak applications."));

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
        "Refreshing Infiltrator, system and Flatpak metadata…",
        "discover-status");
    gtk_box_append(GTK_BOX(page), state->discover_status);

    state->discover_visible = gtk_string_list_new(nullptr);

    GtkListItemFactory *factory =
        gtk_signal_list_item_factory_new();
    g_signal_connect(
        factory, "setup",
        G_CALLBACK(discover_grid_setup), state);
    g_signal_connect(
        factory, "bind",
        G_CALLBACK(discover_grid_bind), state);
    g_signal_connect(
        factory, "unbind",
        G_CALLBACK(discover_grid_unbind), state);

    GtkSelectionModel *selection =
        GTK_SELECTION_MODEL(
            gtk_no_selection_new(
                G_LIST_MODEL(state->discover_visible)));
    GtkWidget *grid =
        gtk_grid_view_new(selection, factory);
    gtk_grid_view_set_min_columns(
        GTK_GRID_VIEW(grid), 1U);
    gtk_grid_view_set_max_columns(
        GTK_GRID_VIEW(grid), 3U);
    gtk_widget_set_vexpand(grid, true);
    gtk_widget_set_valign(grid, GTK_ALIGN_START);
    gtk_widget_add_css_class(grid, "discover-grid");

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, true);
    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER,
        GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(
        GTK_SCROLLED_WINDOW(scroll),
        grid);
    gtk_box_append(GTK_BOX(page), scroll);

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

struct InstalledResult {
    unsigned int generation{0U};
    std::vector<PackageRecord> records;
    std::string error;
    bool from_engine{false};
};

struct InstalledTaskData {
    unsigned int generation{0U};
};

void installed_worker(
    GTask *task,
    gpointer,
    gpointer task_data,
    GCancellable *)
{
    auto *data = static_cast<InstalledTaskData *>(task_data);
    auto *result = new InstalledResult{};
    result->generation = data == nullptr ? 0U : data->generation;
    result->records =
        read_installed_packages(
            result->error,
            &result->from_engine);

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
    gpointer)
{
    auto *window = GTK_WINDOW(source_object);
    auto *state = static_cast<WindowState *>(
        g_object_get_data(
            G_OBJECT(window),
            "infiltrator-window-state"));
    auto *result = static_cast<InstalledResult *>(
        g_task_propagate_pointer(
            G_TASK(async_result), nullptr));

    if (state == nullptr || result == nullptr) {
        delete result;
        return;
    }
    if (result->generation != state->installed_generation) {
        delete result;
        return;
    }

    state->installed_busy = false;

    std::vector<std::string> installed_rows;
    installed_rows.reserve(result->records.size());
    for (const PackageRecord &package : result->records) {
        installed_rows.emplace_back(
            package.name + "    " + package.installed_version);
    }

    std::vector<const char *> installed_additions;
    installed_additions.reserve(installed_rows.size() + 1U);
    for (const std::string &row : installed_rows) {
        installed_additions.push_back(row.c_str());
    }
    installed_additions.push_back(nullptr);

    gtk_string_list_splice(
        state->installed_strings,
        0U,
        g_list_model_get_n_items(
            G_LIST_MODEL(state->installed_strings)),
        installed_additions.data());

    if (state->installed_status != nullptr) {
        std::ostringstream message;
        if (!result->error.empty()) {
            message
                << "Installed inventory unavailable: "
                << result->error;
        } else {
            message
                << result->records.size()
                << " installed packages read "
                << (result->from_engine
                        ? "from the shared native package engine."
                        : "directly from Debian package state while the shared engine state is unavailable.");
        }
        gtk_label_set_text(
            GTK_LABEL(state->installed_status),
            message.str().c_str());
    }
    if (state->installed_count != nullptr) {
        const std::string count =
            std::to_string(result->records.size());
        gtk_label_set_text(
            GTK_LABEL(state->installed_count), count.c_str());
    }
    if (state->installed_backend != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->installed_backend),
            result->from_engine
                ? "Native engine"
                : "Debian state");
    }
    if (state->backend_state != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->backend_state),
            result->error.empty()
                ? "Ready"
                : "Unavailable");
    }

    delete result;
}

void refresh_installed(WindowState *state)
{
    if (state == nullptr ||
        state->installed_strings == nullptr ||
        state->window == nullptr ||
        state->installed_busy) {
        return;
    }

    state->installed_loaded = true;
    state->installed_busy = true;
    ++state->installed_generation;

    if (state->installed_status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->installed_status),
            "Loading installed packages from shared state…");
    }
    if (state->backend_state != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->backend_state), "Loading");
    }

    auto *data = new InstalledTaskData{
        state->installed_generation};
    GTask *task = g_task_new(
        G_OBJECT(state->window),
        nullptr,
        installed_complete,
        nullptr);
    g_task_set_task_data(
        task,
        data,
        [](gpointer pointer) {
            delete static_cast<InstalledTaskData *>(pointer);
        });
    g_task_run_in_thread(task, installed_worker);
    g_object_unref(task);
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
        make_stat_card(
            "BACKEND", "Loading", "stat-operation",
            &state->installed_backend),
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

    return page;
}



struct SystemResult {
    unsigned int generation{0U};
    std::vector<PackageRecord> components;
    std::vector<PackageRecord> updates;
    std::string error;
    std::string update_warning;
    bool from_engine{false};
};

struct SystemTaskData {
    unsigned int generation{0U};
    bool refresh_metadata{false};
};

std::string system_identity(const PackageRecord &package)
{
    return package.package_name.empty()
        ? package.id
        : package.package_name;
}

const char *system_icon_name(const PackageRecord &package)
{
    switch (package.kind) {
    case infiltrator::software::PackageKind::kernel:
        return "computer-symbolic";
    case infiltrator::software::PackageKind::driver:
        return "preferences-system-symbolic";
    case infiltrator::software::PackageKind::system:
        return "applications-system-symbolic";
    default:
        return "application-x-executable-symbolic";
    }
}

GtkWidget *make_system_row(
    const PackageRecord &package,
    const PackageRecord *update)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(row, "package-row");
    gtk_widget_set_margin_top(row, 6);
    gtk_widget_set_margin_bottom(row, 6);
    gtk_widget_set_margin_start(row, 8);
    gtk_widget_set_margin_end(row, 8);

    GtkWidget *icon =
        make_icon(system_icon_name(package), 24);
    gtk_widget_add_css_class(icon, "package-icon");
    gtk_box_append(GTK_BOX(row), icon);

    GtkWidget *identity =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(identity, true);

    GtkWidget *name =
        make_label(package.name.c_str(), "card-title");
    gtk_label_set_ellipsize(
        GTK_LABEL(name), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(identity), name);

    std::string version = package.installed_version;
    if (update != nullptr &&
        !update->available_version.empty()) {
        version += "  →  " + update->available_version;
    }
    GtkWidget *version_label =
        make_label(version.c_str(), "card-copy");
    gtk_label_set_ellipsize(
        GTK_LABEL(version_label), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(identity), version_label);

    std::string meta =
        std::string(
            infiltrator::software::package_kind_name(
                package.kind));
    if (!package.architecture.empty()) {
        meta += "  •  " + package.architecture;
    }
    if (update != nullptr) {
        std::string source_name = update->repository_origin;
        if (source_name.empty()) {
            source_name = update->repository_site;
        }
        if (source_name.empty()) {
            source_name = update->source;
        }
        if (!source_name.empty()) {
            meta += "  •  Source: " + source_name;
        }
    }
    GtkWidget *meta_label =
        make_label(meta.c_str(), "discover-meta");
    gtk_label_set_ellipsize(
        GTK_LABEL(meta_label), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(identity), meta_label);

    gtk_box_append(GTK_BOX(row), identity);

    const char *status_text = "Current";
    const char *status_class = "state-success";
    if (update != nullptr) {
        status_text = update->system_critical
            ? "Recommended • System-critical"
            : "Recommended update";
        status_class = update->system_critical
            ? "state-warning"
            : "state-available";
    } else if (package.system_critical) {
        status_text = "Core system";
        status_class = "state-info";
    }

    GtkWidget *status =
        make_label(status_text, status_class);
    gtk_widget_set_valign(status, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(row), status);

    return row;
}

void rebuild_system(WindowState *state)
{
    if (state == nullptr || state->system_list == nullptr) {
        return;
    }

    GtkWidget *child =
        gtk_widget_get_first_child(
            GTK_WIDGET(state->system_list));
    while (child != nullptr) {
        GtkWidget *next =
            gtk_widget_get_next_sibling(child);
        gtk_list_box_remove(state->system_list, child);
        child = next;
    }

    std::unordered_map<std::string, const PackageRecord *> updates;
    updates.reserve(state->system_update_records.size());
    std::size_t critical_count = 0U;
    for (const PackageRecord &update :
         state->system_update_records) {
        updates[system_identity(update)] = &update;
        if (update.system_critical) {
            ++critical_count;
        }
    }

    for (const PackageRecord &package :
         state->system_records) {
        const auto found =
            updates.find(system_identity(package));
        const PackageRecord *update =
            found == updates.end()
                ? nullptr
                : found->second;

        GtkWidget *row = gtk_list_box_row_new();
        gtk_list_box_row_set_child(
            GTK_LIST_BOX_ROW(row),
            make_system_row(package, update));
        gtk_list_box_append(state->system_list, row);
    }

    if (state->system_count != nullptr) {
        const std::string count =
            std::to_string(state->system_records.size());
        gtk_label_set_text(
            GTK_LABEL(state->system_count), count.c_str());
    }
    if (state->system_updates != nullptr) {
        const std::string count =
            std::to_string(
                state->system_update_records.size());
        gtk_label_set_text(
            GTK_LABEL(state->system_updates), count.c_str());
    }
    if (state->system_critical != nullptr) {
        const std::string count =
            std::to_string(critical_count);
        gtk_label_set_text(
            GTK_LABEL(state->system_critical), count.c_str());
    }
    if (state->system_review_updates != nullptr) {
        gtk_widget_set_sensitive(
            state->system_review_updates,
            !state->system_update_records.empty());
    }
}

void system_worker(
    GTask *task,
    gpointer,
    gpointer task_data,
    GCancellable *)
{
    auto *data =
        static_cast<SystemTaskData *>(task_data);
    auto *result = new SystemResult{};
    result->generation =
        data == nullptr ? 0U : data->generation;

    std::vector<PackageRecord> installed =
        read_installed_packages(
            result->error,
            &result->from_engine);
    if (result->error.empty()) {
        for (PackageRecord &package : installed) {
            infiltrator::software::classify_package_role(
                package);
            if (infiltrator::software::is_system_component(
                    package)) {
                result->components.emplace_back(
                    std::move(package));
            }
        }
    }

    if (data != nullptr) {
        EngineClient engine;
        if (data->refresh_metadata) {
            if (!engine.refresh(result->update_warning)) {
                result->update_warning =
                    "Update refresh failed: " +
                    result->update_warning;
            }
        }

        std::vector<PackageRecord> updates;
        std::string update_error;
        if (result->update_warning.empty() &&
            !engine.list_updates(updates, update_error) &&
            !data->refresh_metadata) {
            std::string refresh_error;
            if (engine.refresh(refresh_error)) {
                update_error.clear();
                (void)engine.list_updates(
                    updates, update_error);
            } else {
                update_error =
                    "Native update state unavailable: " +
                    refresh_error;
            }
        }
        if (!update_error.empty()) {
            result->update_warning = update_error;
        }

        for (PackageRecord &package : updates) {
            infiltrator::software::classify_package_role(
                package);
            if (infiltrator::software::is_system_component(
                    package)) {
                result->updates.emplace_back(
                    std::move(package));
            }
        }
    }

    auto rank = [](const PackageRecord &package) {
        switch (package.kind) {
        case infiltrator::software::PackageKind::kernel:
            return 0;
        case infiltrator::software::PackageKind::driver:
            return 1;
        case infiltrator::software::PackageKind::system:
            return 2;
        default:
            return 3;
        }
    };
    std::stable_sort(
        result->components.begin(),
        result->components.end(),
        [&](const PackageRecord &left,
            const PackageRecord &right) {
            const int left_rank = rank(left);
            const int right_rank = rank(right);
            if (left_rank != right_rank) {
                return left_rank < right_rank;
            }
            return left.name < right.name;
        });

    g_task_return_pointer(
        task,
        result,
        [](gpointer pointer) {
            delete static_cast<SystemResult *>(pointer);
        });
}

void system_complete(
    GObject *source_object,
    GAsyncResult *async_result,
    gpointer)
{
    auto *window = GTK_WINDOW(source_object);
    auto *state = static_cast<WindowState *>(
        g_object_get_data(
            G_OBJECT(window),
            "infiltrator-window-state"));
    auto *result = static_cast<SystemResult *>(
        g_task_propagate_pointer(
            G_TASK(async_result), nullptr));

    if (state == nullptr || result == nullptr) {
        delete result;
        return;
    }
    if (result->generation != state->system_generation) {
        delete result;
        return;
    }

    state->system_busy = false;
    state->system_records =
        std::move(result->components);
    state->system_update_records =
        std::move(result->updates);

    const std::string error = result->error;
    const std::string warning = result->update_warning;
    const bool from_engine = result->from_engine;
    delete result;

    rebuild_system(state);

    if (state->system_status != nullptr) {
        std::ostringstream message;
        if (!error.empty()) {
            message
                << "System inventory unavailable: "
                << error;
        } else {
            message
                << state->system_records.size()
                << " kernel, driver and core system components read "
                << (from_engine
                        ? "from the shared native package engine."
                        : "from direct Debian package state.");
            if (!warning.empty()) {
                message
                    << " Update status: "
                    << warning;
            } else if (state->system_update_records.empty()) {
                message
                    << " No preferred system updates are currently available.";
            } else {
                message
                    << " "
                    << state->system_update_records.size()
                    << " preferred system update"
                    << (state->system_update_records.size() == 1U
                            ? " is"
                            : "s are")
                    << " available.";
            }
        }
        gtk_label_set_text(
            GTK_LABEL(state->system_status),
            message.str().c_str());
    }

    if (state->system_refresh != nullptr) {
        gtk_widget_set_sensitive(
            state->system_refresh, true);
    }
}

void refresh_system(
    WindowState *state,
    const bool refresh_metadata)
{
    if (state == nullptr ||
        state->window == nullptr ||
        state->system_list == nullptr ||
        state->system_busy) {
        return;
    }

    state->system_loaded = true;
    state->system_busy = true;
    ++state->system_generation;

    if (state->system_status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->system_status),
            refresh_metadata
                ? "Refreshing repository state and system components…"
                : "Reading system components from shared state…");
    }
    if (state->system_refresh != nullptr) {
        gtk_widget_set_sensitive(
            state->system_refresh, false);
    }

    auto *data = new SystemTaskData{
        state->system_generation,
        refresh_metadata};
    GTask *task = g_task_new(
        G_OBJECT(state->window),
        nullptr,
        system_complete,
        nullptr);
    g_task_set_task_data(
        task,
        data,
        [](gpointer pointer) {
            delete static_cast<SystemTaskData *>(pointer);
        });
    g_task_run_in_thread(task, system_worker);
    g_object_unref(task);
}

void system_refresh_clicked(
    GtkButton *,
    gpointer user_data)
{
    refresh_system(
        static_cast<WindowState *>(user_data),
        true);
}

void system_review_updates_clicked(
    GtkButton *,
    gpointer user_data)
{
    auto *state =
        static_cast<WindowState *>(user_data);
    if (state == nullptr ||
        state->navigation_list == nullptr) {
        return;
    }

    GtkListBoxRow *updates =
        gtk_list_box_get_row_at_index(
            state->navigation_list, 2);
    if (updates != nullptr) {
        gtk_list_box_select_row(
            state->navigation_list,
            updates);
    }
}

GtkWidget *make_system_page(WindowState *state)
{
    GtkWidget *page =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(page, "content");
    gtk_widget_add_css_class(page, "page-system");

    gtk_box_append(
        GTK_BOX(page),
        make_page_intro(
            "computer-symbolic",
            "System",
            "Kernels, drivers and core operating-system components."));

    GtkWidget *stats = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(stats), 10);
    gtk_grid_set_column_homogeneous(
        GTK_GRID(stats), true);
    gtk_grid_attach(
        GTK_GRID(stats),
        make_stat_card(
            "COMPONENTS", "0", "stat-info",
            &state->system_count),
        0, 0, 1, 1);
    gtk_grid_attach(
        GTK_GRID(stats),
        make_stat_card(
            "UPDATES", "0", "stat-operation",
            &state->system_updates),
        1, 0, 1, 1);
    gtk_grid_attach(
        GTK_GRID(stats),
        make_stat_card(
            "SYSTEM-CRITICAL", "0", "stat-warning",
            &state->system_critical),
        2, 0, 1, 1);
    gtk_box_append(GTK_BOX(page), stats);

    GtkWidget *controls =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(controls, "card");

    state->system_status =
        make_label(
            "System inventory has not been loaded yet.",
            "card-copy");
    gtk_label_set_wrap(
        GTK_LABEL(state->system_status), true);
    gtk_widget_set_hexpand(
        state->system_status, true);
    gtk_box_append(
        GTK_BOX(controls), state->system_status);

    state->system_refresh =
        gtk_button_new_with_label("Refresh");
    gtk_widget_add_css_class(
        state->system_refresh, "control-button");
    g_signal_connect(
        state->system_refresh,
        "clicked",
        G_CALLBACK(system_refresh_clicked),
        state);
    gtk_box_append(
        GTK_BOX(controls), state->system_refresh);

    state->system_review_updates =
        gtk_button_new_with_label("Review system updates");
    gtk_widget_add_css_class(
        state->system_review_updates, "accent-button");
    gtk_widget_set_sensitive(
        state->system_review_updates, false);
    g_signal_connect(
        state->system_review_updates,
        "clicked",
        G_CALLBACK(system_review_updates_clicked),
        state);
    gtk_box_append(
        GTK_BOX(controls),
        state->system_review_updates);

    gtk_box_append(GTK_BOX(page), controls);

    GtkWidget *card =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(card, "card");
    gtk_widget_add_css_class(card, "card-info");
    gtk_widget_set_vexpand(card, true);

    GtkWidget *heading =
        make_label(
            "Installed system components",
            "card-title");
    gtk_box_append(GTK_BOX(card), heading);

    GtkWidget *list = gtk_list_box_new();
    state->system_list = GTK_LIST_BOX(list);
    gtk_widget_add_css_class(list, "package-list");
    gtk_list_box_set_selection_mode(
        state->system_list,
        GTK_SELECTION_NONE);

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

struct UpdatesResult {
    unsigned int generation{0U};
    bool refreshed_metadata{false};
    bool from_engine{false};
    std::vector<PackageRecord> records;
    std::string error;
};

struct UpdatesTaskData {
    unsigned int generation{0U};
    bool refresh_metadata{false};
};

struct UpdatePlanResult {
    std::optional<infiltrator::software::TransactionPlan> plan;
    std::string error;
    bool from_engine{false};
};

struct UpdatePlanTaskData {
    std::vector<std::string> package_ids;
    bool use_engine{false};
};

struct UpdateProcessRun {
    GtkWindow *window{};
    std::string operation;
    TransactionPlan plan;
};

std::filesystem::path update_runtime_state_path()
{
    const char *runtime = g_get_user_runtime_dir();
    if (runtime == nullptr || *runtime == '\0') {
        return {};
    }
    return std::filesystem::path(runtime) /
           "infiltrator-software" / "update-state";
}

std::filesystem::path transaction_history_path()
{
    const char *data = g_get_user_data_dir();
    if (data == nullptr || *data == '\0') {
        return {};
    }
    return std::filesystem::path(data) /
           "infiltrator-software" / "history.sqlite3";
}

void record_transaction_history(
    const TransactionPlan &plan,
    const bool success,
    const std::string_view message)
{
    const std::filesystem::path path =
        transaction_history_path();
    if (path.empty() || plan.items.empty()) {
        return;
    }

    TransactionHistoryStore store(path.string());
    std::string error;
    if (!store.append(plan, success, message, error)) {
        g_warning(
            "Unable to record Software transaction history: %s",
            error.c_str());
    }
}

std::string one_line(std::string value)
{
    for (char &ch : value) {
        if (ch == '\n' || ch == '\r' || ch == '\t') {
            ch = ' ';
        }
    }
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.pop_back();
    }
    if (value.size() > 220U) {
        value.resize(217U);
        value += "...";
    }
    return value;
}


gboolean update_progress_tick(gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    if (state == nullptr ||
        !state->updates_busy ||
        state->updates_progress == nullptr) {
        if (state != nullptr) {
            state->updates_progress_timer_id = 0U;
        }
        return G_SOURCE_REMOVE;
    }

    gtk_progress_bar_pulse(
        GTK_PROGRESS_BAR(state->updates_progress));

    if (state->updates_status != nullptr &&
        state->updates_progress_started_us > 0) {
        const gint64 elapsed_us =
            g_get_monotonic_time() -
            state->updates_progress_started_us;
        const long long elapsed_seconds =
            static_cast<long long>(
                elapsed_us / G_USEC_PER_SEC);

        const std::string message =
            state->updates_post_install_refresh
                ? "Installation completed; verifying final package state… " +
                    std::to_string(elapsed_seconds) +
                    " s elapsed."
                : "Approved update transaction is active… " +
                    std::to_string(elapsed_seconds) +
                    " s elapsed. Software may be refreshing metadata, "
                    "re-validating the approved exact versions or applying packages.";
        gtk_label_set_text(
            GTK_LABEL(state->updates_status),
            message.c_str());
    }

    return G_SOURCE_CONTINUE;
}

void start_update_progress(WindowState *state)
{
    if (state == nullptr ||
        state->updates_progress == nullptr) {
        return;
    }

    state->updates_progress_started_us =
        g_get_monotonic_time();
    gtk_widget_set_visible(
        state->updates_progress, true);
    gtk_progress_bar_set_pulse_step(
        GTK_PROGRESS_BAR(state->updates_progress), 0.08);
    gtk_progress_bar_pulse(
        GTK_PROGRESS_BAR(state->updates_progress));

    if (state->updates_progress_timer_id == 0U) {
        state->updates_progress_timer_id =
            g_timeout_add_seconds(
                1U, update_progress_tick, state);
    }
}

void stop_update_progress(WindowState *state)
{
    if (state == nullptr) {
        return;
    }

    if (state->updates_progress_timer_id != 0U) {
        g_source_remove(state->updates_progress_timer_id);
        state->updates_progress_timer_id = 0U;
    }
    state->updates_progress_started_us = 0;
    if (state->updates_progress != nullptr) {
        gtk_widget_set_visible(
            state->updates_progress, false);
    }
}

struct DiscoverPlanTaskData {
    std::string package_id;
    TransactionAction action{TransactionAction::install};
    GtkWindow *main_window{};
};

struct DiscoverPlanResult {
    std::optional<TransactionPlan> plan;
    std::string error;
    bool from_engine{false};
};

struct DiscoverInstallOperation {
    GtkWindow *main_window{};
    GtkWidget *button{};
    GtkWidget *status{};
    TransactionAction action{TransactionAction::install};
    TransactionPlan plan;
};

void destroy_discover_plan_task_data(gpointer pointer)
{
    auto *data = static_cast<DiscoverPlanTaskData *>(pointer);
    if (data == nullptr) return;
    if (data->main_window != nullptr) g_object_unref(data->main_window);
    delete data;
}

void destroy_discover_install_operation(
    DiscoverInstallOperation *operation)
{
    if (operation == nullptr) return;
    if (operation->main_window != nullptr) g_object_unref(operation->main_window);
    if (operation->button != nullptr) g_object_unref(operation->button);
    if (operation->status != nullptr) g_object_unref(operation->status);
    delete operation;
}

void discover_plan_worker(
    GTask *task,
    gpointer,
    gpointer task_data,
    GCancellable *)
{
    auto *data = static_cast<DiscoverPlanTaskData *>(task_data);
    auto *result = new DiscoverPlanResult{};

    if (data == nullptr || data->package_id.empty()) {
        result->error = "No package was selected for the transaction.";
    } else {
        infiltrator::software::TransactionRequest request;
        request.action = data->action;
        request.package_ids = {data->package_id};

        std::string engine_error;
        EngineClient engine;
        result->plan = engine.plan(request, engine_error);
        if (result->plan.has_value()) {
            result->from_engine = true;
        } else if (data->action == TransactionAction::remove) {
            result->error =
                "Native removal planner: " + one_line(engine_error);
        } else {
            std::string fallback_error;
            AptBackend fallback;
            result->plan = fallback.plan(request, fallback_error);
            if (!result->plan.has_value()) {
                result->error = "Native planner: " + one_line(engine_error);
                if (!fallback_error.empty()) {
                    result->error +=
                        "  Compatibility planner: " +
                        one_line(fallback_error);
                }
            }
        }
    }

    g_task_return_pointer(
        task,
        result,
        [](gpointer pointer) {
            delete static_cast<DiscoverPlanResult *>(pointer);
        });
}

void discover_install_process_complete(
    GObject *source_object,
    GAsyncResult *async_result,
    gpointer user_data)
{
    auto *operation =
        static_cast<DiscoverInstallOperation *>(user_data);
    auto *process = G_SUBPROCESS(source_object);

    GError *error = nullptr;
    gchar *stdout_text = nullptr;
    gchar *stderr_text = nullptr;
    const gboolean communicated =
        g_subprocess_communicate_utf8_finish(
            process, async_result,
            &stdout_text, &stderr_text, &error);
    const bool success =
        communicated != FALSE &&
        g_subprocess_get_successful(process);
    const bool no_changes_required =
        success &&
        stdout_text != nullptr &&
        std::string_view(stdout_text).find(
            "INFILTRATOR_NO_CHANGES_REQUIRED") !=
            std::string_view::npos;

    if (operation != nullptr && !operation->plan.items.empty()) {
        std::string history_message;
        if (no_changes_required) {
            history_message =
                "Approved package state was already satisfied; no package changes were required.";
        } else if (success) {
            history_message = "Transaction completed successfully.";
        } else if (g_subprocess_get_if_exited(process) &&
                   g_subprocess_get_exit_status(process) == 126) {
            history_message = "Administrator authentication was cancelled.";
        } else if (stderr_text != nullptr && *stderr_text != '\0') {
            history_message = one_line(stderr_text);
        } else if (error != nullptr && error->message != nullptr) {
            history_message = one_line(error->message);
        } else {
            history_message = "Transaction failed.";
        }
        record_transaction_history(
            operation->plan, success, history_message);

        if (operation->main_window != nullptr) {
            auto *history_state = static_cast<WindowState *>(
                g_object_get_data(
                    G_OBJECT(operation->main_window),
                    "infiltrator-window-state"));
            if (history_state != nullptr &&
                history_state->history_loaded) {
                refresh_history(history_state);
            }
        }
    }

    if (operation != nullptr && operation->status != nullptr) {
        if (success) {
            gtk_label_set_text(
                GTK_LABEL(operation->status),
                no_changes_required
                    ? "Package state was already current. Refreshing software state…"
                    : operation->action == TransactionAction::remove
                        ? "Removal complete. Refreshing software state…"
                        : operation->action == TransactionAction::upgrade
                            ? "Update complete. Refreshing software state…"
                            : "Installation complete. Refreshing software state…");
        } else {
            std::string message =
                operation->action == TransactionAction::remove
                    ? "Unable to remove package."
                    : operation->action == TransactionAction::upgrade
                        ? "Unable to update package."
                        : "Unable to install package.";
            if (error != nullptr && error->message != nullptr) {
                message += " ";
                message += error->message;
            } else if (stderr_text != nullptr &&
                       *stderr_text != '\0') {
                message += " ";
                message += one_line(stderr_text);
            }
            gtk_label_set_text(
                GTK_LABEL(operation->status),
                message.c_str());
        }
    }

    if (operation != nullptr && operation->button != nullptr) {
        if (success) {
            gtk_button_set_label(
                GTK_BUTTON(operation->button),
                operation->action == TransactionAction::remove
                    ? "Removed"
                    : operation->action == TransactionAction::upgrade
                        ? "Updated"
                        : "Installed");
            gtk_widget_set_sensitive(operation->button, false);
        } else {
            gtk_widget_set_sensitive(operation->button, true);
        }
    }

    if (success && operation != nullptr &&
        operation->main_window != nullptr) {
        auto *state = static_cast<WindowState *>(
            g_object_get_data(
                G_OBJECT(operation->main_window),
                "infiltrator-window-state"));
        if (state != nullptr) {
            refresh_discover(state, true);
            if (state->installed_loaded) refresh_installed(state);
            if (state->updates_loaded) refresh_updates(state);
        }
    }

    g_free(stdout_text);
    g_free(stderr_text);
    g_clear_error(&error);
    destroy_discover_install_operation(operation);
}

void start_discover_install_operation(
    DiscoverInstallOperation *operation)
{
    if (operation == nullptr) return;

    std::vector<std::string> specs;
    std::string plan_error;
    if (!exact_plan_specs(operation->plan, specs, plan_error)) {
        if (operation->status != nullptr) {
            gtk_label_set_text(
                GTK_LABEL(operation->status),
                plan_error.c_str());
        }
        if (operation->button != nullptr) {
            gtk_widget_set_sensitive(operation->button, true);
        }
        record_transaction_history(
            operation->plan, false, plan_error);
        if (operation->main_window != nullptr) {
            auto *history_state = static_cast<WindowState *>(
                g_object_get_data(
                    G_OBJECT(operation->main_window),
                    "infiltrator-window-state"));
            if (history_state != nullptr &&
                history_state->history_loaded) {
                refresh_history(history_state);
            }
        }
        destroy_discover_install_operation(operation);
        return;
    }

    std::vector<std::string> arguments{
        "pkexec",
        "/usr/libexec/infiltrator-software-update-helper",
        "apply-plan"};
    arguments.reserve(specs.size() + 3U);
    arguments.insert(arguments.end(), specs.begin(), specs.end());

    std::vector<const gchar *> argv;
    argv.reserve(arguments.size() + 1U);
    for (const std::string &argument : arguments) {
        argv.push_back(argument.c_str());
    }
    argv.push_back(nullptr);

    GError *error = nullptr;
    GSubprocess *process = g_subprocess_newv(
        argv.data(),
        static_cast<GSubprocessFlags>(
            G_SUBPROCESS_FLAGS_STDOUT_PIPE |
            G_SUBPROCESS_FLAGS_STDERR_PIPE),
        &error);

    if (process == nullptr) {
        std::string message =
            operation->action == TransactionAction::remove
                ? "Unable to start package removal."
                : operation->action == TransactionAction::upgrade
                    ? "Unable to start package update."
                    : "Unable to start package installation.";
        if (error != nullptr && error->message != nullptr) {
            message += " ";
            message += error->message;
        }
        if (operation->status != nullptr) {
            gtk_label_set_text(
                GTK_LABEL(operation->status),
                message.c_str());
        }
        if (operation->button != nullptr) {
            gtk_widget_set_sensitive(operation->button, true);
        }
        record_transaction_history(
            operation->plan, false, message);
        if (operation->main_window != nullptr) {
            auto *history_state = static_cast<WindowState *>(
                g_object_get_data(
                    G_OBJECT(operation->main_window),
                    "infiltrator-window-state"));
            if (history_state != nullptr &&
                history_state->history_loaded) {
                refresh_history(history_state);
            }
        }
        g_clear_error(&error);
        destroy_discover_install_operation(operation);
        return;
    }

    if (operation->status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(operation->status),
            "Waiting for administrator authorization…");
    }

    g_subprocess_communicate_utf8_async(
        process, nullptr, nullptr,
        discover_install_process_complete, operation);
    g_object_unref(process);
}

void discover_install_confirm_response(
    GtkDialog *dialog,
    const gint response_id,
    gpointer user_data)
{
    auto *operation =
        static_cast<DiscoverInstallOperation *>(user_data);
    gtk_window_destroy(GTK_WINDOW(dialog));

    if (operation == nullptr) return;
    if (response_id == GTK_RESPONSE_ACCEPT) {
        start_discover_install_operation(operation);
        return;
    }

    if (operation->status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(operation->status),
            operation->action == TransactionAction::remove
                ? "Removal cancelled."
                : operation->action == TransactionAction::upgrade
                    ? "Update cancelled."
                    : "Installation cancelled.");
    }
    if (operation->button != nullptr) {
        gtk_widget_set_sensitive(operation->button, true);
    }
    destroy_discover_install_operation(operation);
}

void discover_plan_complete(
    GObject *source_object,
    GAsyncResult *async_result,
    gpointer)
{
    auto *button = GTK_WIDGET(source_object);
    auto *task = G_TASK(async_result);
    auto *task_data = static_cast<DiscoverPlanTaskData *>(
        g_task_get_task_data(task));
    auto *result = static_cast<DiscoverPlanResult *>(
        g_task_propagate_pointer(task, nullptr));
    auto *status = static_cast<GtkWidget *>(
        g_object_get_data(
            G_OBJECT(button), "discover-install-status"));
    auto *record = static_cast<PackageRecord *>(
        g_object_get_data(
            G_OBJECT(button), "discover-install-record"));

    WindowState *state = nullptr;
    if (task_data != nullptr && task_data->main_window != nullptr) {
        state = static_cast<WindowState *>(
            g_object_get_data(
                G_OBJECT(task_data->main_window),
                "infiltrator-window-state"));
    }

    if (result == nullptr || !result->plan.has_value() ||
        state == nullptr || record == nullptr || task_data == nullptr) {
        std::string message = "Unable to resolve software transaction.";
        if (result != nullptr && !result->error.empty()) {
            message += " ";
            message += one_line(result->error);
        }
        if (status != nullptr) {
            gtk_label_set_text(GTK_LABEL(status), message.c_str());
        }
        gtk_widget_set_sensitive(button, true);
        delete result;
        return;
    }

    if (status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(status),
            "Transaction resolved. Review every package change before authorizing.");
    }

    GtkWindow *parent = state->window;
    if (GtkRoot *root = gtk_widget_get_root(button);
        root != nullptr && GTK_IS_WINDOW(root)) {
        parent = GTK_WINDOW(root);
    }

    const TransactionPlan plan = *result->plan;
    const bool from_engine = result->from_engine;
    delete result;

    const std::string heading =
        task_data->action == TransactionAction::remove
            ? "Review the complete removal transaction for " + record->name
            : task_data->action == TransactionAction::upgrade
                ? "Review the complete update transaction for " + record->name
                : "Review the complete installation transaction for " + record->name;

    auto *operation = new DiscoverInstallOperation{};
    operation->main_window =
        GTK_WINDOW(g_object_ref(task_data->main_window));
    operation->button = GTK_WIDGET(g_object_ref(button));
    operation->status =
        status != nullptr ? GTK_WIDGET(g_object_ref(status)) : nullptr;
    operation->action = task_data->action;
    operation->plan = plan;

    GtkWidget *dialog =
        make_transaction_confirmation_dialog(
            parent,
            task_data->action == TransactionAction::remove
                ? "Review removal"
                : task_data->action == TransactionAction::upgrade
                    ? "Review update" : "Review installation",
            heading,
            task_data->action == TransactionAction::remove
                ? "Remove"
                : task_data->action == TransactionAction::upgrade
                    ? "Update" : "Install",
            plan,
            from_engine);
    g_signal_connect(
        dialog, "response",
        G_CALLBACK(discover_install_confirm_response),
        operation);
    gtk_window_present(GTK_WINDOW(dialog));
}

void discover_install_clicked(
    GtkButton *button,
    gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    auto *record = static_cast<PackageRecord *>(
        g_object_get_data(
            G_OBJECT(button), "discover-install-record"));
    auto *status = static_cast<GtkWidget *>(
        g_object_get_data(
            G_OBJECT(button), "discover-install-status"));

    if (state == nullptr || state->window == nullptr ||
        record == nullptr || record->package_name.empty()) {
        return;
    }

    const TransactionAction action =
        record->state == infiltrator::software::InstallState::installed
            ? TransactionAction::remove
            : record->state == infiltrator::software::InstallState::upgradable
                ? TransactionAction::upgrade
                : TransactionAction::install;

    gtk_widget_set_sensitive(GTK_WIDGET(button), false);
    if (status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(status),
            action == TransactionAction::remove
                ? "Checking reverse dependencies and resolving removal…"
                : action == TransactionAction::upgrade
                    ? "Resolving the complete update transaction…"
                    : "Resolving the complete installation transaction…");
    }

    auto *data = new DiscoverPlanTaskData{};
    data->package_id = record->package_name;
    data->action = action;
    data->main_window = GTK_WINDOW(g_object_ref(state->window));

    GTask *task = g_task_new(
        G_OBJECT(button), nullptr,
        discover_plan_complete, nullptr);
    g_task_set_task_data(
        task, data, destroy_discover_plan_task_data);
    g_task_run_in_thread(task, discover_plan_worker);
    g_object_unref(task);
}

void set_update_runtime_state(const std::string_view value)
{
    const std::filesystem::path path = update_runtime_state_path();
    if (path.empty()) {
        return;
    }

    std::error_code ec;
    if (value.empty()) {
        std::filesystem::remove(path, ec);
        return;
    }

    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        return;
    }

    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        return;
    }
    output << value << '\n';
}

const char *update_icon_name(const PackageRecord &package)
{
    switch (package.kind) {
    case infiltrator::software::PackageKind::kernel:
        return "computer-symbolic";
    case infiltrator::software::PackageKind::driver:
        return "preferences-system-symbolic";
    case infiltrator::software::PackageKind::library:
    case infiltrator::software::PackageKind::runtime:
        return "applications-system-symbolic";
    case infiltrator::software::PackageKind::system:
        return "emblem-system-symbolic";
    case infiltrator::software::PackageKind::application:
    case infiltrator::software::PackageKind::unknown:
        /*
         * Mint icon themes may provide software-update-available-symbolic as
         * a pre-coloured raster asset.  Such assets ignore GtkImage's CSS
         * foreground and become nearly black on the Software dark palette.
         * view-refresh-symbolic is a true symbolic icon across the supported
         * GTK/Mint themes, so it reliably follows .package-icon colour.
         */
        return "view-refresh-symbolic";
    }
    return "view-refresh-symbolic";
}

std::string update_identity(const PackageRecord &package)
{
    return package.package_name.empty()
        ? package.id
        : package.package_name;
}

void update_selection_controls(WindowState *state)
{
    if (state == nullptr || state->updates_install == nullptr) {
        return;
    }

    const std::size_t selected = state->selected_update_ids.size();
    if (selected == 0U) {
        gtk_button_set_label(
            GTK_BUTTON(state->updates_install),
            "Install selected updates");
    } else if (selected == state->update_records.size()) {
        gtk_button_set_label(
            GTK_BUTTON(state->updates_install),
            state->update_records.size() == 1U
                ? "Install update"
                : "Install all updates");
    } else {
        const std::string label =
            "Install " + std::to_string(selected) + " selected";
        gtk_button_set_label(
            GTK_BUTTON(state->updates_install), label.c_str());
    }

    gtk_widget_set_sensitive(
        state->updates_install,
        !state->updates_busy && selected != 0U);
}

void update_selection_toggled(GtkCheckButton *button, gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    const char *identity = static_cast<const char *>(
        g_object_get_data(G_OBJECT(button), "update-identity"));
    if (state == nullptr || identity == nullptr || *identity == '\0') {
        return;
    }

    if (gtk_check_button_get_active(button)) {
        state->selected_update_ids.insert(identity);
    } else {
        state->selected_update_ids.erase(identity);
    }
    update_selection_controls(state);
}

GtkWidget *make_update_row(
    WindowState *state,
    const PackageRecord &package)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(row, "package-row");
    gtk_widget_set_margin_top(row, 6);
    gtk_widget_set_margin_bottom(row, 6);
    gtk_widget_set_margin_start(row, 8);
    gtk_widget_set_margin_end(row, 8);

    const std::string identity_key = update_identity(package);
    GtkWidget *selected = gtk_check_button_new();
    gtk_widget_set_tooltip_text(selected, "Include this package in the update");
    gtk_widget_set_valign(selected, GTK_ALIGN_CENTER);
    gtk_check_button_set_active(
        GTK_CHECK_BUTTON(selected),
        state != nullptr &&
            state->selected_update_ids.find(identity_key) !=
                state->selected_update_ids.end());
    g_object_set_data_full(
        G_OBJECT(selected),
        "update-identity",
        g_strdup(identity_key.c_str()),
        g_free);
    g_signal_connect(
        selected, "toggled",
        G_CALLBACK(update_selection_toggled), state);
    gtk_box_append(GTK_BOX(row), selected);

    GtkWidget *icon = make_icon(update_icon_name(package), 24);
    gtk_widget_add_css_class(icon, "package-icon");
    gtk_box_append(GTK_BOX(row), icon);

    GtkWidget *identity = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(identity, true);

    GtkWidget *name = make_label(package.name.c_str(), "card-title");
    gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(identity), name);

    std::string version =
        package.installed_version + "  →  " + package.available_version;
    GtkWidget *version_label = make_label(version.c_str(), "card-copy");
    gtk_label_set_ellipsize(
        GTK_LABEL(version_label), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(identity), version_label);

    std::string source_name = package.repository_origin;
    if (source_name.empty()) {
        source_name = package.repository_site;
    }
    if (source_name.empty()) {
        source_name = package.source;
    }
    if (!package.repository_site.empty() &&
        package.repository_site != source_name) {
        source_name += " (" + package.repository_site + ")";
    }

    std::string policy_name = "Repository default";
    if (package.policy_provider == "host-apt-preferences") {
        policy_name = "Host preferred";
    } else if (
        package.policy_provider == "infiltrator-distribution") {
        policy_name = "Infiltrator preferred";
    } else if (!package.policy_provider.empty() &&
               package.policy_provider != "repository-default") {
        policy_name = package.policy_provider;
    }

    std::string source_meta =
        std::string(infiltrator::software::package_kind_name(package.kind));
    if (!source_name.empty()) {
        source_meta += "  •  Source: " + source_name;
    }
    GtkWidget *source_label =
        make_label(source_meta.c_str(), "discover-meta");
    gtk_label_set_ellipsize(
        GTK_LABEL(source_label), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(identity), source_label);

    std::string policy_meta =
        "Policy: " + policy_name;
    if (package.candidate_priority != 0) {
        policy_meta +=
            "  •  priority " +
            std::to_string(package.candidate_priority);
    }
    GtkWidget *policy_label =
        make_label(policy_meta.c_str(), "discover-meta");
    gtk_label_set_ellipsize(
        GTK_LABEL(policy_label), PANGO_ELLIPSIZE_END);
    std::string explanation = package.policy_reason;
    if (!package.selection_reason.empty()) {
        if (!explanation.empty()) {
            explanation += " ";
        }
        explanation += package.selection_reason;
    }
    if (!explanation.empty()) {
        gtk_widget_set_tooltip_text(
            policy_label, explanation.c_str());
    }
    gtk_box_append(GTK_BOX(identity), policy_label);

    gtk_box_append(GTK_BOX(row), identity);

    GtkWidget *recommended =
        make_label(
            package.system_critical
                ? "Recommended • System-critical"
                : "Recommended update",
            package.system_critical
                ? "state-warning"
                : "state-available");
    gtk_widget_set_valign(recommended, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(row), recommended);

    return row;
}

void rebuild_updates(WindowState *state)
{
    if (state == nullptr || state->updates_list == nullptr) {
        return;
    }

    GtkWidget *child =
        gtk_widget_get_first_child(GTK_WIDGET(state->updates_list));
    while (child != nullptr) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_list_box_remove(state->updates_list, child);
        child = next;
    }

    std::size_t critical_count = 0U;
    for (const PackageRecord &package : state->update_records) {
        if (package.system_critical) {
            ++critical_count;
        }
        GtkWidget *row = gtk_list_box_row_new();
        gtk_list_box_row_set_child(
            GTK_LIST_BOX_ROW(row), make_update_row(state, package));
        gtk_list_box_append(state->updates_list, row);
    }

    if (state->updates_count != nullptr) {
        const std::string count =
            std::to_string(state->update_records.size());
        gtk_label_set_text(
            GTK_LABEL(state->updates_count), count.c_str());
    }
    if (state->updates_critical != nullptr) {
        const std::string critical =
            std::to_string(critical_count);
        gtk_label_set_text(
            GTK_LABEL(state->updates_critical), critical.c_str());
    }
}

void updates_worker(
    GTask *task,
    gpointer,
    gpointer task_data,
    GCancellable *)
{
    auto *data = static_cast<UpdatesTaskData *>(task_data);
    auto *result = new UpdatesResult{};
    result->generation = data == nullptr ? 0U : data->generation;

    /*
     * Update inventory and repository refresh now both go through the shared
     * native engine.  The GUI never launches an APT process merely to discover
     * package state.  If no generation exists yet, initialise it once through
     * the same reconciliation path before retrying the inventory read.
     */
    result->from_engine = true;
    if (data == nullptr) {
        result->error = "Update task state is unavailable.";
    } else {
        EngineClient engine;

        /*
         * Ordinary inventory reads reconcile authoritative dpkg state against
         * the already verified repository generation. This is local and fast:
         * packages that were just installed disappear from Updates without
         * paying for another network refresh. Explicit/periodic metadata
         * refreshes still run the complete repository reconciliation.
         */
        if (data->refresh_metadata) {
            result->refreshed_metadata = true;
            (void)engine.refresh(result->error);
        } else {
            (void)engine.refresh_installed(result->error);
        }

        if (result->error.empty()) {
            (void)engine.list_updates(
                result->records,
                result->error);
        }
    }

    g_task_return_pointer(
        task,
        result,
        [](gpointer pointer) {
            delete static_cast<UpdatesResult *>(pointer);
        });
}

gboolean auto_refresh_updates_idle(gpointer user_data)
{
    auto *window = GTK_WINDOW(user_data);
    if (window == nullptr) {
        return G_SOURCE_REMOVE;
    }

    auto *state = static_cast<WindowState *>(
        g_object_get_data(
            G_OBJECT(window),
            "infiltrator-window-state"));
    if (state != nullptr && !state->updates_busy) {
        refresh_updates(state, true);
    }
    return G_SOURCE_REMOVE;
}

void updates_complete(
    GObject *source_object,
    GAsyncResult *async_result,
    gpointer)
{
    auto *window = GTK_WINDOW(source_object);
    auto *state = static_cast<WindowState *>(
        g_object_get_data(
            G_OBJECT(window), "infiltrator-window-state"));
    auto *result = static_cast<UpdatesResult *>(
        g_task_propagate_pointer(G_TASK(async_result), nullptr));

    if (state == nullptr || result == nullptr) {
        delete result;
        return;
    }

    if (result->generation != state->updates_generation) {
        delete result;
        return;
    }

    state->updates_busy = false;
    stop_update_progress(state);
    state->update_records = std::move(result->records);
    state->updates_from_engine = result->from_engine;
    state->selected_update_ids.clear();
    for (const PackageRecord &package : state->update_records) {
        const std::string identity = update_identity(package);
        if (!identity.empty()) {
            state->selected_update_ids.insert(identity);
        }
    }
    const bool refreshed_metadata = result->refreshed_metadata;
    const bool from_engine = result->from_engine;
    const std::string error = result->error;
    delete result;

    if (refreshed_metadata && error.empty()) {
        state->updates_last_metadata_refresh_us =
            g_get_monotonic_time();
    }

    const bool post_install =
        state->updates_post_install_refresh;
    state->updates_post_install_refresh = false;

    /*
     * Present the coherent local generation immediately, then reconcile
     * repository metadata once per application session. Do not start a second
     * network refresh immediately after an install: the privileged executor
     * has already refreshed metadata and the local dpkg reconciliation above
     * is sufficient to prove which approved versions are now installed.
     */
    const bool schedule_auto_refresh =
        !post_install &&
        !refreshed_metadata &&
        error.empty() &&
        state->updates_auto_refresh_pending;
    if (schedule_auto_refresh || refreshed_metadata) {
        state->updates_auto_refresh_pending = false;
    }

    if (state->updates_backend != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->updates_backend),
            from_engine
                ? "Native engine"
                : "Native engine unavailable");
    }

    rebuild_updates(state);

    if (state->updates_status != nullptr) {
        if (!error.empty()) {
            const std::string message =
                std::string(
                    post_install
                        ? "Updates were installed, but final state verification failed: "
                        : refreshed_metadata
                            ? "Unable to refresh package metadata: "
                            : "Unable to check for updates: ") +
                one_line(error);
            gtk_label_set_text(
                GTK_LABEL(state->updates_status), message.c_str());
        } else if (post_install) {
            const std::string message =
                state->update_records.empty()
                    ? "Installation complete. Final package state verified; the system is up to date."
                    : "Installation complete. Final package state verified; " +
                        std::to_string(state->update_records.size()) +
                        (state->update_records.size() == 1U
                             ? " preferred update remains."
                             : " preferred updates remain.");
            gtk_label_set_text(
                GTK_LABEL(state->updates_status), message.c_str());
        } else if (schedule_auto_refresh) {
            gtk_label_set_text(
                GTK_LABEL(state->updates_status),
                state->update_records.empty()
                    ? "Cached package state has no updates; checking repositories for newer metadata…"
                    : "Cached updates loaded; checking repositories for newer metadata…");
        } else if (state->update_records.empty()) {
            gtk_label_set_text(
                GTK_LABEL(state->updates_status),
                "Your system is up to date.");
        } else {
            const std::string message =
                std::to_string(state->update_records.size()) +
                (state->update_records.size() == 1U
                     ? " update is available."
                     : " updates are available.");
            gtk_label_set_text(
                GTK_LABEL(state->updates_status), message.c_str());
        }
    }

    if (state->updates_install != nullptr) {
        if (!error.empty()) {
            gtk_widget_set_sensitive(state->updates_install, false);
        } else {
            update_selection_controls(state);
        }
    }
    if (state->updates_refresh != nullptr) {
        gtk_widget_set_sensitive(state->updates_refresh, true);
    }

    if (error.empty()) {
        set_update_runtime_state({});
    } else {
        set_update_runtime_state(
            "error:" + one_line(error));
    }

    if (post_install) {
        stop_update_progress(state);
        if (error.empty()) {
            /*
             * The native refresh has now published the authoritative
             * post-transaction generation.  Only now may other pages reload.
             */
            refresh_installed(state);
            refresh_discover(state);
            refresh_repositories(state);
        }
    }

    if (schedule_auto_refresh &&
        state->window != nullptr) {
        g_idle_add_full(
            G_PRIORITY_DEFAULT_IDLE,
            auto_refresh_updates_idle,
            g_object_ref(state->window),
            reinterpret_cast<GDestroyNotify>(g_object_unref));
    }
}

void refresh_updates(WindowState *state, const bool refresh_metadata)
{
    if (state == nullptr || state->window == nullptr ||
        state->updates_list == nullptr || state->updates_busy) {
        return;
    }

    if (refresh_metadata) {
        state->updates_auto_refresh_pending = false;
    }

    state->updates_loaded = true;
    state->updates_busy = true;
    ++state->updates_generation;

    if (state->updates_status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->updates_status),
            state->updates_post_install_refresh
                ? "Installation finished. Verifying installed versions and remaining updates…"
                : refresh_metadata
                    ? "Refreshing repository metadata without administrator access…"
                    : "Checking installed versions and available updates…");
    }
    if (state->updates_install != nullptr) {
        gtk_widget_set_sensitive(state->updates_install, false);
    }
    if (state->updates_refresh != nullptr) {
        gtk_widget_set_sensitive(state->updates_refresh, false);
    }

    set_update_runtime_state("checking");

    auto *data = new UpdatesTaskData{
        state->updates_generation, refresh_metadata};
    GTask *task = g_task_new(
        G_OBJECT(state->window),
        nullptr,
        updates_complete,
        nullptr);
    g_task_set_task_data(
        task, data,
        [](gpointer pointer) {
            delete static_cast<UpdatesTaskData *>(pointer);
        });
    g_task_run_in_thread(task, updates_worker);
    g_object_unref(task);
}

void destroy_update_process_run(UpdateProcessRun *run)
{
    if (run == nullptr) {
        return;
    }
    if (run->window != nullptr) {
        g_object_unref(run->window);
    }
    delete run;
}

void update_process_complete(
    GObject *source_object,
    GAsyncResult *async_result,
    gpointer user_data)
{
    auto *run = static_cast<UpdateProcessRun *>(user_data);
    auto *process = G_SUBPROCESS(source_object);

    GError *error = nullptr;
    gchar *stdout_text = nullptr;
    gchar *stderr_text = nullptr;
    const gboolean communicated =
        g_subprocess_communicate_utf8_finish(
            process, async_result,
            &stdout_text, &stderr_text, &error);

    auto *state =
        run == nullptr || run->window == nullptr
            ? nullptr
            : static_cast<WindowState *>(
                  g_object_get_data(
                      G_OBJECT(run->window),
                      "infiltrator-window-state"));

    const bool success =
        communicated && g_subprocess_get_successful(process);
    const bool no_changes_required =
        success &&
        stdout_text != nullptr &&
        std::string_view(stdout_text).find(
            "INFILTRATOR_NO_CHANGES_REQUIRED") !=
            std::string_view::npos;

    if (state != nullptr) {
        state->updates_busy = false;

        if (run != nullptr &&
            run->operation == "install" &&
            !run->plan.items.empty()) {
            std::string history_message;
            if (no_changes_required) {
                history_message =
                    "Approved package versions were already installed; no package changes were required.";
            } else if (success) {
                history_message = "Transaction completed successfully.";
            } else if (g_subprocess_get_if_exited(process) &&
                       g_subprocess_get_exit_status(process) == 126) {
                history_message = "Administrator authentication was cancelled.";
            } else if (stderr_text != nullptr && *stderr_text != '\0') {
                history_message = one_line(stderr_text);
            } else if (error != nullptr && error->message != nullptr) {
                history_message = one_line(error->message);
            } else {
                history_message = "Transaction failed.";
            }
            record_transaction_history(
                run->plan, success, history_message);
            if (state->history_loaded) {
                refresh_history(state);
            }
        }

        if (success) {
            set_update_runtime_state({});
            if (state->updates_status != nullptr) {
                gtk_label_set_text(
                    GTK_LABEL(state->updates_status),
                    run->operation == "refresh"
                        ? "Package lists refreshed. Checking updates…"
                        : no_changes_required
                            ? "Selected updates were already installed. Checking current package state…"
                            : "Updates installed. Checking system state…");
            }

            state->updates_post_install_refresh =
                run->operation == "install";
            /*
             * Re-read authoritative dpkg state first. Repository metadata was
             * already refreshed by the privileged helper before mutation, so
             * repeating the full network reconciliation here only delays the
             * UI and can leave completed packages visible as stale updates.
             */
            refresh_updates(
                state,
                run->operation == "refresh");
        } else {
            stop_update_progress(state);
            std::string message =
                run->operation == "refresh"
                    ? "Unable to refresh package lists."
                    : "Unable to install updates.";

            if (g_subprocess_get_if_exited(process) &&
                g_subprocess_get_exit_status(process) == 126) {
                message = "Authentication was cancelled.";
                set_update_runtime_state({});
            } else {
                if (stderr_text != nullptr && *stderr_text != '\0') {
                    message += " ";
                    message += one_line(stderr_text);
                } else if (error != nullptr &&
                           error->message != nullptr) {
                    message += " ";
                    message += one_line(error->message);
                }
                set_update_runtime_state(
                    "error:" + one_line(message));
            }

            if (state->updates_status != nullptr) {
                gtk_label_set_text(
                    GTK_LABEL(state->updates_status),
                    message.c_str());
            }
            if (state->updates_install != nullptr) {
                update_selection_controls(state);
            }
            if (state->updates_refresh != nullptr) {
                gtk_widget_set_sensitive(
                    state->updates_refresh, true);
            }
        }
    }

    g_free(stdout_text);
    g_free(stderr_text);
    g_clear_error(&error);
    destroy_update_process_run(run);
}

void start_update_process(
    WindowState *state,
    std::vector<std::string> arguments,
    const std::string &operation,
    TransactionPlan plan)
{
    if (state == nullptr || state->window == nullptr ||
        arguments.empty()) {
        return;
    }

    std::vector<const gchar *> argv;
    argv.reserve(arguments.size() + 1U);
    for (const std::string &argument : arguments) {
        argv.push_back(argument.c_str());
    }
    argv.push_back(nullptr);

    GError *error = nullptr;
    GSubprocess *process = g_subprocess_newv(
        argv.data(),
        static_cast<GSubprocessFlags>(
            G_SUBPROCESS_FLAGS_STDOUT_PIPE |
            G_SUBPROCESS_FLAGS_STDERR_PIPE),
        &error);

    if (process == nullptr) {
        state->updates_busy = false;
        std::string message =
            operation == "refresh"
                ? "Unable to start package-list refresh."
                : "Unable to start update installation.";
        if (error != nullptr && error->message != nullptr) {
            message += " ";
            message += one_line(error->message);
        }
        if (state->updates_status != nullptr) {
            gtk_label_set_text(
                GTK_LABEL(state->updates_status),
                message.c_str());
        }
        set_update_runtime_state(
            "error:" + one_line(message));
        g_clear_error(&error);
        if (state->updates_refresh != nullptr) {
            gtk_widget_set_sensitive(
                state->updates_refresh, true);
        }
        if (state->updates_install != nullptr) {
            update_selection_controls(state);
        }
        stop_update_progress(state);
        if (!plan.items.empty()) {
            record_transaction_history(
                plan, false, message);
            if (state->history_loaded) {
                refresh_history(state);
            }
        }
        return;
    }

    auto *run = new UpdateProcessRun{
        GTK_WINDOW(g_object_ref(state->window)),
        operation,
        std::move(plan)};

    g_subprocess_communicate_utf8_async(
        process, nullptr, nullptr,
        update_process_complete, run);
    g_object_unref(process);
}

void update_refresh_clicked(GtkButton *, gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    if (state == nullptr || state->updates_busy) {
        return;
    }

    /*
     * Refresh is read-only and is owned by the shared native engine.  It
     * verifies configured repository metadata, publishes a new coherent
     * generation, and requires no administrator prompt.
     */
    refresh_updates(state, true);
}

void begin_apply_updates(WindowState *state)
{
    if (state == nullptr || !state->pending_update_plan.has_value()) {
        return;
    }

    std::vector<std::string> specs;
    std::string plan_error;
    if (!exact_plan_specs(
            *state->pending_update_plan, specs, plan_error)) {
        state->updates_busy = false;
        state->pending_update_plan.reset();
        if (state->updates_status != nullptr) {
            gtk_label_set_text(
                GTK_LABEL(state->updates_status),
                plan_error.c_str());
        }
        if (state->updates_install != nullptr) {
            update_selection_controls(state);
        }
        if (state->updates_refresh != nullptr) {
            gtk_widget_set_sensitive(state->updates_refresh, true);
        }
        set_update_runtime_state("error:" + one_line(plan_error));
        return;
    }

    const TransactionPlan approved_plan =
        *state->pending_update_plan;

    state->updates_busy = true;
    if (state->updates_status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->updates_status),
            "Starting the approved update transaction; administrator authentication may be requested…");
    }
    if (state->updates_install != nullptr) {
        gtk_widget_set_sensitive(state->updates_install, false);
    }
    if (state->updates_refresh != nullptr) {
        gtk_widget_set_sensitive(state->updates_refresh, false);
    }
    set_update_runtime_state("installing");
    start_update_progress(state);

    std::vector<std::string> arguments{
        "pkexec",
        "/usr/libexec/infiltrator-software-update-helper",
        "apply-plan"};
    arguments.reserve(specs.size() + 3U);
    arguments.insert(arguments.end(), specs.begin(), specs.end());

    state->pending_update_plan.reset();
    start_update_process(
        state,
        std::move(arguments),
        "install",
        approved_plan);
}

void update_confirm_response(
    GtkDialog *dialog,
    gint response_id,
    gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    gtk_window_destroy(GTK_WINDOW(dialog));

    if (state == nullptr) {
        return;
    }

    if (response_id == GTK_RESPONSE_ACCEPT) {
        begin_apply_updates(state);
        return;
    }

    state->updates_busy = false;
    state->pending_update_plan.reset();
    if (state->updates_status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->updates_status),
            "Update installation cancelled.");
    }
    if (state->updates_install != nullptr) {
        update_selection_controls(state);
    }
    if (state->updates_refresh != nullptr) {
        gtk_widget_set_sensitive(state->updates_refresh, true);
    }
    set_update_runtime_state({});
}

void update_plan_worker(
    GTask *task,
    gpointer,
    gpointer task_data,
    GCancellable *)
{
    auto *data = static_cast<UpdatePlanTaskData *>(task_data);
    auto *result = new UpdatePlanResult{};

    if (data == nullptr || data->package_ids.empty()) {
        result->error = "No updates are available to plan.";
    } else {
        infiltrator::software::TransactionRequest request;
        request.action =
            infiltrator::software::TransactionAction::upgrade;
        request.package_ids = data->package_ids;

        if (data->use_engine) {
            EngineClient engine;
            result->plan =
                engine.plan(request, result->error);
            result->from_engine =
                result->plan.has_value();
        } else {
            AptBackend fallback;
            result->plan =
                fallback.plan(request, result->error);
        }
    }

    g_task_return_pointer(
        task,
        result,
        [](gpointer pointer) {
            delete static_cast<UpdatePlanResult *>(pointer);
        });
}

void update_plan_complete(
    GObject *source_object,
    GAsyncResult *async_result,
    gpointer)
{
    auto *window = GTK_WINDOW(source_object);
    auto *state = static_cast<WindowState *>(
        g_object_get_data(
            G_OBJECT(window), "infiltrator-window-state"));
    auto *result = static_cast<UpdatePlanResult *>(
        g_task_propagate_pointer(G_TASK(async_result), nullptr));

    if (state == nullptr || result == nullptr) {
        delete result;
        return;
    }

    if (!result->plan.has_value()) {
        state->updates_busy = false;
        state->pending_update_plan.reset();
        std::string message =
            "Unable to plan updates: " + one_line(result->error);
        if (state->updates_status != nullptr) {
            gtk_label_set_text(
                GTK_LABEL(state->updates_status),
                message.c_str());
        }
        if (state->updates_install != nullptr) {
            update_selection_controls(state);
        }
        if (state->updates_refresh != nullptr) {
            gtk_widget_set_sensitive(state->updates_refresh, true);
        }
        set_update_runtime_state(
            "error:" + one_line(message));
        delete result;
        return;
    }

    const infiltrator::software::TransactionPlan plan =
        *result->plan;
    const bool from_engine = result->from_engine;
    delete result;

    state->pending_update_plan = plan;

    const std::size_t requested_count =
        static_cast<std::size_t>(std::count_if(
            plan.items.begin(),
            plan.items.end(),
            [](const infiltrator::software::TransactionItem &item) {
                return item.requested;
            }));
    std::ostringstream heading;
    heading << "Install "
            << requested_count
            << (requested_count == 1U
                    ? " selected software update?"
                    : " selected software updates?");

    GtkWidget *dialog =
        make_transaction_confirmation_dialog(
            state->window,
            "Review updates",
            heading.str(),
            "Install updates",
            plan,
            from_engine);
    g_signal_connect(
        dialog, "response",
        G_CALLBACK(update_confirm_response), state);
    gtk_window_present(GTK_WINDOW(dialog));
}

void updates_select_all_clicked(GtkButton *, gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    if (state == nullptr || state->updates_busy) return;

    state->selected_update_ids.clear();
    for (const PackageRecord &package : state->update_records) {
        const std::string identity = update_identity(package);
        if (!identity.empty()) state->selected_update_ids.insert(identity);
    }
    rebuild_updates(state);
    update_selection_controls(state);
}

void updates_clear_selection_clicked(GtkButton *, gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    if (state == nullptr || state->updates_busy) return;

    state->selected_update_ids.clear();
    rebuild_updates(state);
    update_selection_controls(state);
}

void update_install_clicked(GtkButton *, gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    if (state == nullptr || state->window == nullptr ||
        state->updates_busy || state->update_records.empty() ||
        state->selected_update_ids.empty()) {
        return;
    }

    state->updates_busy = true;
    state->pending_update_plan.reset();
    if (state->updates_status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->updates_status),
            "Resolving the complete update transaction…");
    }
    if (state->updates_install != nullptr) {
        gtk_widget_set_sensitive(state->updates_install, false);
    }
    if (state->updates_refresh != nullptr) {
        gtk_widget_set_sensitive(state->updates_refresh, false);
    }
    set_update_runtime_state("checking");

    auto *data = new UpdatePlanTaskData{};
    data->use_engine = state->updates_from_engine;
    data->package_ids.reserve(state->selected_update_ids.size());
    for (const PackageRecord &package : state->update_records) {
        const std::string identity = update_identity(package);
        if (state->selected_update_ids.find(identity) !=
            state->selected_update_ids.end()) {
            data->package_ids.push_back(identity);
        }
    }

    GTask *task = g_task_new(
        G_OBJECT(state->window),
        nullptr,
        update_plan_complete,
        nullptr);
    g_task_set_task_data(
        task, data,
        [](gpointer pointer) {
            delete static_cast<UpdatePlanTaskData *>(pointer);
        });
    g_task_run_in_thread(task, update_plan_worker);
    g_object_unref(task);
}

GtkWidget *make_updates_page(WindowState *state)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(page, "content");
    gtk_widget_add_css_class(page, "page-updates");

    gtk_box_append(
        GTK_BOX(page),
        make_page_intro(
            "software-update-available-symbolic",
            "Updates",
            "Application, library, kernel and system updates in one place."));

    GtkWidget *stats = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(stats), 10);
    gtk_grid_set_column_homogeneous(GTK_GRID(stats), true);
    gtk_grid_attach(
        GTK_GRID(stats),
        make_stat_card(
            "AVAILABLE", "0", "stat-operation",
            &state->updates_count),
        0, 0, 1, 1);
    gtk_grid_attach(
        GTK_GRID(stats),
        make_stat_card(
            "SYSTEM-CRITICAL", "0", "stat-warning",
            &state->updates_critical),
        1, 0, 1, 1);
    gtk_grid_attach(
        GTK_GRID(stats),
        make_stat_card(
            "BACKEND", "Loading", "stat-info",
            &state->updates_backend),
        2, 0, 1, 1);
    gtk_box_append(GTK_BOX(page), stats);

    GtkWidget *controls =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(controls, "card");

    state->updates_status =
        make_label("Checking for updates…", "card-copy");
    gtk_label_set_wrap(GTK_LABEL(state->updates_status), true);
    gtk_widget_set_hexpand(state->updates_status, true);
    gtk_box_append(GTK_BOX(controls), state->updates_status);

    GtkWidget *select_all =
        gtk_button_new_with_label("Select all");
    gtk_widget_add_css_class(select_all, "discover-details");
    g_signal_connect(
        select_all, "clicked",
        G_CALLBACK(updates_select_all_clicked), state);
    gtk_box_append(GTK_BOX(controls), select_all);

    GtkWidget *clear_selection =
        gtk_button_new_with_label("Clear");
    gtk_widget_add_css_class(clear_selection, "discover-details");
    g_signal_connect(
        clear_selection, "clicked",
        G_CALLBACK(updates_clear_selection_clicked), state);
    gtk_box_append(GTK_BOX(controls), clear_selection);

    state->updates_refresh =
        gtk_button_new_with_label("Refresh package lists");
    gtk_widget_add_css_class(
        state->updates_refresh, "discover-details");
    g_signal_connect(
        state->updates_refresh, "clicked",
        G_CALLBACK(update_refresh_clicked), state);
    gtk_box_append(GTK_BOX(controls), state->updates_refresh);

    state->updates_install =
        gtk_button_new_with_label("Install selected updates");
    gtk_widget_add_css_class(
        state->updates_install, "suggested-action");
    gtk_widget_set_sensitive(state->updates_install, false);
    g_signal_connect(
        state->updates_install, "clicked",
        G_CALLBACK(update_install_clicked), state);
    gtk_box_append(GTK_BOX(controls), state->updates_install);

    gtk_box_append(GTK_BOX(page), controls);

    state->updates_progress = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(
        GTK_PROGRESS_BAR(state->updates_progress), false);
    gtk_widget_set_visible(state->updates_progress, false);
    gtk_widget_set_tooltip_text(
        state->updates_progress,
        "Software is actively processing the approved transaction.");
    gtk_box_append(GTK_BOX(page), state->updates_progress);

    GtkWidget *card =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(card, "card");
    gtk_widget_add_css_class(card, "card-info");
    gtk_widget_set_vexpand(card, true);

    GtkWidget *heading =
        make_label("Available updates", "card-title");
    gtk_box_append(GTK_BOX(card), heading);

    GtkWidget *list = gtk_list_box_new();
    state->updates_list = GTK_LIST_BOX(list);
    gtk_widget_add_css_class(list, "package-list");
    gtk_list_box_set_selection_mode(
        state->updates_list, GTK_SELECTION_NONE);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, true);
    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(
        GTK_SCROLLED_WINDOW(scroll), list);
    gtk_box_append(GTK_BOX(card), scroll);

    gtk_box_append(GTK_BOX(page), card);

    return page;
}


struct AddSourceDialog {
    GtkWindow *window{};
    GtkWindow *main_window{};
    GtkWidget *type{};
    GtkWidget *name{};
    GtkWidget *url{};
    GtkWidget *suite{};
    GtkWidget *components{};
    GtkWidget *signed_by{};
    GtkWidget *status{};
};

struct AddSourceRun {
    GtkWindow *dialog{};
    GtkWindow *main_window{};
};

void destroy_add_source_dialog(gpointer data)
{
    delete static_cast<AddSourceDialog *>(data);
}

void destroy_add_source_run(AddSourceRun *run)
{
    if (run == nullptr) {
        return;
    }
    if (run->dialog != nullptr) {
        g_object_unref(run->dialog);
    }
    if (run->main_window != nullptr) {
        g_object_unref(run->main_window);
    }
    delete run;
}

std::string entry_text(GtkWidget *widget)
{
    if (widget == nullptr || !GTK_IS_EDITABLE(widget)) {
        return {};
    }
    const char *text = gtk_editable_get_text(GTK_EDITABLE(widget));
    return text == nullptr ? std::string{} : std::string{text};
}

void add_source_process_complete(
    GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
    auto *run = static_cast<AddSourceRun *>(user_data);
    auto *process = G_SUBPROCESS(source_object);

    GError *error = nullptr;
    gchar *stdout_text = nullptr;
    gchar *stderr_text = nullptr;
    const gboolean communicated =
        g_subprocess_communicate_utf8_finish(
            process,
            result,
            &stdout_text,
            &stderr_text,
            &error);

    auto *state = run == nullptr || run->main_window == nullptr
        ? nullptr
        : static_cast<WindowState *>(
              g_object_get_data(
                  G_OBJECT(run->main_window),
                  "infiltrator-window-state"));

    auto *dialog_context =
        run == nullptr || run->dialog == nullptr
            ? nullptr
            : static_cast<AddSourceDialog *>(
                  g_object_get_data(
                      G_OBJECT(run->dialog),
                      "add-source-context"));

    bool success = communicated &&
                   g_subprocess_get_successful(process);

    if (dialog_context != nullptr &&
        dialog_context->status != nullptr) {
        if (success) {
            gtk_label_set_text(
                GTK_LABEL(dialog_context->status),
                "Source added. Refreshing repositories and Discover…");
        } else {
            std::string message = "Unable to add source.";
            if (error != nullptr && error->message != nullptr) {
                message += " ";
                message += error->message;
            } else if (stderr_text != nullptr &&
                       *stderr_text != '\0') {
                message += " ";
                message += stderr_text;
            }
            gtk_label_set_text(
                GTK_LABEL(dialog_context->status),
                message.c_str());
        }
    }

    if (success && state != nullptr) {
        refresh_repositories(state);
        refresh_discover(state, true);
        if (run != nullptr && run->dialog != nullptr) {
            gtk_window_destroy(run->dialog);
        }
    }

    g_free(stdout_text);
    g_free(stderr_text);
    g_clear_error(&error);
    destroy_add_source_run(run);
}

void add_source_submit(GtkButton *, gpointer user_data)
{
    auto *context = static_cast<AddSourceDialog *>(user_data);
    if (context == nullptr ||
        context->window == nullptr ||
        context->main_window == nullptr) {
        return;
    }

    const std::string name = entry_text(context->name);
    const std::string url = entry_text(context->url);
    const std::string suite = entry_text(context->suite);
    const std::string components = entry_text(context->components);
    const std::string signed_by = entry_text(context->signed_by);
    const guint selected =
        gtk_drop_down_get_selected(GTK_DROP_DOWN(context->type));

    if (name.empty() || url.rfind("https://", 0U) != 0U) {
        gtk_label_set_text(
            GTK_LABEL(context->status),
            "Name is required and the source URL must use HTTPS.");
        return;
    }

    std::vector<std::string> arguments;
    if (selected == 0U) {
        if (suite.empty() || components.empty()) {
            gtk_label_set_text(
                GTK_LABEL(context->status),
                "APT sources require a suite and at least one component.");
            return;
        }
        arguments = {
            "pkexec",
            "/usr/libexec/infiltrator-software-helper",
            "add-apt-source",
            name,
            url,
            suite,
            components,
            signed_by
        };
    } else {
        arguments = {
            "flatpak",
            "remote-add",
            "--user",
            "--if-not-exists",
            name,
            url
        };
    }

    std::vector<const gchar *> argv;
    argv.reserve(arguments.size() + 1U);
    for (const std::string &argument : arguments) {
        argv.push_back(argument.c_str());
    }
    argv.push_back(nullptr);

    GError *error = nullptr;
    GSubprocess *process = g_subprocess_newv(
        argv.data(),
        static_cast<GSubprocessFlags>(
            G_SUBPROCESS_FLAGS_STDOUT_PIPE |
            G_SUBPROCESS_FLAGS_STDERR_PIPE),
        &error);

    if (process == nullptr) {
        std::string message = "Unable to start source management.";
        if (error != nullptr && error->message != nullptr) {
            message += " ";
            message += error->message;
        }
        gtk_label_set_text(
            GTK_LABEL(context->status),
            message.c_str());
        g_clear_error(&error);
        return;
    }

    gtk_label_set_text(
        GTK_LABEL(context->status),
        selected == 0U
            ? "Waiting for administrator authorization…"
            : "Adding Flatpak remote…");

    auto *run = new AddSourceRun{};
    run->dialog = GTK_WINDOW(g_object_ref(context->window));
    run->main_window =
        GTK_WINDOW(g_object_ref(context->main_window));

    g_subprocess_communicate_utf8_async(
        process,
        nullptr,
        nullptr,
        add_source_process_complete,
        run);
    g_object_unref(process);
}

void add_source_type_changed(
    GObject *object,
    GParamSpec *,
    gpointer user_data)
{
    auto *context = static_cast<AddSourceDialog *>(user_data);
    if (context == nullptr) {
        return;
    }

    const bool apt =
        gtk_drop_down_get_selected(GTK_DROP_DOWN(object)) == 0U;
    gtk_widget_set_sensitive(context->suite, apt);
    gtk_widget_set_sensitive(context->components, apt);
    gtk_widget_set_sensitive(context->signed_by, apt);

    if (context->status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(context->status),
            apt
                ? "APT sources are written as modern .sources files and require administrator authorization."
                : "Flatpak remotes are added for your user account and do not require administrator authorization.");
    }
}

GtkWidget *form_row(const char *caption, GtkWidget *control)
{
    GtkWidget *row =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *label =
        make_label(caption, "detail-label");
    gtk_widget_set_size_request(label, 120, -1);
    gtk_widget_set_hexpand(control, true);
    gtk_box_append(GTK_BOX(row), label);
    gtk_box_append(GTK_BOX(row), control);
    return row;
}

void add_source_clicked(GtkButton *, gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    if (state == nullptr || state->window == nullptr) {
        return;
    }

    GtkWidget *window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window), "Add Software Source");
    gtk_window_set_default_size(GTK_WINDOW(window), 620, 440);
    gtk_window_set_transient_for(GTK_WINDOW(window), state->window);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(window), true);
    gtk_window_set_modal(GTK_WINDOW(window), true);

    auto *context = new AddSourceDialog{};
    context->window = GTK_WINDOW(window);
    context->main_window = state->window;
    g_object_set_data_full(
        G_OBJECT(window),
        "add-source-context",
        context,
        destroy_add_source_dialog);

    GtkWidget *outer =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(outer, 24);
    gtk_widget_set_margin_end(outer, 24);
    gtk_widget_set_margin_top(outer, 24);
    gtk_widget_set_margin_bottom(outer, 24);
    gtk_window_set_child(GTK_WINDOW(window), outer);

    gtk_box_append(
        GTK_BOX(outer),
        make_page_intro(
            "list-add-symbolic",
            "Add Software Source",
            "Add an APT repository or Flatpak remote to Software."));

    static const char *types[] = {
        "APT repository",
        "Flatpak remote",
        nullptr
    };
    context->type = gtk_drop_down_new_from_strings(types);
    gtk_box_append(
        GTK_BOX(outer),
        form_row("Type", context->type));

    context->name = gtk_entry_new();
    gtk_entry_set_placeholder_text(
        GTK_ENTRY(context->name), "e.g. flathub or vendor-name");
    gtk_box_append(
        GTK_BOX(outer),
        form_row("Name", context->name));

    context->url = gtk_entry_new();
    gtk_entry_set_placeholder_text(
        GTK_ENTRY(context->url), "https://…");
    gtk_box_append(
        GTK_BOX(outer),
        form_row("URL", context->url));

    context->suite = gtk_entry_new();
    gtk_entry_set_placeholder_text(
        GTK_ENTRY(context->suite), "e.g. noble, stable");
    gtk_box_append(
        GTK_BOX(outer),
        form_row("APT suite", context->suite));

    context->components = gtk_entry_new();
    gtk_entry_set_placeholder_text(
        GTK_ENTRY(context->components), "e.g. main universe");
    gtk_box_append(
        GTK_BOX(outer),
        form_row("Components", context->components));

    context->signed_by = gtk_entry_new();
    gtk_entry_set_placeholder_text(
        GTK_ENTRY(context->signed_by),
        "/etc/apt/keyrings/vendor.gpg (optional)");
    gtk_box_append(
        GTK_BOX(outer),
        form_row("Signed by", context->signed_by));

    context->status = make_label(
        "APT sources are written as modern .sources files and require administrator authorization.",
        "discover-status");
    gtk_label_set_wrap(GTK_LABEL(context->status), true);
    gtk_box_append(GTK_BOX(outer), context->status);

    GtkWidget *actions =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);

    GtkWidget *cancel = gtk_button_new_with_label("Cancel");
    g_signal_connect_swapped(
        cancel,
        "clicked",
        G_CALLBACK(gtk_window_destroy),
        window);
    gtk_box_append(GTK_BOX(actions), cancel);

    GtkWidget *add = gtk_button_new_with_label("Add Source");
    gtk_widget_add_css_class(add, "suggested-action");
    g_signal_connect(
        add,
        "clicked",
        G_CALLBACK(add_source_submit),
        context);
    gtk_box_append(GTK_BOX(actions), add);
    gtk_box_append(GTK_BOX(outer), actions);

    g_signal_connect(
        context->type,
        "notify::selected",
        G_CALLBACK(add_source_type_changed),
        context);

    gtk_window_present(GTK_WINDOW(window));
}

const char *source_icon(const SourceRecord &source) noexcept
{
    switch (source.kind) {
    case infiltrator::software::SourceKind::infiltrator:
        return "emblem-default-symbolic";
    case infiltrator::software::SourceKind::apt:
        return "package-x-generic-symbolic";
    case infiltrator::software::SourceKind::flatpak:
        return "package-x-generic-symbolic";
    }
    return "network-workgroup-symbolic";
}

struct SourceToggleContext {
    GtkWindow *window{};
    SourceRecord source;
};

struct SourceToggleRun {
    GtkWindow *window{};
    bool enabled{false};
    std::string source_name;
};

void destroy_source_toggle_context(gpointer data)
{
    delete static_cast<SourceToggleContext *>(data);
}

void destroy_source_toggle_run(SourceToggleRun *run)
{
    if (run == nullptr) {
        return;
    }
    if (run->window != nullptr) {
        g_object_unref(run->window);
    }
    delete run;
}

void source_toggle_process_complete(
    GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
    auto *run = static_cast<SourceToggleRun *>(user_data);
    auto *process = G_SUBPROCESS(source_object);

    GError *error = nullptr;
    gchar *stdout_text = nullptr;
    gchar *stderr_text = nullptr;
    const gboolean communicated =
        g_subprocess_communicate_utf8_finish(
            process,
            result,
            &stdout_text,
            &stderr_text,
            &error);
    const bool success =
        communicated &&
        g_subprocess_get_successful(process);

    auto *state =
        run == nullptr || run->window == nullptr
            ? nullptr
            : static_cast<WindowState *>(
                  g_object_get_data(
                      G_OBJECT(run->window),
                      "infiltrator-window-state"));

    if (state != nullptr) {
        state->repositories_busy = false;
        if (state->repository_flow != nullptr) {
            gtk_widget_set_sensitive(
                state->repository_flow, true);
        }

        if (success) {
            if (state->repository_status != nullptr) {
                const std::string message =
                    run->source_name +
                    (run->enabled
                         ? " enabled. Refreshing software state…"
                         : " disabled. Refreshing software state…");
                gtk_label_set_text(
                    GTK_LABEL(state->repository_status),
                    message.c_str());
            }

            refresh_repositories(state);
            if (state->discover_loaded) {
                refresh_discover(state, true);
            }
            if (state->updates_loaded) {
                refresh_updates(state, true);
            }
        } else if (state->repository_status != nullptr) {
            std::string message =
                run != nullptr && run->enabled
                    ? "Unable to enable source."
                    : "Unable to disable source.";
            if (stderr_text != nullptr &&
                *stderr_text != '\0') {
                message += " ";
                message += one_line(stderr_text);
            } else if (error != nullptr &&
                       error->message != nullptr) {
                message += " ";
                message += one_line(error->message);
            }
            gtk_label_set_text(
                GTK_LABEL(state->repository_status),
                message.c_str());
        }
    }

    g_free(stdout_text);
    g_free(stderr_text);
    g_clear_error(&error);
    destroy_source_toggle_run(run);
}

void source_toggle_clicked(
    GtkButton *,
    gpointer user_data)
{
    auto *context =
        static_cast<SourceToggleContext *>(user_data);
    if (context == nullptr ||
        context->window == nullptr) {
        return;
    }

    auto *state = static_cast<WindowState *>(
        g_object_get_data(
            G_OBJECT(context->window),
            "infiltrator-window-state"));
    if (state == nullptr || state->repositories_busy) {
        return;
    }

    const bool enable = !context->source.enabled;
    std::vector<std::string> arguments;

    if (context->source.kind ==
        infiltrator::software::SourceKind::apt) {
        if (context->source.backing_file.empty() ||
            context->source.entry_index == 0U) {
            if (state->repository_status != nullptr) {
                gtk_label_set_text(
                    GTK_LABEL(state->repository_status),
                    "This APT source has no mutable source-file identity.");
            }
            return;
        }
        arguments = {
            "pkexec",
            "/usr/libexec/infiltrator-software-helper",
            "set-apt-source-enabled",
            context->source.backing_file,
            std::to_string(context->source.entry_index),
            enable ? "yes" : "no"
        };
    } else if (context->source.kind ==
               infiltrator::software::SourceKind::flatpak) {
        const bool system_scope =
            context->source.scope == "System";
        if (system_scope) {
            arguments = {
                "pkexec",
                "/usr/bin/flatpak",
                "remote-modify",
                "--system",
                enable ? "--enable" : "--disable",
                context->source.name
            };
        } else {
            arguments = {
                "flatpak",
                "remote-modify",
                "--user",
                enable ? "--enable" : "--disable",
                context->source.name
            };
        }
    } else {
        return;
    }

    std::vector<const gchar *> argv;
    argv.reserve(arguments.size() + 1U);
    for (const std::string &argument : arguments) {
        argv.push_back(argument.c_str());
    }
    argv.push_back(nullptr);

    GError *error = nullptr;
    GSubprocess *process =
        g_subprocess_newv(
            argv.data(),
            static_cast<GSubprocessFlags>(
                G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                G_SUBPROCESS_FLAGS_STDERR_PIPE),
            &error);
    if (process == nullptr) {
        if (state->repository_status != nullptr) {
            std::string message =
                enable
                    ? "Unable to enable source."
                    : "Unable to disable source.";
            if (error != nullptr &&
                error->message != nullptr) {
                message += " ";
                message += one_line(error->message);
            }
            gtk_label_set_text(
                GTK_LABEL(state->repository_status),
                message.c_str());
        }
        g_clear_error(&error);
        return;
    }

    state->repositories_busy = true;
    if (state->repository_flow != nullptr) {
        gtk_widget_set_sensitive(
            state->repository_flow, false);
    }
    if (state->repository_status != nullptr) {
        const std::string message =
            std::string(enable ? "Enabling " : "Disabling ") +
            context->source.name + "…";
        gtk_label_set_text(
            GTK_LABEL(state->repository_status),
            message.c_str());
    }

    auto *run = new SourceToggleRun{
        context->window,
        enable,
        context->source.name};
    g_object_ref(run->window);
    g_subprocess_communicate_utf8_async(
        process,
        nullptr,
        nullptr,
        source_toggle_process_complete,
        run);
    g_object_unref(process);
}

GtkWidget *make_source_card(
    WindowState *state,
    const SourceRecord &source)
{
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 7);
    gtk_widget_add_css_class(card, "source-card");

    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *icon = make_icon(source_icon(source), 24);
    gtk_widget_add_css_class(icon, "source-icon");
    gtk_box_append(GTK_BOX(header), icon);

    GtkWidget *identity = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(identity, true);
    gtk_box_append(
        GTK_BOX(identity),
        make_label(source.name.c_str(), "source-name"));

    std::string type =
        std::string(source_kind_name(source.kind));
    if (!source.scope.empty()) {
        type += "  •  " + source.scope;
    }
    gtk_box_append(
        GTK_BOX(identity),
        make_label(type.c_str(), "source-meta"));
    gtk_box_append(GTK_BOX(header), identity);

    const bool mutable_source =
        source.kind == infiltrator::software::SourceKind::flatpak ||
        (source.kind == infiltrator::software::SourceKind::apt &&
         !source.backing_file.empty() &&
         source.entry_index != 0U);

    GtkWidget *source_state = nullptr;
    if (mutable_source && state != nullptr &&
        state->window != nullptr) {
        source_state =
            gtk_button_new_with_label(
                source.enabled ? "Enabled" : "Disabled");
        gtk_widget_add_css_class(
            source_state, "source-state-toggle");
        gtk_widget_add_css_class(
            source_state,
            source.enabled
                ? "state-installed"
                : "state-available");
        gtk_widget_set_tooltip_text(
            source_state,
            source.enabled
                ? "Click to disable this source"
                : "Click to enable this source");

        auto *context = new SourceToggleContext{
            state->window,
            source};
        g_object_set_data_full(
            G_OBJECT(source_state),
            "source-toggle-context",
            context,
            destroy_source_toggle_context);
        g_signal_connect(
            source_state,
            "clicked",
            G_CALLBACK(source_toggle_clicked),
            context);
    } else {
        source_state = make_label(
            source.enabled ? "Enabled" : "Disabled",
            source.enabled
                ? "state-installed"
                : "state-available");
        if (source.kind ==
            infiltrator::software::SourceKind::infiltrator) {
            gtk_widget_set_tooltip_text(
                source_state,
                "The built-in Infiltrator project catalogue is always enabled.");
        }
    }

    gtk_box_append(GTK_BOX(header), source_state);
    gtk_box_append(GTK_BOX(card), header);

    GtkWidget *location =
        make_label(source.location.c_str(), "source-location");
    gtk_label_set_wrap(GTK_LABEL(location), true);
    gtk_box_append(GTK_BOX(card), location);

    if (!source.detail.empty()) {
        GtkWidget *detail =
            make_label(source.detail.c_str(), "source-detail");
        gtk_label_set_wrap(GTK_LABEL(detail), true);
        gtk_box_append(GTK_BOX(card), detail);
    }

    if (!source.backing_file.empty()) {
        GtkWidget *file =
            make_label(source.backing_file.c_str(), "source-file");
        gtk_label_set_ellipsize(
            GTK_LABEL(file), PANGO_ELLIPSIZE_MIDDLE);
        gtk_box_append(GTK_BOX(card), file);
    }

    return card;
}

struct RepositoryResult {
    unsigned int generation{0U};
    std::vector<SourceRecord> sources;
    std::string error;
};

struct RepositoryTaskData {
    unsigned int generation{0U};
};

void repositories_worker(
    GTask *task,
    gpointer,
    gpointer task_data,
    GCancellable *)
{
    auto *data = static_cast<RepositoryTaskData *>(task_data);
    auto *result = new RepositoryResult{};
    result->generation = data == nullptr ? 0U : data->generation;

    SourceInventory inventory;
    result->sources = inventory.list(result->error);

    g_task_return_pointer(
        task,
        result,
        [](gpointer pointer) {
            delete static_cast<RepositoryResult *>(pointer);
        });
}

void repositories_complete(
    GObject *source_object,
    GAsyncResult *async_result,
    gpointer)
{
    auto *window = GTK_WINDOW(source_object);
    auto *state = static_cast<WindowState *>(
        g_object_get_data(
            G_OBJECT(window), "infiltrator-window-state"));
    auto *result = static_cast<RepositoryResult *>(
        g_task_propagate_pointer(
            G_TASK(async_result), nullptr));

    if (state == nullptr || result == nullptr) {
        delete result;
        return;
    }
    if (result->generation != state->repositories_generation) {
        delete result;
        return;
    }

    state->repositories_busy = false;

    GtkWidget *child =
        gtk_widget_get_first_child(state->repository_flow);
    while (child != nullptr) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_flow_box_remove(
            GTK_FLOW_BOX(state->repository_flow), child);
        child = next;
    }

    std::size_t enabled = 0U;
    for (const SourceRecord &source : result->sources) {
        gtk_flow_box_append(
            GTK_FLOW_BOX(state->repository_flow),
            make_source_card(state, source));
        if (source.enabled) {
            ++enabled;
        }
    }

    if (state->repository_count != nullptr) {
        const std::string count =
            std::to_string(result->sources.size());
        gtk_label_set_text(
            GTK_LABEL(state->repository_count), count.c_str());
    }

    if (state->repository_status != nullptr) {
        if (!result->error.empty()) {
            gtk_label_set_text(
                GTK_LABEL(state->repository_status),
                result->error.c_str());
        } else {
            std::ostringstream status;
            status << enabled << " enabled source"
                   << (enabled == 1U ? "" : "s")
                   << " detected. APT sources and Flatpak remotes feed Discover.";
            gtk_label_set_text(
                GTK_LABEL(state->repository_status),
                status.str().c_str());
        }
    }

    delete result;
}

void refresh_repositories(WindowState *state)
{
    if (state == nullptr || state->repository_flow == nullptr ||
        state->window == nullptr || state->repositories_busy) {
        return;
    }

    state->repositories_loaded = true;
    state->repositories_busy = true;
    ++state->repositories_generation;

    if (state->repository_status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->repository_status),
            "Reading configured software sources…");
    }

    auto *data = new RepositoryTaskData{
        state->repositories_generation};
    GTask *task = g_task_new(
        G_OBJECT(state->window),
        nullptr,
        repositories_complete,
        nullptr);
    g_task_set_task_data(
        task,
        data,
        [](gpointer pointer) {
            delete static_cast<RepositoryTaskData *>(pointer);
        });
    g_task_run_in_thread(task, repositories_worker);
    g_object_unref(task);
}

GtkWidget *make_repositories_page(WindowState *state)
{
    GtkWidget *page =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(page, "content");
    gtk_widget_add_css_class(page, "page-repositories");

    GtkWidget *header =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *intro =
        make_page_intro(
            "network-workgroup-symbolic",
            "Repositories",
            "Software sources feeding Discover.");
    gtk_widget_set_hexpand(intro, true);
    gtk_box_append(GTK_BOX(header), intro);

    GtkWidget *add_source =
        gtk_button_new_with_label("Add Source…");
    gtk_widget_add_css_class(add_source, "suggested-action");
    gtk_widget_set_valign(add_source, GTK_ALIGN_CENTER);
    g_signal_connect(
        add_source,
        "clicked",
        G_CALLBACK(add_source_clicked),
        state);
    gtk_box_append(GTK_BOX(header), add_source);
    gtk_box_append(GTK_BOX(page), header);

    GtkWidget *stats = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(stats), 10);
    gtk_grid_set_column_homogeneous(GTK_GRID(stats), true);
    gtk_grid_attach(
        GTK_GRID(stats),
        make_stat_card(
            "SOURCES", "0", "stat-info",
            &state->repository_count),
        0, 0, 1, 1);
    gtk_grid_attach(
        GTK_GRID(stats),
        make_stat_card(
            "APT", "System sources", "stat-operation"),
        1, 0, 1, 1);
    gtk_grid_attach(
        GTK_GRID(stats),
        make_stat_card(
            "FLATPAK", "User + system", "stat-success"),
        2, 0, 1, 1);
    gtk_box_append(GTK_BOX(page), stats);

    state->repository_status = make_label(
        "Reading configured software sources…",
        "discover-status");
    gtk_box_append(
        GTK_BOX(page), state->repository_status);

    state->repository_flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(
        GTK_FLOW_BOX(state->repository_flow),
        GTK_SELECTION_NONE);
    gtk_flow_box_set_row_spacing(
        GTK_FLOW_BOX(state->repository_flow), 10U);
    gtk_flow_box_set_column_spacing(
        GTK_FLOW_BOX(state->repository_flow), 10U);
    gtk_flow_box_set_min_children_per_line(
        GTK_FLOW_BOX(state->repository_flow), 1U);
    gtk_flow_box_set_max_children_per_line(
        GTK_FLOW_BOX(state->repository_flow), 2U);
    gtk_widget_set_valign(
        state->repository_flow, GTK_ALIGN_START);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, true);
    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER,
        GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(
        GTK_SCROLLED_WINDOW(scroll),
        state->repository_flow);
    gtk_box_append(GTK_BOX(page), scroll);

    return page;
}


struct HistoryResult {
    unsigned int generation{0U};
    std::vector<TransactionHistoryItem> records;
    std::string error;
};

struct HistoryTaskData {
    unsigned int generation{0U};
};

std::string history_timestamp(const std::int64_t unix_time)
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
        card,
        head.success ? "card-info" : "card-warning");

    GtkWidget *header =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);

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
        GtkWidget *package_label =
            make_label(package_title.c_str(), "source-name");
        gtk_box_append(GTK_BOX(identity), package_label);

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
        GtkWidget *versions_label =
            make_label(versions.c_str(), "card-copy");
        gtk_box_append(GTK_BOX(identity), versions_label);

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
                    infiltrator::software::transaction_action_name(
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

void rebuild_history(WindowState *state)
{
    if (state == nullptr || state->history_list == nullptr) {
        return;
    }

    GtkWidget *child =
        gtk_widget_get_first_child(
            GTK_WIDGET(state->history_list));
    while (child != nullptr) {
        GtkWidget *next =
            gtk_widget_get_next_sibling(child);
        gtk_list_box_remove(state->history_list, child);
        child = next;
    }

    std::size_t transaction_count = 0U;
    std::size_t index = 0U;
    while (index < state->history_records.size()) {
        const std::int64_t transaction_id =
            state->history_records[index].transaction_id;
        std::size_t end = index + 1U;
        while (end < state->history_records.size() &&
               state->history_records[end].transaction_id ==
                   transaction_id) {
            ++end;
        }

        GtkWidget *row = gtk_list_box_row_new();
        gtk_list_box_row_set_child(
            GTK_LIST_BOX_ROW(row),
            make_history_transaction_card(
                state->history_records, index, end));
        gtk_list_box_append(state->history_list, row);

        ++transaction_count;
        index = end;
    }

    if (state->history_count != nullptr) {
        const std::string count =
            std::to_string(transaction_count);
        gtk_label_set_text(
            GTK_LABEL(state->history_count),
            count.c_str());
    }
}

void history_worker(
    GTask *task,
    gpointer,
    gpointer task_data,
    GCancellable *)
{
    auto *data =
        static_cast<HistoryTaskData *>(task_data);
    auto *result = new HistoryResult{};
    result->generation =
        data == nullptr ? 0U : data->generation;

    const std::filesystem::path path =
        transaction_history_path();
    if (path.empty()) {
        result->error =
            "The user data directory is unavailable.";
    } else {
        TransactionHistoryStore store(path.string());
        result->records =
            store.load_recent(100U, result->error);
    }

    g_task_return_pointer(
        task,
        result,
        [](gpointer pointer) {
            delete static_cast<HistoryResult *>(pointer);
        });
}

void history_complete(
    GObject *source_object,
    GAsyncResult *async_result,
    gpointer)
{
    auto *window = GTK_WINDOW(source_object);
    auto *state = static_cast<WindowState *>(
        g_object_get_data(
            G_OBJECT(window), "infiltrator-window-state"));
    auto *result = static_cast<HistoryResult *>(
        g_task_propagate_pointer(
            G_TASK(async_result), nullptr));

    if (state == nullptr || result == nullptr) {
        delete result;
        return;
    }
    if (result->generation != state->history_generation) {
        delete result;
        return;
    }

    state->history_busy = false;
    state->history_records =
        std::move(result->records);
    const std::string error = result->error;
    delete result;

    rebuild_history(state);

    if (state->history_status != nullptr) {
        if (!error.empty()) {
            const std::string message =
                "Unable to read transaction history: " +
                one_line(error);
            gtk_label_set_text(
                GTK_LABEL(state->history_status),
                message.c_str());
        } else if (state->history_records.empty()) {
            gtk_label_set_text(
                GTK_LABEL(state->history_status),
                "No completed software transactions have been recorded yet.");
        } else {
            std::unordered_set<std::int64_t> transactions;
            for (const TransactionHistoryItem &entry :
                 state->history_records) {
                transactions.insert(entry.transaction_id);
            }
            const std::string message =
                std::to_string(transactions.size()) +
                (transactions.size() == 1U
                     ? " recent transaction loaded."
                     : " recent transactions loaded.");
            gtk_label_set_text(
                GTK_LABEL(state->history_status),
                message.c_str());
        }
    }

    if (state->history_refresh != nullptr) {
        gtk_widget_set_sensitive(
            state->history_refresh, true);
    }
}

void refresh_history(WindowState *state)
{
    if (state == nullptr || state->window == nullptr ||
        state->history_list == nullptr || state->history_busy) {
        return;
    }

    state->history_loaded = true;
    state->history_busy = true;
    ++state->history_generation;

    if (state->history_status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->history_status),
            "Reading durable transaction history…");
    }
    if (state->history_refresh != nullptr) {
        gtk_widget_set_sensitive(
            state->history_refresh, false);
    }

    auto *data = new HistoryTaskData{
        state->history_generation};
    GTask *task = g_task_new(
        G_OBJECT(state->window),
        nullptr,
        history_complete,
        nullptr);
    g_task_set_task_data(
        task,
        data,
        [](gpointer pointer) {
            delete static_cast<HistoryTaskData *>(pointer);
        });
    g_task_run_in_thread(task, history_worker);
    g_object_unref(task);
}

void history_refresh_clicked(
    GtkButton *,
    gpointer user_data)
{
    refresh_history(
        static_cast<WindowState *>(user_data));
}

GtkWidget *make_history_page(WindowState *state)
{
    GtkWidget *page =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(page, "content");
    gtk_widget_add_css_class(page, "page-history");

    gtk_box_append(
        GTK_BOX(page),
        make_page_intro(
            "document-open-recent-symbolic",
            "History",
            "Exact software-management operations and outcomes."));

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
            &state->history_count),
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

    state->history_status =
        make_label(
            "Transaction history has not been loaded yet.",
            "card-copy");
    gtk_label_set_wrap(
        GTK_LABEL(state->history_status), true);
    gtk_widget_set_hexpand(
        state->history_status, true);
    gtk_box_append(
        GTK_BOX(controls), state->history_status);

    state->history_refresh =
        gtk_button_new_with_label("Refresh history");
    gtk_widget_add_css_class(
        state->history_refresh, "control-button");
    g_signal_connect(
        state->history_refresh,
        "clicked",
        G_CALLBACK(history_refresh_clicked),
        state);
    gtk_box_append(
        GTK_BOX(controls), state->history_refresh);

    gtk_box_append(GTK_BOX(page), controls);

    GtkWidget *list = gtk_list_box_new();
    state->history_list = GTK_LIST_BOX(list);
    gtk_widget_add_css_class(list, "package-list");
    gtk_list_box_set_selection_mode(
        state->history_list,
        GTK_SELECTION_NONE);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, true);
    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER,
        GTK_POLICY_ALWAYS);
    gtk_scrolled_window_set_overlay_scrolling(
        GTK_SCROLLED_WINDOW(scroll), false);
    gtk_scrolled_window_set_child(
        GTK_SCROLLED_WINDOW(scroll), list);
    gtk_box_append(GTK_BOX(page), scroll);

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

gboolean periodic_updates_refresh(gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    if (state == nullptr || state->stack == nullptr ||
        !state->updates_loaded || state->updates_busy) {
        return G_SOURCE_CONTINUE;
    }

    const char *visible =
        gtk_stack_get_visible_child_name(state->stack);
    if (visible == nullptr || std::strcmp(visible, "updates") != 0) {
        return G_SOURCE_CONTINUE;
    }

    if (update_metadata_refresh_due(
            g_get_monotonic_time(),
            state->updates_last_metadata_refresh_us,
            state->updates_busy)) {
        refresh_updates(state, true);
    }
    return G_SOURCE_CONTINUE;
}

void refresh_page_if_needed(WindowState *state, const int index)
{
    if (state == nullptr || !state->window_presented) {
        return;
    }

    switch (index) {
    case 0:
        if (!state->discover_loaded) {
            refresh_discover(state);
        }
        break;
    case 1:
        if (!state->installed_loaded) {
            refresh_installed(state);
        }
        break;
    case 2:
        if (!state->updates_loaded) {
            refresh_updates(state);
        } else if (update_metadata_refresh_due(
                       g_get_monotonic_time(),
                       state->updates_last_metadata_refresh_us,
                       state->updates_busy)) {
            refresh_updates(state, true);
        }
        break;
    case 3:
        if (!state->system_busy) {
            refresh_system(state, false);
        }
        break;
    case 4:
        if (!state->repositories_loaded) {
            refresh_repositories(state);
        }
        break;
    case 5:
        if (!state->history_loaded) {
            refresh_history(state);
        }
        break;
    default:
        break;
    }
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

    /*
     * Let GTK commit and paint the new page before starting its lazy refresh.
     * The refresh itself is asynchronous, but deferring its launch until the
     * main loop is idle guarantees that a page change cannot be visually held
     * in an old/new mixed state by task setup or backend startup.
     *
     * Hold the window rather than the WindowState across the idle boundary;
     * WindowState is window-owned and may disappear if the user closes the
     * application before this callback runs.
     */
    g_idle_add_full(
        G_PRIORITY_DEFAULT_IDLE,
        [](gpointer data) -> gboolean {
            auto *window = GTK_WINDOW(data);
            auto *idle_state =
                static_cast<WindowState *>(
                    g_object_get_data(
                        G_OBJECT(window),
                        "infiltrator-window-state"));
            if (idle_state == nullptr ||
                idle_state->navigation_list == nullptr) {
                return G_SOURCE_REMOVE;
            }

            GtkListBoxRow *selected =
                gtk_list_box_get_selected_row(
                    idle_state->navigation_list);
            if (selected != nullptr) {
                refresh_page_if_needed(
                    idle_state,
                    gtk_list_box_row_get_index(selected));
            }
            return G_SOURCE_REMOVE;
        },
        g_object_ref(state->window),
        [](gpointer data) {
            g_object_unref(data);
        });
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
    const std::string common_version =
        std::string("Common ") + INFILTRATR_COMMON_VERSION;
    GtkWidget *common =
        make_label(common_version.c_str(), "sidebar-note");
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
    if (state == nullptr || state->stack == nullptr) {
        return;
    }

    const char *page =
        gtk_stack_get_visible_child_name(state->stack);
    if (page == nullptr) {
        return;
    }

    if (std::strcmp(page, "discover") == 0) {
        refresh_discover(state, true);
    } else if (std::strcmp(page, "installed") == 0) {
        refresh_installed(state);
    } else if (std::strcmp(page, "updates") == 0) {
        refresh_updates(state);
    } else if (std::strcmp(page, "repositories") == 0) {
        refresh_repositories(state);
    }
}

void about_clicked(GtkButton *, gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    if (state == nullptr || state->window == nullptr) {
        return;
    }

    GtkWidget *dialog = gtk_about_dialog_new();
    const char *profile = INFILTRATOR_SOFTWARE_BUILD_PROFILE;
    char comments[512];
    std::snprintf(
        comments, sizeof(comments),
        "Unified software management and updates for the Infiltrator project family.\n\nBuild: %s",
        infiltratr_build_profile_label(profile));

    gtk_about_dialog_set_program_name(
        GTK_ABOUT_DIALOG(dialog), "Infiltrator Software");
    gtk_about_dialog_set_logo_icon_name(
        GTK_ABOUT_DIALOG(dialog), "net.ssmith.infiltrator.software");
    gtk_window_set_icon_name(GTK_WINDOW(dialog), "net.ssmith.infiltrator.software");
    gtk_about_dialog_set_version(
        GTK_ABOUT_DIALOG(dialog), INFILTRATOR_SOFTWARE_VERSION);
    gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(dialog), comments);
    gtk_about_dialog_set_website(
        GTK_ABOUT_DIALOG(dialog),
        "https://github.com/Infiltrator-Projects/Software");
    gtk_about_dialog_set_website_label(GTK_ABOUT_DIALOG(dialog), "Website");
    gtk_about_dialog_set_copyright(
        GTK_ABOUT_DIALOG(dialog), "Copyright © 2026 Shannon Smith");
    gtk_about_dialog_set_license_type(
        GTK_ABOUT_DIALOG(dialog), GTK_LICENSE_CUSTOM);
    gtk_about_dialog_set_license(
        GTK_ABOUT_DIALOG(dialog),
        "Infiltrator Software is free software licensed under the GNU General "
        "Public License version 3 or, at your option, any later version "
        "(GPL-3.0-or-later).\n\n"
        "See LICENSE in the source package for the complete licence text.");
    gtk_about_dialog_set_wrap_license(GTK_ABOUT_DIALOG(dialog), true);

    static const char *authors[] = {
        "Shannon Smith — Author and project maintainer",
        nullptr
    };
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
    if (state->discover_visible != nullptr) {
        g_object_unref(state->discover_visible);
        state->discover_visible = nullptr;
    }
    if (state->updates_refresh_timer_id != 0U) {
        g_source_remove(state->updates_refresh_timer_id);
        state->updates_refresh_timer_id = 0U;
    }
    if (state->updates_progress_timer_id != 0U) {
        g_source_remove(state->updates_progress_timer_id);
        state->updates_progress_timer_id = 0U;
    }
    delete state;
}

void ensure_update_indicator()
{
    gchar *program = g_find_program_in_path("infiltrator-software-tray");
    if (program == nullptr) {
        return;
    }
    g_free(program);

    GError *error = nullptr;
    if (!g_spawn_command_line_async(
            "infiltrator-software-tray", &error)) {
        if (error != nullptr) {
            g_warning(
                "Unable to start software update indicator: %s",
                error->message);
            g_error_free(error);
        }
    }
}

void select_page(WindowState *state, const int index)
{
    if (state == nullptr || state->navigation_list == nullptr ||
        index < 0 || index >= 7) {
        return;
    }

    GtkListBoxRow *row =
        gtk_list_box_get_row_at_index(
            state->navigation_list, index);
    if (row != nullptr) {
        gtk_list_box_select_row(
            state->navigation_list, row);
    }
}

void activate(GtkApplication *application, gpointer)
{
    const bool open_updates =
        g_object_get_data(
            G_OBJECT(application),
            "infiltrator-open-updates") != nullptr;

    GList *windows =
        gtk_application_get_windows(application);
    GtkWindow *existing =
        windows == nullptr
            ? nullptr
            : GTK_WINDOW(windows->data);
    if (existing != nullptr) {
        auto *state =
            static_cast<WindowState *>(
                g_object_get_data(
                    G_OBJECT(existing),
                    "infiltrator-window-state"));
        if (open_updates && state != nullptr) {
            select_page(state, 2);
        }
        gtk_window_present(existing);
        g_object_set_data(
            G_OBJECT(application),
            "infiltrator-open-updates",
            nullptr);
        return;
    }

    GtkWidget *window = gtk_application_window_new(application);
    gtk_window_set_title(GTK_WINDOW(window), "Infiltrator Software");
    gtk_window_set_icon_name(GTK_WINDOW(window), "net.ssmith.infiltrator.software");
    gtk_window_set_default_size(GTK_WINDOW(window), 1220, 780);
    gtk_widget_set_size_request(window, 940, 620);

    auto *state = new WindowState{};
    state->window = GTK_WINDOW(window);
    state->theme.initialise();
    ensure_update_indicator();

    g_object_set_data_full(
        G_OBJECT(window), "infiltrator-window-state",
        state, destroy_window_state);

    /*
     * Keep the Updates page current while it remains open.  This timer does
     * not block the UI and only starts an unprivileged metadata refresh when
     * the last successful refresh is stale.
     */
    state->updates_refresh_timer_id =
        g_timeout_add_seconds(
            60U, periodic_updates_refresh, state);

    GtkWidget *header = make_header_bar(state);
    gtk_window_set_titlebar(GTK_WINDOW(window), header);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(window), root);

    GtkWidget *body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_vexpand(body, true);
    gtk_box_append(GTK_BOX(root), body);

    GtkWidget *stack = gtk_stack_new();
    state->stack = GTK_STACK(stack);
    /*
     * Navigation is a state change, not a long-running visual operation.
     * A crossfade keeps both pages mapped while the destination page starts
     * its lazy catalogue work; if that work delays a frame, GTK can leave a
     * half-faded source page visible for seconds.  Switch pages atomically so
     * the user always sees exactly one page.
     */
    gtk_stack_set_transition_type(
        state->stack, GTK_STACK_TRANSITION_TYPE_NONE);
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
        make_updates_page(state),
        "updates");
    gtk_stack_add_named(
        state->stack,
        make_system_page(state),
        "system");
    gtk_stack_add_named(
        state->stack,
        make_repositories_page(state),
        "repositories");
    gtk_stack_add_named(
        state->stack,
        make_history_page(state),
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

    const int initial_page = open_updates ? 2 : 0;
    gtk_stack_set_visible_child_name(
        state->stack,
        initial_page == 2 ? "updates" : "discover");
    select_page(state, initial_page);
    gtk_box_append(GTK_BOX(root), make_status_bar());

    gtk_window_present(GTK_WINDOW(window));
    state->window_presented = true;

    g_idle_add_full(
        G_PRIORITY_DEFAULT_IDLE,
        [](gpointer data) -> gboolean {
            auto *window = GTK_WINDOW(data);
            auto *state =
                static_cast<WindowState *>(
                    g_object_get_data(
                        G_OBJECT(window),
                        "infiltrator-window-state"));
            if (state != nullptr && state->navigation_list != nullptr) {
                GtkListBoxRow *selected =
                    gtk_list_box_get_selected_row(
                        state->navigation_list);
                if (selected != nullptr) {
                    refresh_page_if_needed(
                        state,
                        gtk_list_box_row_get_index(selected));
                }
            }
            return G_SOURCE_REMOVE;
        },
        g_object_ref(window),
        [](gpointer data) {
            g_object_unref(data);
        });

    g_object_set_data(
        G_OBJECT(application),
        "infiltrator-open-updates",
        nullptr);
}

int command_line(
    GApplication *application,
    GApplicationCommandLine *command_line,
    gpointer)
{
    GVariantDict *options =
        g_application_command_line_get_options_dict(command_line);
    const bool open_updates =
        options != nullptr &&
        g_variant_dict_contains(options, "updates");

    g_object_set_data(
        G_OBJECT(application),
        "infiltrator-open-updates",
        open_updates ? GINT_TO_POINTER(1) : nullptr);
    g_application_activate(application);
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    GtkApplication *application = gtk_application_new(
        "net.ssmith.infiltrator.software",
        G_APPLICATION_HANDLES_COMMAND_LINE);

    static const GOptionEntry options[] = {
        {
            "updates",
            0,
            0,
            G_OPTION_ARG_NONE,
            nullptr,
            "Open the Updates page",
            nullptr
        },
        {nullptr, 0, 0, G_OPTION_ARG_NONE, nullptr, nullptr, nullptr}
    };
    g_application_add_main_option_entries(
        G_APPLICATION(application),
        options);

    g_signal_connect(
        application,
        "activate",
        G_CALLBACK(activate),
        nullptr);
    g_signal_connect(
        application,
        "command-line",
        G_CALLBACK(command_line),
        nullptr);

    const int status =
        g_application_run(
            G_APPLICATION(application),
            argc,
            argv);
    g_object_unref(application);
    return status;
}
