// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/discover_view.hpp"
#include "app/app_coordinator.hpp"
#include "app/transaction_review.hpp"
#include "app/history_controller.hpp"
#include "app/history_view.hpp"
#include "app/installed_controller.hpp"
#include "app/installed_inventory.hpp"
#include "app/repository_controller.hpp"
#include "app/repository_view.hpp"
#include "app/ui_components.hpp"
#include "app/updates_controller.hpp"
#include "app/window_state.hpp"
#include "catalogue/repository_catalogue.hpp"
#include "catalogue/catalogue_snapshot_store.hpp"
#include "catalogue/system_catalogue.hpp"
#include "client/engine_client.hpp"
#include "core/model.hpp"
#include "core/exact_transaction_spec.hpp"
#include "core/transaction_history.hpp"
#include "core/update_freshness.hpp"
#include "core/update_policy.hpp"
#include "external/external_updates.hpp"
#include "sources/source_inventory.hpp"

#include <gtk/gtk.h>
#include <curl/curl.h>
#include <infiltratr/core.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <unistd.h>
#include <vector>

#ifndef INFILTRATOR_SOFTWARE_VERSION
#define INFILTRATOR_SOFTWARE_VERSION "0.0.0"
#endif

namespace infiltrator::software::app {

using infiltrator::software::CatalogueSnapshot;
using infiltrator::software::CatalogueSnapshotStore;
using infiltrator::software::EngineClient;
using infiltrator::software::PackageRecord;
using infiltrator::software::KernelRecord;
using infiltrator::software::RepositoryCatalogue;
using infiltrator::software::SourceInventory;
using infiltrator::software::SourceKind;
using infiltrator::software::SourceRecord;
using infiltrator::software::SystemCatalogue;
using infiltrator::software::TransactionAction;
using infiltrator::software::TransactionHistoryItem;
using infiltrator::software::TransactionHistoryStore;
using infiltrator::software::TransactionPlan;
using infiltrator::software::TransactionItem;
using infiltrator::software::SoftwarePreferences;
using infiltrator::software::load_software_preferences;
using infiltrator::software::save_software_preferences;
using infiltrator::software::update_is_ignored;
using infiltrator::software::reboot_required;
using infiltrator::software::reboot_required_packages;
using infiltrator::software::ExternalUpdate;
using infiltrator::software::ExternalUpdateKind;
using infiltrator::software::external_update_kind_name;
using infiltrator::software::discover_flatpak_updates;
using infiltrator::software::discover_cinnamon_updates;
using infiltrator::software::apply_flatpak_updates;
using infiltrator::software::set_flatpak_application_installed;
using infiltrator::software::apply_cinnamon_updates;
using infiltrator::software::apply_cinnamon_updates_selected;
using infiltrator::software::source_kind_name;
using infiltrator::software::update_metadata_refresh_due;
using infiltrator::software::HistoryController;
using infiltrator::software::create_history_controller_page;
using infiltrator::software::history_timestamp;
using infiltrator::software::refresh_history_controller;
using infiltrator::software::read_installed_packages;
using infiltrator::software::InstalledController;
using infiltrator::software::create_installed_page;
using infiltrator::software::refresh_installed_controller;
using infiltrator::software::RepositoryController;
using infiltrator::software::configure_repository_controller;
using infiltrator::software::refresh_repository_controller;
using infiltrator::software::make_icon;
using infiltrator::software::make_label;
using infiltrator::software::make_page_intro;
using infiltrator::software::make_stat_card;

void refresh_updates(WindowState *state, bool refresh_metadata = false);
void refresh_updates_internal(
    WindowState *state,
    bool refresh_metadata,
    bool refresh_external);
void refresh_discover(WindowState *state, bool force_refresh);
void rebuild_updates(WindowState *state);
std::string one_line(std::string value);
const char *update_icon_name(const PackageRecord &package);
void discover_install_clicked(GtkButton *button, gpointer user_data);

void record_transaction_history(
    const TransactionPlan &plan,
    bool success,
    std::string_view message);

static constexpr const char kDiscoverHeroBase64[] =
#include "discover_hero_part01.inc"
#include "discover_hero_part02.inc"
#include "discover_hero_part03.inc"
#include "discover_hero_part04.inc"
#include "discover_hero_part05.inc"
#include "discover_hero_part06.inc"
    ;


GtkWidget *make_embedded_picture(
    const char *base64,
    const char *css_class,
    const GtkContentFit content_fit)
{
    if (base64 == nullptr || *base64 == '\0') {
        return nullptr;
    }

    gsize image_size = 0;
    guchar *image_data =
        g_base64_decode(base64, &image_size);
    if (image_data == nullptr || image_size == 0U) {
        g_free(image_data);
        return nullptr;
    }

    GBytes *image_bytes =
        g_bytes_new_take(image_data, image_size);
    GError *texture_error = nullptr;
    GdkTexture *texture =
        gdk_texture_new_from_bytes(
            image_bytes, &texture_error);
    g_bytes_unref(image_bytes);

    if (texture == nullptr) {
        g_warning(
            "Embedded raster asset failed to decode: %s",
            texture_error != nullptr
                ? texture_error->message
                : "unknown image decoder error");
        g_clear_error(&texture_error);
        return nullptr;
    }

    GtkWidget *picture =
        gtk_picture_new_for_paintable(
            GDK_PAINTABLE(texture));
    g_object_unref(texture);
    gtk_picture_set_content_fit(
        GTK_PICTURE(picture), content_fit);
    gtk_picture_set_can_shrink(
        GTK_PICTURE(picture), true);
    if (css_class != nullptr) {
        gtk_widget_add_css_class(
            picture, css_class);
    }
    return picture;
}

struct DiscoverResult {
    CatalogueSnapshot snapshot;
    std::vector<std::string> search_texts;
    std::vector<std::string> categories;
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

std::string discover_search_text(const PackageRecord &record)
{
    /*
     * Discover is a software search, not just a title search.  Index every
     * user-facing identity/description field we already have so queries such
     * as "theme" can match package summaries, publishers, source package
     * names and catalogue identifiers as well as the visible application
     * name.  Keep the folded representation cached by the caller.
     */
    return folded(
        record.name + "\n" +
        record.summary + "\n" +
        record.description + "\n" +
        record.publisher + "\n" +
        record.category + "\n" +
        record.id + "\n" +
        record.package_name + "\n" +
        record.source_package + "\n" +
        record.source);
}

void prepare_discover_presentation(DiscoverResult *result)
{
    if (result == nullptr) {
        return;
    }

    result->search_texts.clear();
    result->search_texts.reserve(result->snapshot.records.size());
    std::set<std::string> categories;
    for (const PackageRecord &record : result->snapshot.records) {
        result->search_texts.emplace_back(discover_search_text(record));
        if (!record.category.empty()) {
            categories.insert(record.category);
        }
    }
    result->categories.assign(categories.begin(), categories.end());
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
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(card, "discover-card");
    gtk_widget_set_size_request(card, 310, 246);

    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 13);

    GtkWidget *icon_well =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(icon_well, "discover-icon-well");
    gtk_widget_set_size_request(icon_well, 74, 74);
    GtkWidget *app_icon = catalogue_icon(record, 58);
    gtk_widget_set_halign(app_icon, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(app_icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(icon_well), app_icon);
    gtk_box_append(GTK_BOX(header), icon_well);

    GtkWidget *identity = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_set_hexpand(identity, true);
    GtkWidget *name = make_label(record.name.c_str(), "discover-name");
    gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
    std::string meta_text = record.category;
    if (!record.available_version.empty()) {
        meta_text += "  •  " + record.available_version;
    }
    GtkWidget *meta = make_label(
        meta_text.c_str(), "discover-meta");
    gtk_label_set_ellipsize(GTK_LABEL(meta), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(identity), name);
    gtk_box_append(GTK_BOX(identity), meta);

    if (!record.source.empty()) {
        GtkWidget *source =
            make_label(record.source.c_str(), "discover-source-chip");
        gtk_label_set_ellipsize(
            GTK_LABEL(source), PANGO_ELLIPSIZE_END);
        gtk_box_append(GTK_BOX(identity), source);
    }

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
    } else if (
        record.state ==
        infiltrator::software::InstallState::upgradable) {
        state_label =
            make_label("Update available", "state-available");
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

    if (!record.package_name.empty()) {
        const bool installed =
            record.state ==
            infiltrator::software::InstallState::installed;
        const bool upgradable =
            record.state ==
            infiltrator::software::InstallState::upgradable;
        GtkWidget *action = gtk_button_new_with_label(
            installed ? "Remove" : (upgradable ? "Update" : "Install"));
        gtk_widget_add_css_class(
            action,
            installed ? "destructive-action" : "suggested-action");
        g_object_set_data_full(
            G_OBJECT(action),
            "discover-install-record",
            new PackageRecord(record),
            package_record_destroy);
        g_signal_connect(
            action, "clicked",
            G_CALLBACK(discover_install_clicked), state);
        gtk_box_append(GTK_BOX(footer), action);
    }

    gtk_box_append(GTK_BOX(card), footer);
    return card;
}


GtkWidget *make_featured_card(
    WindowState *state,
    const PackageRecord &record)
{
    GtkWidget *card =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(
        card, "featured-card");
    /*
     * This is a minimum only.  FlowBox expands four homogeneous cards across
     * the two-thirds Featured column on wide windows, while the small minimum
     * prevents display scaling from forcing the dashboard wider than the
     * available content area.
     */
    gtk_widget_set_size_request(
        card, 112, 218);

    GtkWidget *art =
        gtk_overlay_new();
    gtk_widget_add_css_class(
        art, "featured-card-art");
    gtk_widget_set_size_request(
        art, -1, 82);

    GtkWidget *art_picture =
        make_embedded_picture(
            kDiscoverHeroBase64,
            "featured-card-raster",
            GTK_CONTENT_FIT_COVER);
    if (art_picture != nullptr) {
        gtk_widget_set_hexpand(
            art_picture, true);
        gtk_widget_set_vexpand(
            art_picture, true);
        gtk_overlay_set_child(
            GTK_OVERLAY(art),
            art_picture);
    }

    GtkWidget *shade =
        gtk_box_new(
            GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(
        shade, "featured-card-art-shade");
    gtk_widget_set_hexpand(
        shade, true);
    gtk_widget_set_vexpand(
        shade, true);
    gtk_widget_set_can_target(
        shade, false);
    gtk_overlay_add_overlay(
        GTK_OVERLAY(art), shade);

    GtkWidget *icon =
        catalogue_icon(record, 58);
    gtk_widget_set_halign(
        icon, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(
        icon, GTK_ALIGN_CENTER);
    gtk_widget_set_can_target(
        icon, false);
    gtk_overlay_add_overlay(
        GTK_OVERLAY(art), icon);
    gtk_box_append(GTK_BOX(card), art);

    GtkWidget *name =
        make_label(
            record.name.c_str(),
            "featured-card-name");
    gtk_label_set_ellipsize(
        GTK_LABEL(name),
        PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(card), name);

    const std::string category =
        record.category.empty()
            ? "Application"
            : record.category;
    GtkWidget *meta =
        make_label(
            category.c_str(),
            "featured-card-meta");
    gtk_label_set_ellipsize(
        GTK_LABEL(meta),
        PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(card), meta);

    GtkWidget *description =
        make_label(
            record.description.c_str(),
            "featured-card-copy");
    gtk_label_set_wrap(
        GTK_LABEL(description), true);
    gtk_label_set_lines(
        GTK_LABEL(description), 2);
    gtk_label_set_ellipsize(
        GTK_LABEL(description),
        PANGO_ELLIPSIZE_END);
    gtk_widget_set_vexpand(
        description, true);
    gtk_box_append(
        GTK_BOX(card), description);

    if (!record.package_name.empty()) {
        const bool installed =
            record.state ==
            infiltrator::software::InstallState::installed;
        const bool upgradable =
            record.state ==
            infiltrator::software::InstallState::upgradable;
        GtkWidget *action =
            gtk_button_new_with_label(
                installed
                    ? "Remove"
                    : (upgradable
                           ? "Update"
                           : "Install"));
        gtk_widget_add_css_class(
            action,
            installed
                ? "destructive-action"
                : "suggested-action");
        gtk_widget_add_css_class(
            action,
            "featured-primary");
        gtk_widget_set_hexpand(
            action, true);
        g_object_set_data_full(
            G_OBJECT(action),
            "discover-install-record",
            new PackageRecord(record),
            package_record_destroy);
        g_signal_connect(
            action, "clicked",
            G_CALLBACK(discover_install_clicked),
            state);
        gtk_box_append(
            GTK_BOX(card), action);
    }

    return card;
}

std::vector<PackageRecord> featured_fallback_records()
{
    std::vector<PackageRecord> records;
    records.reserve(4U);

    auto add = [&records](
        const char *id,
        const char *name,
        const char *package_name,
        const char *category,
        const char *description,
        const char *icon_name) {
        PackageRecord record;
        record.id = id;
        record.name = name;
        record.package_name = package_name;
        record.publisher = "Distribution";
        record.category = category;
        record.description = description;
        record.summary = description;
        record.icon_name = icon_name;
        record.source = "System repository";
        record.kind =
            infiltrator::software::PackageKind::application;
        record.state =
            infiltrator::software::InstallState::not_installed;
        records.emplace_back(std::move(record));
    };

    add(
        "featured:firefox",
        "Firefox",
        "firefox",
        "Internet",
        "Fast, private and open web browsing.",
        "firefox");
    add(
        "featured:libreoffice",
        "LibreOffice",
        "libreoffice",
        "Office",
        "A complete office productivity suite.",
        "libreoffice-startcenter");
    add(
        "featured:gimp",
        "GIMP",
        "gimp",
        "Graphics",
        "Create and edit images with powerful tools.",
        "gimp");
    add(
        "featured:vlc",
        "VLC",
        "vlc",
        "Sound & Video",
        "Play video and audio in almost any format.",
        "vlc");

    return records;
}

const PackageRecord *find_featured_record(
    const WindowState *state,
    const PackageRecord &fallback,
    const std::vector<std::size_t> &visible_indices)
{
    if (state == nullptr) {
        return nullptr;
    }

    const std::string wanted_package =
        folded(fallback.package_name);
    const std::string wanted_name =
        folded(fallback.name);

    for (const std::size_t index : visible_indices) {
        if (index >= state->discover.discover_records.size()) {
            continue;
        }
        const PackageRecord &record =
            state->discover.discover_records[index];
        if (folded(record.package_name) == wanted_package ||
            folded(record.name) == wanted_name) {
            return &record;
        }
    }
    return nullptr;
}

void append_curated_featured(
    WindowState *state,
    const std::vector<std::size_t> *visible_indices = nullptr)
{
    if (state == nullptr ||
        state->discover.discover_featured_flow == nullptr) {
        return;
    }

    const std::vector<PackageRecord> fallback =
        featured_fallback_records();
    static const std::vector<std::size_t> empty_indices;

    const std::vector<std::size_t> &indices =
        visible_indices != nullptr
            ? *visible_indices
            : empty_indices;

    for (const PackageRecord &placeholder : fallback) {
        const PackageRecord *record =
            find_featured_record(
                state, placeholder, indices);
        gtk_flow_box_append(
            GTK_FLOW_BOX(
                state->discover.discover_featured_flow),
            make_featured_card(
                state,
                record != nullptr
                    ? *record
                    : placeholder));
    }
}

void clear_box_children(GtkWidget *box)
{
    if (box == nullptr) {
        return;
    }
    GtkWidget *child =
        gtk_widget_get_first_child(box);
    while (child != nullptr) {
        GtkWidget *next =
            gtk_widget_get_next_sibling(child);
        gtk_box_remove(
            GTK_BOX(box), child);
        child = next;
    }
}

GtkWidget *make_preview_row(
    const char *icon_name,
    const char *title,
    const char *subtitle,
    const char *semantic_class)
{
    GtkWidget *row =
        gtk_box_new(
            GTK_ORIENTATION_HORIZONTAL, 9);
    gtk_widget_add_css_class(
        row, "preview-row");

    GtkWidget *icon_well =
        gtk_box_new(
            GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(
        icon_well, "preview-icon-well");
    if (semantic_class != nullptr) {
        gtk_widget_add_css_class(
            icon_well, semantic_class);
    }
    GtkWidget *icon =
        make_icon(icon_name, 18);
    gtk_widget_set_halign(
        icon, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(
        icon, GTK_ALIGN_CENTER);
    gtk_box_append(
        GTK_BOX(icon_well), icon);
    gtk_box_append(
        GTK_BOX(row), icon_well);

    GtkWidget *copy =
        gtk_box_new(
            GTK_ORIENTATION_VERTICAL, 1);
    gtk_widget_set_hexpand(copy, true);
    GtkWidget *heading =
        make_label(
            title, "preview-row-title");
    GtkWidget *note =
        make_label(
            subtitle, "preview-row-note");
    gtk_label_set_ellipsize(
        GTK_LABEL(note),
        PANGO_ELLIPSIZE_END);
    gtk_box_append(
        GTK_BOX(copy), heading);
    gtk_box_append(
        GTK_BOX(copy), note);
    gtk_box_append(GTK_BOX(row), copy);
    return row;
}

void rebuild_discover_update_preview(
    WindowState *state)
{
    if (state == nullptr ||
        state->discover.discover_update_preview == nullptr) {
        return;
    }
    clear_box_children(
        state->discover.discover_update_preview);

    const std::size_t limit =
        std::min<std::size_t>(
            state->updates.inventory.update_records.size(), 2U);
    if (limit == 0U) {
        gtk_box_append(
            GTK_BOX(
                state->discover.discover_update_preview),
            make_preview_row(
                "emblem-ok-symbolic",
                "System is up to date",
                "No preferred updates are waiting.",
                "preview-good"));
        return;
    }

    for (std::size_t index = 0U;
         index < limit;
         ++index) {
        const PackageRecord &record =
            state->updates.inventory.update_records[index];
        std::string version =
            record.installed_version;
        if (!version.empty() &&
            !record.available_version.empty()) {
            version += "  →  " +
                record.available_version;
        } else if (!record.available_version.empty()) {
            version =
                record.available_version;
        }
        gtk_box_append(
            GTK_BOX(
                state->discover.discover_update_preview),
            make_preview_row(
                update_icon_name(record),
                record.name.c_str(),
                version.c_str(),
                record.system_critical
                    ? "preview-warning"
                    : "preview-info"));
    }
}

void rebuild_discover_repository_preview(
    WindowState *state)
{
    if (state == nullptr ||
        state->discover.discover_repository_preview ==
            nullptr) {
        return;
    }
    clear_box_children(
        state->discover.discover_repository_preview);

    if (state->repositories.records.empty()) {
        gtk_box_append(
            GTK_BOX(
                state->discover.discover_repository_preview),
            make_preview_row(
                "network-workgroup-symbolic",
                "Repository status",
                "Reading configured sources…",
                "preview-info"));
        return;
    }

    const std::size_t limit =
        std::min<std::size_t>(
            state->repositories.records.size(),
            3U);
    for (std::size_t index = 0U;
         index < limit;
         ++index) {
        const SourceRecord &source =
            state->repositories.records[index];
        gtk_box_append(
            GTK_BOX(
                state->discover.discover_repository_preview),
            make_preview_row(
                source.kind ==
                        SourceKind::flatpak
                    ? "package-x-generic-symbolic"
                    : "drive-multidisk-symbolic",
                source.name.empty()
                    ? "Software source"
                    : source.name.c_str(),
                source.enabled
                    ? "Online / enabled"
                    : "Disabled",
                source.enabled
                    ? "preview-good"
                    : "preview-warning"));
    }
}

void rebuild_discover_activity_preview(
    WindowState *state)
{
    if (state == nullptr ||
        state->discover.discover_activity_preview ==
            nullptr) {
        return;
    }
    clear_box_children(
        state->discover.discover_activity_preview);

    if (state->history.records.empty()) {
        gtk_box_append(
            GTK_BOX(
                state->discover.discover_activity_preview),
            make_preview_row(
                "document-open-recent-symbolic",
                "No recent activity",
                "Completed software actions will appear here.",
                "preview-info"));
        return;
    }

    std::size_t index = 0U;
    std::size_t shown = 0U;
    while (index <
               state->history.records.size() &&
           shown < 4U) {
        const TransactionHistoryItem &head =
            state->history.records[index];
        std::size_t end = index + 1U;
        while (
            end <
                state->history.records.size() &&
            state->history.records[end]
                    .transaction_id ==
                head.transaction_id) {
            ++end;
        }
        const std::size_t count =
            end - index;
        const std::string title =
            head.success
                ? "Updated " +
                      std::to_string(count) +
                      (count == 1U
                           ? " package"
                           : " packages")
                : "Software action failed";
        const std::string note =
            history_timestamp(
                head.completed_at_unix);
        gtk_box_append(
            GTK_BOX(
                state->discover.discover_activity_preview),
            make_preview_row(
                head.success
                    ? "emblem-ok-symbolic"
                    : "dialog-warning-symbolic",
                title.c_str(),
                note.c_str(),
                head.success
                    ? "preview-good"
                    : "preview-warning"));
        index = end;
        ++shown;
    }
}

std::string selected_category(WindowState *state)
{
    if (state == nullptr || state->discover.discover_category == nullptr) {
        return "All";
    }

    gpointer item = gtk_drop_down_get_selected_item(
        GTK_DROP_DOWN(state->discover.discover_category));
    if (item == nullptr || !GTK_IS_STRING_OBJECT(item)) {
        return "All";
    }

    const char *value =
        gtk_string_object_get_string(GTK_STRING_OBJECT(item));
    return value == nullptr ? std::string("All") : std::string(value);
}

void discover_quick_category_clicked(
    GtkButton *button,
    gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    const char *category = static_cast<const char *>(
        g_object_get_data(
            G_OBJECT(button),
            "discover-quick-category"));
    if (state == nullptr || state->discover.discover_category == nullptr ||
        state->discover.discover_categories == nullptr ||
        category == nullptr) {
        return;
    }

    const guint count =
        g_list_model_get_n_items(
            G_LIST_MODEL(state->discover.discover_categories));
    for (guint index = 0U; index < count; ++index) {
        gpointer object =
            g_list_model_get_item(
                G_LIST_MODEL(state->discover.discover_categories),
                index);
        if (object == nullptr) {
            continue;
        }

        bool matched = false;
        if (GTK_IS_STRING_OBJECT(object)) {
            const char *value =
                gtk_string_object_get_string(
                    GTK_STRING_OBJECT(object));
            matched =
                value != nullptr &&
                std::strcmp(value, category) == 0;
        }
        g_object_unref(object);

        if (matched) {
            gtk_drop_down_set_selected(
                GTK_DROP_DOWN(state->discover.discover_category),
                index);
            return;
        }
    }
}

GtkWidget *make_discover_category_shortcut(
    WindowState *state,
    const char *category,
    const char *label,
    const char *icon_name)
{
    GtkWidget *button = gtk_button_new();
    gtk_widget_add_css_class(
        button, "category-shortcut");

    GtkWidget *content =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 7);
    GtkWidget *icon = make_icon(icon_name, 18);
    GtkWidget *text = make_label(label, "category-shortcut-label");
    gtk_box_append(GTK_BOX(content), icon);
    gtk_box_append(GTK_BOX(content), text);
    gtk_button_set_child(GTK_BUTTON(button), content);

    g_object_set_data_full(
        G_OBJECT(button),
        "discover-quick-category",
        g_strdup(category),
        g_free);
    g_signal_connect(
        button,
        "clicked",
        G_CALLBACK(discover_quick_category_clicked),
        state);
    return button;
}

void rebuild_discover(WindowState *state)
{
    if (state == nullptr || state->discover.discover_visible == nullptr) {
        return;
    }

    const char *search_text =
        state->discover.discover_search == nullptr
            ? ""
            : gtk_editable_get_text(
                  GTK_EDITABLE(state->discover.discover_search));
    const std::string query =
        folded(search_text == nullptr ? "" : search_text);
    const std::string category = selected_category(state);

    std::vector<std::string> indices;
    indices.reserve(state->discover.discover_records.size());
    std::vector<std::size_t> visible_indices;
    visible_indices.reserve(state->discover.discover_records.size());
    const bool cached_search_text =
        state->discover.discover_search_texts.size() ==
        state->discover.discover_records.size();

    for (std::size_t index = 0U;
         index < state->discover.discover_records.size();
         ++index) {
        const PackageRecord &record =
            state->discover.discover_records[index];

        if (category != "All" && record.category != category) {
            continue;
        }

        if (!query.empty()) {
            const std::string haystack =
                cached_search_text
                    ? state->discover.discover_search_texts[index]
                    : discover_search_text(record);
            if (haystack.find(query) == std::string::npos) {
                continue;
            }
        }

        visible_indices.push_back(index);
        indices.emplace_back(std::to_string(index));
    }

    if (state->discover.discover_featured_flow != nullptr) {
        GtkWidget *child =
            gtk_widget_get_first_child(
                state->discover.discover_featured_flow);
        while (child != nullptr) {
            GtkWidget *next =
                gtk_widget_get_next_sibling(child);
            gtk_flow_box_remove(
                GTK_FLOW_BOX(
                    state->discover.discover_featured_flow),
                child);
            child = next;
        }

        if (query.empty() && category == "All") {
            /*
             * The landing page is deliberately curated instead of showing the
             * first four alphabetic package records.  When the system
             * catalogue contains these applications we use its exact state;
             * otherwise the same cards remain useful install entry points
             * while catalogue refresh completes.
             */
            append_curated_featured(
                state, &visible_indices);
        } else {
            const std::size_t limit =
                std::min<std::size_t>(
                    visible_indices.size(), 4U);
            for (std::size_t position = 0U;
                 position < limit;
                 ++position) {
                gtk_flow_box_append(
                    GTK_FLOW_BOX(
                        state->discover.discover_featured_flow),
                    make_featured_card(
                        state,
                        state->discover.discover_records[
                            visible_indices[position]]));
            }
        }
    }

    std::vector<const char *> additions;
    additions.reserve(indices.size() + 1U);
    for (const std::string &index : indices) {
        additions.push_back(index.c_str());
    }
    additions.push_back(nullptr);

    gtk_string_list_splice(
        state->discover.discover_visible,
        0U,
        g_list_model_get_n_items(
            G_LIST_MODEL(state->discover.discover_visible)),
        additions.data());

    if (state->discover.discover_status != nullptr) {
        std::ostringstream status;
        status << indices.size() << " of "
               << state->discover.discover_records.size()
               << " applications shown.";
        gtk_label_set_text(
            GTK_LABEL(state->discover.discover_status), status.str().c_str());
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
        index >= state->discover.discover_records.size()) {
        return;
    }

    gtk_box_append(
        GTK_BOX(holder),
        make_discover_card(
            state,
            state->discover.discover_records[
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
    auto *state = static_cast<WindowState *>(user_data);
    if (state == nullptr) {
        return;
    }

    if (!state->search_syncing &&
        state->discover.discover_search != nullptr &&
        state->global_search != nullptr) {
        const char *local_text =
            gtk_editable_get_text(
                GTK_EDITABLE(state->discover.discover_search));
        const char *global_text =
            gtk_editable_get_text(
                GTK_EDITABLE(state->global_search));
        if (g_strcmp0(local_text, global_text) != 0) {
            /*
             * Mirroring the two search boxes used to recurse through the
             * opposite "search-changed" handler.  That handler also changed
             * navigation selection, which could steal focus between
             * keystrokes and make each new character replace the previous
             * one.  Mirror silently at the application level instead.
             */
            state->search_syncing = true;
            gtk_editable_set_text(
                GTK_EDITABLE(state->global_search),
                local_text == nullptr ? "" : local_text);
            state->search_syncing = false;
        }
    }
    rebuild_discover(state);
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
            prepare_discover_presentation(result);
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
    prepare_discover_presentation(result);
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
        task_data->generation != state->discover.discover_generation) {
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

    for (PackageRecord &record : state->discover.discover_records) {
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
        state->discover.discover_status != nullptr) {
        const std::string message =
            "Applications loaded; installed-state detection is unavailable: " +
            result->warning;
        gtk_label_set_text(
            GTK_LABEL(state->discover.discover_status),
            message.c_str());
    }

    delete result;
}

void start_discover_installed_hydration(
    WindowState *state,
    const unsigned int generation)
{
    if (state == nullptr || state->window == nullptr ||
        state->discover.discover_records.empty()) {
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
        task_data->generation != state->discover.discover_generation) {
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
    for (PackageRecord &record : state->discover.discover_records) {
        const auto found = icons.find(record.id);
        if (found != icons.end()) {
            record.cached_icon_path = found->second;
        }
    }
    rebuild_discover(state);

    if (!result->warning.empty() &&
        state->discover.discover_status != nullptr) {
        const std::string message =
            "Applications are available; some icons could not be verified: " +
            result->warning;
        gtk_label_set_text(
            GTK_LABEL(state->discover.discover_status), message.c_str());
    }

    delete result;
}

void start_discover_icon_hydration(
    WindowState *state,
    const unsigned int generation)
{
    if (state == nullptr || state->window == nullptr ||
        state->discover.discover_records.empty()) {
        return;
    }

    bool has_remote_icons = false;
    for (const PackageRecord &record : state->discover.discover_records) {
        if (!record.icon_url.empty() &&
            record.cached_icon_path.empty()) {
            has_remote_icons = true;
            break;
        }
    }
    if (!has_remote_icons) {
        return;
    }

    if (state->discover.discover_status != nullptr) {
        const std::string message =
            std::to_string(state->discover.discover_records.size()) +
            " applications loaded; verifying icons in the background…";
        gtk_label_set_text(
            GTK_LABEL(state->discover.discover_status), message.c_str());
    }

    GTask *task = g_task_new(
        G_OBJECT(state->window),
        nullptr,
        discover_icons_complete,
        nullptr);
    auto *task_data = new IconHydrationTaskData{};
    task_data->generation = generation;
    task_data->records = state->discover.discover_records;
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
        if (state->discover.discover_state != nullptr) {
            gtk_label_set_text(
                GTK_LABEL(state->discover.discover_state), "Unavailable");
        }
        if (state->discover.discover_status != nullptr) {
            gtk_label_set_text(
                GTK_LABEL(state->discover.discover_status),
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
        task_data->generation != state->discover.discover_generation) {
        delete result;
        return;
    }

    state->discover.discover_records = std::move(result->snapshot.records);
    state->discover.discover_search_texts = std::move(result->search_texts);

    if (state->discover.discover_categories != nullptr) {
        while (g_list_model_get_n_items(
                   G_LIST_MODEL(state->discover.discover_categories)) > 0U) {
            gtk_string_list_remove(state->discover.discover_categories, 0U);
        }
        gtk_string_list_append(state->discover.discover_categories, "All");
        for (const std::string &category : result->categories) {
            gtk_string_list_append(
                state->discover.discover_categories, category.c_str());
        }
        gtk_drop_down_set_selected(
            GTK_DROP_DOWN(state->discover.discover_category), 0U);
    }

    if (state->discover.discover_count != nullptr) {
        const std::string count =
            std::to_string(state->discover.discover_records.size());
        gtk_label_set_text(
            GTK_LABEL(state->discover.discover_count), count.c_str());
    }
    if (state->discover.discover_source != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->discover.discover_source),
            "Infiltrator + system");
    }
    if (state->discover.discover_state != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->discover.discover_state),
            result->snapshot.from_cache ? "Cached" : "Ready");
    }
    if (state->discover.discover_status != nullptr) {
        if (!result->warning.empty()) {
            gtk_label_set_text(
                GTK_LABEL(state->discover.discover_status),
                result->warning.c_str());
        } else if (task_data->force_refresh) {
            const std::string message =
                "Catalogue synchronized: " +
                std::to_string(result->added) +
                " added, " +
                std::to_string(result->removed) +
                " removed.";
            gtk_label_set_text(
                GTK_LABEL(state->discover.discover_status),
                message.c_str());
        } else {
            gtk_label_set_text(
                GTK_LABEL(state->discover.discover_status),
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
    const bool force_refresh)
{
    if (state == nullptr || state->window == nullptr ||
        state->discover.discover_state == nullptr) {
        return;
    }

    state->discover.discover_loaded = true;
    ++state->discover.discover_generation;
    gtk_label_set_text(
        GTK_LABEL(state->discover.discover_state), "Loading");
    if (state->discover.discover_status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->discover.discover_status),
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
    task_data->generation = state->discover.discover_generation;
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


void dashboard_nav_clicked(
    GtkButton *button,
    gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    if (state == nullptr) {
        return;
    }

    const int index =
        GPOINTER_TO_INT(
            g_object_get_data(
                G_OBJECT(button),
                "dashboard-page-index"));
    select_app_page(state, static_cast<AppPage>(index));
}

GtkWidget *make_dashboard_card(
    WindowState *state,
    const int page_index,
    const char *icon_name,
    const char *title,
    const char *value,
    const char *subtitle,
    const char *semantic_class,
    GtkWidget **value_out)
{
    GtkWidget *button = gtk_button_new();
    gtk_widget_add_css_class(
        button, "dashboard-card");
    if (semantic_class != nullptr) {
        gtk_widget_add_css_class(
            button, semantic_class);
    }

    GtkWidget *row =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);

    GtkWidget *icon_well =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(
        icon_well, "dashboard-icon-well");
    GtkWidget *icon = make_icon(icon_name, 24);
    gtk_widget_set_halign(
        icon, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(
        icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(icon_well), icon);
    gtk_box_append(GTK_BOX(row), icon_well);

    GtkWidget *copy =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(copy, true);

    GtkWidget *caption =
        make_label(title, "dashboard-card-title");
    GtkWidget *value_label =
        make_label(value, "dashboard-card-value");
    GtkWidget *note =
        make_label(subtitle, "dashboard-card-note");
    gtk_label_set_ellipsize(
        GTK_LABEL(note), PANGO_ELLIPSIZE_END);

    gtk_box_append(GTK_BOX(copy), caption);
    gtk_box_append(GTK_BOX(copy), value_label);
    gtk_box_append(GTK_BOX(copy), note);
    gtk_box_append(GTK_BOX(row), copy);

    GtkWidget *chevron =
        make_label("›", "dashboard-chevron", 0.5F);
    gtk_widget_set_valign(
        chevron, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(row), chevron);

    gtk_button_set_child(
        GTK_BUTTON(button), row);
    g_object_set_data(
        G_OBJECT(button),
        "dashboard-page-index",
        GINT_TO_POINTER(page_index));
    g_signal_connect(
        button,
        "clicked",
        G_CALLBACK(dashboard_nav_clicked),
        state);

    if (value_out != nullptr) {
        *value_out = value_label;
    }
    return button;
}

GtkWidget *make_discover_welcome_hero()
{
    GtkWidget *hero =
        gtk_overlay_new();
    gtk_widget_add_css_class(
        hero, "discover-welcome");
    gtk_widget_set_overflow(
        hero, GTK_OVERFLOW_HIDDEN);
    gtk_widget_set_size_request(
        hero, -1, 166);

    /*
     * The Discover background is a dedicated clean raster asset: artwork only,
     * with no embedded product wording, checker sphere, retro computer or
     * other mock-up props.  Every visible word is a live GTK label layered
     * above the image so it remains scalable and accessible.
     */
    GtkWidget *picture =
        make_embedded_picture(
            kDiscoverHeroBase64,
            "welcome-artwork",
            GTK_CONTENT_FIT_COVER);
    if (picture != nullptr) {
        gtk_widget_set_hexpand(
            picture, true);
        gtk_widget_set_vexpand(
            picture, true);
        gtk_overlay_set_child(
            GTK_OVERLAY(hero),
            picture);
    } else {
        GtkWidget *fallback =
            gtk_box_new(
                GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_add_css_class(
            fallback, "welcome-artwork-error");
        gtk_overlay_set_child(
            GTK_OVERLAY(hero),
            fallback);
    }

    GtkWidget *veil =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(
        veil, "welcome-artwork-veil");
    gtk_widget_set_hexpand(
        veil, true);
    gtk_widget_set_vexpand(
        veil, true);
    gtk_widget_set_can_target(
        veil, false);
    gtk_overlay_add_overlay(
        GTK_OVERLAY(hero), veil);

    GtkWidget *copy =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(
        copy, "welcome-copy");
    gtk_widget_set_halign(
        copy, GTK_ALIGN_START);
    gtk_widget_set_valign(
        copy, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(
        copy, 24);
    gtk_widget_set_margin_end(
        copy, 24);
    gtk_widget_set_can_target(
        copy, false);

    GtkWidget *kicker =
        make_label(
            "INFLITRATOR SOFTWARE",
            "welcome-kicker");

    GtkWidget *heading =
        gtk_label_new(nullptr);
    gtk_label_set_xalign(
        GTK_LABEL(heading), 0.0F);
    gtk_label_set_markup(
        GTK_LABEL(heading),
        "Welcome to <span foreground=\"#d93cff\">Infiltrator</span>"
        " <span foreground=\"#57a8ff\">Software</span>");
    gtk_widget_add_css_class(
        heading, "welcome-title");

    GtkWidget *subtitle =
        make_label(
            "Discover, install and keep your system up to date with the latest software.",
            "welcome-subtitle");
    gtk_label_set_wrap(
        GTK_LABEL(subtitle), true);
    gtk_label_set_max_width_chars(
        GTK_LABEL(subtitle), 72);

    gtk_box_append(GTK_BOX(copy), kicker);
    gtk_box_append(GTK_BOX(copy), heading);
    gtk_box_append(GTK_BOX(copy), subtitle);
    gtk_overlay_add_overlay(
        GTK_OVERLAY(hero), copy);

    return hero;
}

GtkWidget *make_discover_page(WindowState *state)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(page, "content");
    gtk_widget_add_css_class(page, "page-discover");

    gtk_box_append(
        GTK_BOX(page),
        make_discover_welcome_hero());

    GtkWidget *dashboard =
        gtk_grid_new();
    gtk_widget_add_css_class(
        dashboard, "discover-dashboard");
    gtk_grid_set_column_spacing(
        GTK_GRID(dashboard), 10);
    gtk_grid_set_column_homogeneous(
        GTK_GRID(dashboard), true);

    gtk_grid_attach(
        GTK_GRID(dashboard),
        make_dashboard_card(
            state,
            2,
            "software-update-available-symbolic",
            "Available Updates",
            "View",
            "New versions and system updates",
            "dashboard-updates",
            &state->discover.discover_updates_summary),
        0, 0, 1, 1);
    gtk_grid_attach(
        GTK_GRID(dashboard),
        make_dashboard_card(
            state,
            6,
            "emblem-ok-symbolic",
            "System Health",
            "Check",
            "Package and repository integrity",
            "dashboard-health",
            &state->discover.discover_health_summary),
        1, 0, 1, 1);
    gtk_grid_attach(
        GTK_GRID(dashboard),
        make_dashboard_card(
            state,
            4,
            "network-workgroup-symbolic",
            "Repositories",
            "Manage",
            "Configured software sources",
            "dashboard-repositories",
            &state->discover.discover_repositories_summary),
        2, 0, 1, 1);

    gtk_box_append(GTK_BOX(page), dashboard);

    GtkWidget *main_dashboard =
        gtk_grid_new();
    gtk_widget_add_css_class(
        main_dashboard, "discover-main-dashboard");
    gtk_grid_set_column_spacing(
        GTK_GRID(main_dashboard), 12);
    gtk_grid_set_column_homogeneous(
        GTK_GRID(main_dashboard), true);

    GtkWidget *left_column =
        gtk_box_new(
            GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_hexpand(
        left_column, true);

    GtkWidget *featured_panel =
        gtk_box_new(
            GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(
        featured_panel, "discover-panel");
    gtk_widget_add_css_class(
        featured_panel, "featured-panel");

    GtkWidget *featured_header =
        gtk_box_new(
            GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *featured_icon =
        make_icon(
            "starred-symbolic", 23);
    gtk_widget_add_css_class(
        featured_icon, "featured-header-icon");
    GtkWidget *featured_copy =
        gtk_box_new(
            GTK_ORIENTATION_VERTICAL, 1);
    gtk_widget_set_hexpand(
        featured_copy, true);
    gtk_box_append(
        GTK_BOX(featured_copy),
        make_label(
            "Featured Applications",
            "panel-title"));
    gtk_box_append(
        GTK_BOX(featured_copy),
        make_label(
            "Recommended software from the current catalogue.",
            "panel-subtitle"));
    gtk_box_append(
        GTK_BOX(featured_header),
        featured_icon);
    gtk_box_append(
        GTK_BOX(featured_header),
        featured_copy);
    gtk_box_append(
        GTK_BOX(featured_panel),
        featured_header);

    state->discover.discover_featured_flow =
        gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(
        GTK_FLOW_BOX(
            state->discover.discover_featured_flow),
        GTK_SELECTION_NONE);
    gtk_flow_box_set_min_children_per_line(
        GTK_FLOW_BOX(
            state->discover.discover_featured_flow),
        4U);
    gtk_flow_box_set_max_children_per_line(
        GTK_FLOW_BOX(
            state->discover.discover_featured_flow),
        4U);
    gtk_flow_box_set_column_spacing(
        GTK_FLOW_BOX(
            state->discover.discover_featured_flow),
        10U);
    gtk_flow_box_set_homogeneous(
        GTK_FLOW_BOX(
            state->discover.discover_featured_flow),
        true);
    gtk_widget_set_valign(
        state->discover.discover_featured_flow,
        GTK_ALIGN_START);
    gtk_box_append(
        GTK_BOX(featured_panel),
        state->discover.discover_featured_flow);
    append_curated_featured(state);
    gtk_box_append(
        GTK_BOX(left_column),
        featured_panel);

    GtkWidget *updates_panel =
        gtk_box_new(
            GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(
        updates_panel, "discover-panel");
    gtk_widget_add_css_class(
        updates_panel, "updates-preview-panel");
    gtk_box_append(
        GTK_BOX(updates_panel),
        make_label(
            "Updates Available",
            "panel-title"));
    state->discover.discover_update_preview =
        gtk_box_new(
            GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_append(
        GTK_BOX(updates_panel),
        state->discover.discover_update_preview);
    rebuild_discover_update_preview(state);

    GtkWidget *update_all =
        gtk_button_new_with_label(
            "Open Updates");
    gtk_widget_add_css_class(
        update_all, "suggested-action");
    g_object_set_data(
        G_OBJECT(update_all),
        "dashboard-page-index",
        GINT_TO_POINTER(2));
    g_signal_connect(
        update_all,
        "clicked",
        G_CALLBACK(dashboard_nav_clicked),
        state);
    gtk_widget_set_halign(
        update_all, GTK_ALIGN_END);
    gtk_box_append(
        GTK_BOX(updates_panel),
        update_all);
    gtk_box_append(
        GTK_BOX(left_column),
        updates_panel);

    GtkWidget *health =
        gtk_button_new();
    gtk_widget_add_css_class(
        health, "health-banner");
    GtkWidget *health_row =
        gtk_box_new(
            GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *health_icon_well =
        gtk_box_new(
            GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(
        health_icon_well,
        "health-banner-icon");
    GtkWidget *health_icon =
        make_icon(
            "emblem-ok-symbolic", 24);
    gtk_widget_set_halign(
        health_icon, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(
        health_icon, GTK_ALIGN_CENTER);
    gtk_box_append(
        GTK_BOX(health_icon_well),
        health_icon);
    gtk_box_append(
        GTK_BOX(health_row),
        health_icon_well);

    GtkWidget *health_copy =
        gtk_box_new(
            GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(
        health_copy, true);
    gtk_box_append(
        GTK_BOX(health_copy),
        make_label(
            "Keep your system healthy",
            "health-banner-title"));
    state->discover.discover_health_banner_state =
        make_label(
            "Checking package and repository health…",
            "health-banner-copy");
    gtk_box_append(
        GTK_BOX(health_copy),
        state->discover.discover_health_banner_state);
    gtk_box_append(
        GTK_BOX(health_row),
        health_copy);
    gtk_box_append(
        GTK_BOX(health_row),
        make_label(
            "Open Repair Center  ›",
            "health-banner-action",
            1.0F));
    gtk_button_set_child(
        GTK_BUTTON(health),
        health_row);
    g_object_set_data(
        G_OBJECT(health),
        "dashboard-page-index",
        GINT_TO_POINTER(6));
    g_signal_connect(
        health,
        "clicked",
        G_CALLBACK(dashboard_nav_clicked),
        state);
    gtk_box_append(
        GTK_BOX(left_column), health);

    GtkWidget *right_column =
        gtk_box_new(
            GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_hexpand(
        right_column, true);

    GtkWidget *repository_panel =
        gtk_box_new(
            GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(
        repository_panel, "discover-panel");
    gtk_box_append(
        GTK_BOX(repository_panel),
        make_label(
            "Repository Status",
            "panel-title"));
    state->discover.discover_repository_preview =
        gtk_box_new(
            GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_append(
        GTK_BOX(repository_panel),
        state->discover.discover_repository_preview);
    rebuild_discover_repository_preview(state);
    gtk_box_append(
        GTK_BOX(right_column),
        repository_panel);

    GtkWidget *activity_panel =
        gtk_box_new(
            GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(
        activity_panel, "discover-panel");
    gtk_box_append(
        GTK_BOX(activity_panel),
        make_label(
            "Recent Activity",
            "panel-title"));
    state->discover.discover_activity_preview =
        gtk_box_new(
            GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_append(
        GTK_BOX(activity_panel),
        state->discover.discover_activity_preview);
    rebuild_discover_activity_preview(state);
    gtk_box_append(
        GTK_BOX(right_column),
        activity_panel);

    /*
     * The reference composition is structural: Featured/Updates/Health own
     * two thirds of this row and Repository/Activity own one third.  A
     * homogeneous three-column grid preserves that ratio at every window and
     * desktop scale instead of letting a fixed right-rail width consume half
     * the page on scaled displays.
     */
    gtk_grid_attach(
        GTK_GRID(main_dashboard),
        left_column,
        0, 0, 2, 1);
    gtk_grid_attach(
        GTK_GRID(main_dashboard),
        right_column,
        2, 0, 1, 1);
    gtk_box_append(
        GTK_BOX(page),
        main_dashboard);

    GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(controls, "discover-controls");

    state->discover.discover_search = gtk_search_entry_new();
    gtk_search_entry_set_placeholder_text(
        GTK_SEARCH_ENTRY(state->discover.discover_search),
        "Search applications…");
    gtk_widget_set_hexpand(state->discover.discover_search, true);
    g_signal_connect(
        state->discover.discover_search,
        "search-changed",
        G_CALLBACK(discover_filter_changed),
        state);
    gtk_box_append(GTK_BOX(controls), state->discover.discover_search);

    state->discover.discover_categories = gtk_string_list_new(nullptr);
    gtk_string_list_append(state->discover.discover_categories, "All");
    state->discover.discover_category =
        gtk_drop_down_new(
            G_LIST_MODEL(state->discover.discover_categories), nullptr);
    gtk_widget_set_size_request(state->discover.discover_category, 190, -1);
    g_signal_connect(
        state->discover.discover_category,
        "notify::selected",
        G_CALLBACK(discover_category_changed),
        state);
    gtk_box_append(GTK_BOX(controls), state->discover.discover_category);
    gtk_box_append(GTK_BOX(page), controls);

    GtkWidget *shortcuts =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(
        shortcuts, "category-shortcuts");
    gtk_box_append(
        GTK_BOX(shortcuts),
        make_discover_category_shortcut(
            state, "All", "All software",
            "view-grid-symbolic"));
    gtk_box_append(
        GTK_BOX(shortcuts),
        make_discover_category_shortcut(
            state, "Productivity", "Productivity",
            "accessories-calculator-symbolic"));
    gtk_box_append(
        GTK_BOX(shortcuts),
        make_discover_category_shortcut(
            state, "Graphics", "Graphics",
            "applications-graphics-symbolic"));
    gtk_box_append(
        GTK_BOX(shortcuts),
        make_discover_category_shortcut(
            state, "Internet", "Internet",
            "applications-internet-symbolic"));
    gtk_box_append(
        GTK_BOX(shortcuts),
        make_discover_category_shortcut(
            state, "System Tools", "System",
            "applications-system-symbolic"));
    gtk_box_append(GTK_BOX(page), shortcuts);

    GtkWidget *catalogue_heading =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(
        catalogue_heading,
        "catalogue-section-heading");
    GtkWidget *catalogue_icon_widget =
        make_icon("view-grid-symbolic", 20);
    gtk_widget_add_css_class(
        catalogue_icon_widget,
        "catalogue-section-icon");
    GtkWidget *catalogue_copy =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gtk_widget_set_hexpand(
        catalogue_copy, true);
    GtkWidget *catalogue_title =
        make_label(
            "Explore software",
            "catalogue-section-title");
    GtkWidget *catalogue_note =
        make_label(
            "Applications from Infiltrator, system repositories and Flatpak.",
            "catalogue-section-note");
    gtk_box_append(
        GTK_BOX(catalogue_copy), catalogue_title);
    gtk_box_append(
        GTK_BOX(catalogue_copy), catalogue_note);
    gtk_box_append(
        GTK_BOX(catalogue_heading),
        catalogue_icon_widget);
    gtk_box_append(
        GTK_BOX(catalogue_heading),
        catalogue_copy);
    gtk_box_append(
        GTK_BOX(page), catalogue_heading);

    state->discover.discover_status = make_label(
        "Refreshing Infiltrator, system and Flatpak metadata…",
        "discover-status");
    gtk_box_append(GTK_BOX(page), state->discover.discover_status);

    state->discover.discover_visible = gtk_string_list_new(nullptr);

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
                G_LIST_MODEL(state->discover.discover_visible)));
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
    gtk_widget_set_size_request(
        scroll, -1, 520);
    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER,
        GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_overlay_scrolling(
        GTK_SCROLLED_WINDOW(scroll),
        false);
    gtk_scrolled_window_set_child(
        GTK_SCROLLED_WINDOW(scroll),
        grid);
    gtk_box_append(GTK_BOX(page), scroll);

    GtkWidget *page_scroll =
        gtk_scrolled_window_new();
    gtk_widget_add_css_class(
        page_scroll, "page-scroller");
    gtk_widget_set_hexpand(
        page_scroll, true);
    gtk_widget_set_vexpand(
        page_scroll, true);
    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(page_scroll),
        GTK_POLICY_NEVER,
        GTK_POLICY_ALWAYS);
    gtk_scrolled_window_set_overlay_scrolling(
        GTK_SCROLLED_WINDOW(page_scroll),
        false);
    gtk_scrolled_window_set_child(
        GTK_SCROLLED_WINDOW(page_scroll),
        page);

    return page_scroll;
}


struct DiscoverFlatpakOperation {
    GtkWindow *main_window{};
    GtkWidget *button{};
    GtkWidget *status{};
    std::string application_id;
    std::string remote;
    bool user_installation{false};
    bool install{true};
};

void destroy_discover_flatpak_operation(gpointer pointer)
{
    auto *operation =
        static_cast<DiscoverFlatpakOperation *>(pointer);
    if (operation == nullptr) return;
    if (operation->main_window != nullptr) {
        g_object_unref(operation->main_window);
    }
    if (operation->button != nullptr) {
        g_object_unref(operation->button);
    }
    if (operation->status != nullptr) {
        g_object_unref(operation->status);
    }
    delete operation;
}

TransactionPlan discover_flatpak_history_plan(
    const DiscoverFlatpakOperation &operation)
{
    TransactionPlan plan;
    TransactionItem item;
    item.package_id = operation.application_id;
    item.action =
        operation.install
            ? TransactionAction::install
            : TransactionAction::remove;
    item.source =
        operation.user_installation
            ? "Flatpak User"
            : "Flatpak System";
    item.requested = true;
    plan.items.emplace_back(std::move(item));
    plan.touches_system = !operation.user_installation;
    return plan;
}

void discover_flatpak_worker(
    GTask *task,
    gpointer,
    gpointer task_data,
    GCancellable *)
{
    auto *operation =
        static_cast<DiscoverFlatpakOperation *>(task_data);
    if (operation == nullptr) {
        g_task_return_new_error(
            task,
            G_IO_ERROR,
            G_IO_ERROR_FAILED,
            "Flatpak operation state is unavailable.");
        return;
    }

    std::string error;
    if (!set_flatpak_application_installed(
            operation->application_id,
            operation->remote,
            operation->user_installation,
            operation->install,
            error)) {
        g_task_return_new_error(
            task,
            G_IO_ERROR,
            G_IO_ERROR_FAILED,
            "%s",
            error.empty()
                ? "Flatpak operation failed."
                : error.c_str());
        return;
    }
    g_task_return_boolean(task, TRUE);
}

void discover_flatpak_complete(
    GObject *,
    GAsyncResult *async_result,
    gpointer)
{
    auto *task = G_TASK(async_result);
    auto *operation =
        static_cast<DiscoverFlatpakOperation *>(
            g_task_get_task_data(task));
    if (operation == nullptr) {
        return;
    }

    GError *error = nullptr;
    const bool success =
        g_task_propagate_boolean(task, &error) != FALSE;

    std::string message;
    if (success) {
        message =
            operation->install
                ? "Flatpak application installed."
                : "Flatpak application removed.";
    } else {
        message =
            error != nullptr && error->message != nullptr
                ? one_line(error->message)
                : "Flatpak operation failed.";
    }

    if (operation->status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(operation->status),
            message.c_str());
    }
    if (operation->button != nullptr) {
        gtk_widget_set_sensitive(
            operation->button,
            success ? false : true);
    }

    const TransactionPlan history_plan =
        discover_flatpak_history_plan(*operation);
    record_transaction_history(
        history_plan,
        success,
        message);

    if (operation->main_window != nullptr) {
        auto *state = static_cast<WindowState *>(
            g_object_get_data(
                G_OBJECT(operation->main_window),
                "infiltrator-window-state"));
        if (state != nullptr) {
            if (success) {
                refresh_discover(state, true);
                if (state->installed.loaded) {
                    refresh_installed_controller(&state->installed);
                }
                if (state->updates.inventory.updates_loaded) {
                    refresh_updates(state, false);
                }
            }
            if (state->history.loaded) {
                refresh_history_controller(&state->history);
            }
        }
    }

    g_clear_error(&error);
}

void start_discover_flatpak_operation(
    DiscoverFlatpakOperation *operation)
{
    if (operation == nullptr || operation->button == nullptr) {
        destroy_discover_flatpak_operation(operation);
        return;
    }

    if (operation->status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(operation->status),
            operation->install
                ? "Installing the selected Flatpak application…"
                : "Removing the selected Flatpak application…");
    }

    GTask *task =
        g_task_new(
            G_OBJECT(operation->button),
            nullptr,
            discover_flatpak_complete,
            nullptr);
    g_task_set_task_data(
        task,
        operation,
        destroy_discover_flatpak_operation);
    g_task_run_in_thread(
        task,
        discover_flatpak_worker);
    g_object_unref(task);
}

void discover_flatpak_confirm_response(
    GtkDialog *dialog,
    const gint response_id,
    gpointer user_data)
{
    auto *operation =
        static_cast<DiscoverFlatpakOperation *>(user_data);
    gtk_window_destroy(GTK_WINDOW(dialog));

    if (operation == nullptr) {
        return;
    }
    if (response_id == GTK_RESPONSE_ACCEPT) {
        start_discover_flatpak_operation(operation);
        return;
    }

    if (operation->status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(operation->status),
            operation->install
                ? "Flatpak installation cancelled."
                : "Flatpak removal cancelled.");
    }
    if (operation->button != nullptr) {
        gtk_widget_set_sensitive(
            operation->button, true);
    }
    destroy_discover_flatpak_operation(operation);
}

void confirm_discover_flatpak_operation(
    WindowState *state,
    GtkWidget *button,
    GtkWidget *status,
    const PackageRecord &record)
{
    const bool user_installation =
        record.id.rfind("flatpak:user:", 0U) == 0U;
    const bool system_installation =
        record.id.rfind("flatpak:system:", 0U) == 0U;
    if (!user_installation && !system_installation) {
        if (status != nullptr) {
            gtk_label_set_text(
                GTK_LABEL(status),
                "Flatpak application has no installation-scope identity.");
        }
        gtk_widget_set_sensitive(button, true);
        return;
    }

    auto *operation =
        new DiscoverFlatpakOperation{};
    operation->main_window =
        GTK_WINDOW(g_object_ref(state->window));
    operation->button =
        GTK_WIDGET(g_object_ref(button));
    operation->status =
        status != nullptr
            ? GTK_WIDGET(g_object_ref(status))
            : nullptr;
    operation->application_id =
        record.package_name;
    operation->remote =
        record.repository_origin;
    operation->user_installation =
        user_installation;
    operation->install =
        record.state !=
            infiltrator::software::InstallState::installed;

    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    GtkWidget *dialog =
        gtk_dialog_new_with_buttons(
            operation->install
                ? "Review Flatpak installation"
                : "Review Flatpak removal",
            state->window,
            static_cast<GtkDialogFlags>(
                GTK_DIALOG_MODAL |
                GTK_DIALOG_DESTROY_WITH_PARENT),
            "Cancel",
            GTK_RESPONSE_CANCEL,
            operation->install ? "Install" : "Remove",
            GTK_RESPONSE_ACCEPT,
            nullptr);
    GtkWidget *content =
        gtk_dialog_get_content_area(
            GTK_DIALOG(dialog));
    G_GNUC_END_IGNORE_DEPRECATIONS
    gtk_box_set_spacing(GTK_BOX(content), 12);
    gtk_widget_set_margin_start(content, 18);
    gtk_widget_set_margin_end(content, 18);
    gtk_widget_set_margin_top(content, 16);
    gtk_widget_set_margin_bottom(content, 16);

    const std::string heading =
        std::string(
            operation->install ? "Install " : "Remove ") +
        record.name;
    GtkWidget *heading_label =
        make_label(
            heading.c_str(),
            "hero-title");
    gtk_label_set_wrap(
        GTK_LABEL(heading_label), true);
    gtk_box_append(
        GTK_BOX(content),
        heading_label);

    std::string detail =
        "Flatpak " +
        std::string(
            user_installation
                ? "User"
                : "System") +
        " installation · " +
        record.package_name;
    if (!record.repository_origin.empty()) {
        detail +=
            " · remote " +
            record.repository_origin;
    }
    GtkWidget *detail_label =
        make_label(
            detail.c_str(),
            "detail-note");
    gtk_label_set_wrap(
        GTK_LABEL(detail_label), true);
    gtk_box_append(
        GTK_BOX(content),
        detail_label);

    g_signal_connect(
        dialog,
        "response",
        G_CALLBACK(
            discover_flatpak_confirm_response),
        operation);
    gtk_window_present(GTK_WINDOW(dialog));
}

struct DiscoverPlanTaskData {
    std::string package_id;
    TransactionAction action{TransactionAction::install};
    bool install_recommends{false};
    GtkWindow *main_window{};
};

struct DiscoverPlanResult {
    std::optional<TransactionPlan> plan;
    std::string error;
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
        request.install_recommends =
            data->install_recommends;

        std::string engine_error;
        EngineClient engine;
        result->plan = engine.plan(request, engine_error);
        if (!result->plan.has_value()) {
            /*
             * A missing/stale shared generation is recoverable without
             * reviving the retired APT planner. Reconcile once, then retry the
             * exact native request. Mutation still cannot begin unless the
             * native planner returns a complete reviewed transaction.
             */
            std::string refresh_error;
            if (engine.refresh(refresh_error)) {
                engine_error.clear();
                result->plan =
                    engine.plan(request, engine_error);
            }
            if (!result->plan.has_value()) {
                result->error =
                    "Native planner: " + one_line(engine_error);
                if (!refresh_error.empty()) {
                    result->error +=
                        "  State refresh: " +
                        one_line(refresh_error);
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
                history_state->history.loaded) {
                refresh_history_controller(&history_state->history);
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
            if (state->installed.loaded) refresh_installed_controller(&state->installed);
            if (state->updates.inventory.updates_loaded) refresh_updates(state);
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
    if (!exact_transaction_specs(operation->plan, specs, plan_error)) {
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
                history_state->history.loaded) {
                refresh_history_controller(&history_state->history);
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
                history_state->history.loaded) {
                refresh_history_controller(&history_state->history);
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
        make_transaction_review_dialog(
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
            plan);
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

    if (record->id.rfind("flatpak:", 0U) == 0U) {
        if (status != nullptr) {
            gtk_label_set_text(
                GTK_LABEL(status),
                action == TransactionAction::remove
                    ? "Preparing scoped Flatpak removal…"
                    : "Preparing scoped Flatpak installation…");
        }
        confirm_discover_flatpak_operation(
            state,
            GTK_WIDGET(button),
            status,
            *record);
        return;
    }
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
    data->install_recommends =
        state->preferences.install_recommends;
    data->main_window = GTK_WINDOW(g_object_ref(state->window));

    GTask *task = g_task_new(
        G_OBJECT(button), nullptr,
        discover_plan_complete, nullptr);
    g_task_set_task_data(
        task, data, destroy_discover_plan_task_data);
    g_task_run_in_thread(task, discover_plan_worker);
    g_object_unref(task);
}

} // namespace infiltrator::software::app
