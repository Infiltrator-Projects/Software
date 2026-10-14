// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/updates_view.hpp"
#include "app/page_support.hpp"
#include "app/app_coordinator.hpp"
#include "app/transaction_review.hpp"
#include "app/ui_components.hpp"
#include "app/updates_controller.hpp"
#include "app/window_state.hpp"
#include "core/model.hpp"
#include "core/exact_transaction_spec.hpp"
#include "core/update_policy.hpp"
#include "external/external_updates.hpp"

#include "app/process_support.hpp"

#include <gtk/gtk.h>
#include <curl/curl.h>

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

using infiltrator::software::PackageRecord;
using infiltrator::software::TransactionAction;
using infiltrator::software::TransactionPlan;
using infiltrator::software::TransactionItem;
using infiltrator::software::save_software_preferences;
using infiltrator::software::update_is_ignored;
using infiltrator::software::reboot_required;
using infiltrator::software::reboot_required_packages;
using infiltrator::software::ExternalUpdate;
using infiltrator::software::ExternalUpdateKind;
using infiltrator::software::external_update_kind_name;
using infiltrator::software::apply_cinnamon_updates_selected;
using infiltrator::software::make_icon;
using infiltrator::software::make_label;
using infiltrator::software::make_page_intro;
using infiltrator::software::make_stat_card;

void refresh_updates(WindowState *state, bool refresh_metadata);
void refresh_updates_internal(
    WindowState *state,
    bool refresh_metadata,
    bool refresh_external);
void rebuild_updates(WindowState *state);

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

std::filesystem::path privileged_update_progress_path()
{
    return std::filesystem::path("/run/infiltrator-software") /
           ("update-" +
            std::to_string(
                static_cast<unsigned long long>(getuid())) +
            ".state");
}

struct PrivilegedUpdateProgress {
    std::string token;
    std::string phase;
    std::string detail;
};

std::optional<PrivilegedUpdateProgress>
read_privileged_update_progress()
{
    std::ifstream input(privileged_update_progress_path());
    if (!input) {
        return std::nullopt;
    }

    std::string line;
    if (!std::getline(input, line) || line.empty()) {
        return std::nullopt;
    }

    const std::size_t first = line.find('\t');
    if (first == std::string::npos) {
        return std::nullopt;
    }
    const std::size_t second = line.find('\t', first + 1U);
    if (second == std::string::npos) {
        return std::nullopt;
    }

    PrivilegedUpdateProgress progress;
    progress.token = line.substr(0U, first);
    progress.phase =
        line.substr(first + 1U, second - first - 1U);
    progress.detail = line.substr(second + 1U);
    if (progress.token.empty() || progress.phase.empty()) {
        return std::nullopt;
    }
    return progress;
}


int update_phase_index(const std::string_view phase) noexcept
{
    if (phase == "authorize" || phase == "wait-lock") return 0;
    if (phase == "refresh") return 1;
    if (phase == "validate") return 2;
    if (phase == "download") return 3;
    if (phase == "install" ||
        phase == "configure" ||
        phase == "finalize") return 4;
    if (phase == "verify" || phase == "complete") return 5;
    return -1;
}

const char *update_phase_title(const std::string_view phase) noexcept
{
    if (phase == "authorize") return "Waiting for authorization";
    if (phase == "wait-lock") return "Waiting for package manager";
    if (phase == "refresh") return "Refreshing package metadata";
    if (phase == "validate") return "Re-validating approved changes";
    if (phase == "download") return "Downloading updates";
    if (phase == "install") return "Installing packages";
    if (phase == "configure") return "Configuring packages";
    if (phase == "finalize") return "Finishing installation";
    if (phase == "verify") return "Verifying final package state";
    if (phase == "complete") return "Update transaction complete";
    if (phase == "error") return "Update transaction failed";
    return "Processing updates";
}

void update_nav_updates_badge(WindowState *state)
{
    if (state == nullptr || state->nav_updates_badge == nullptr) {
        return;
    }

    if (state->updates.inventory.updates_busy &&
        (state->updates.external.external_updates_active ||
         !state->updates.transaction.updates_progress_token.empty())) {
        gtk_label_set_text(
            GTK_LABEL(state->nav_updates_badge), "↻");
        gtk_widget_add_css_class(
            state->nav_updates_badge, "nav-badge-active");
        gtk_widget_set_visible(
            state->nav_updates_badge, true);
        return;
    }

    gtk_widget_remove_css_class(
        state->nav_updates_badge, "nav-badge-active");
    const std::size_t count = state->updates.inventory.update_records.size();
    if (count == 0U) {
        gtk_widget_set_visible(
            state->nav_updates_badge, false);
        return;
    }

    const std::string badge =
        count > 99U ? "99+" : std::to_string(count);
    gtk_label_set_text(
        GTK_LABEL(state->nav_updates_badge),
        badge.c_str());
    gtk_widget_set_visible(
        state->nav_updates_badge, true);
}

void set_update_transaction_phase(
    WindowState *state,
    const std::string_view phase,
    const std::string_view detail)
{
    if (state == nullptr) {
        return;
    }

    state->updates.transaction.updates_progress_phase = std::string(phase);
    if (state->updates.transaction.updates_transaction_panel != nullptr) {
        gtk_widget_set_visible(
            state->updates.transaction.updates_transaction_panel, true);
    }
    if (state->updates.transaction.updates_transaction_phase != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->updates.transaction.updates_transaction_phase),
            update_phase_title(phase));
    }
    if (state->updates.transaction.updates_transaction_detail != nullptr) {
        std::string copy(detail);
        if (copy.empty()) {
            if (phase == "authorize") {
                copy =
                    "Approve the exact package transaction when the administrator prompt appears.";
            } else if (phase == "refresh") {
                copy =
                    "Refreshing trusted repository metadata before any package changes are applied.";
            } else if (phase == "validate") {
                copy =
                    "Checking that the reviewed package versions still match the transaction to be executed.";
            } else if (phase == "download") {
                copy =
                    "Downloading the approved package payloads.";
            } else if (phase == "install") {
                copy =
                    "Applying the downloaded package payloads.";
            } else if (phase == "configure") {
                copy =
                    "Configuring installed packages and services.";
            } else if (phase == "finalize") {
                copy =
                    "Processing package triggers and finishing the transaction.";
            } else if (phase == "verify") {
                copy =
                    "Reading authoritative installed state and checking which updates remain.";
            } else if (phase == "complete") {
                copy =
                    "The approved transaction and final package-state verification completed successfully.";
            }
        }
        gtk_label_set_text(
            GTK_LABEL(state->updates.transaction.updates_transaction_detail),
            copy.c_str());
    }

    const int active = update_phase_index(phase);
    for (int index = 0; index < 6; ++index) {
        GtkWidget *label = state->updates.transaction.updates_stage_labels[index];
        if (label == nullptr) continue;
        gtk_widget_remove_css_class(label, "update-stage-active");
        gtk_widget_remove_css_class(label, "update-stage-done");
        gtk_widget_remove_css_class(label, "update-stage-error");
        if (active >= 0 && index < active) {
            gtk_widget_add_css_class(label, "update-stage-done");
        } else if (active >= 0 && index == active) {
            gtk_widget_add_css_class(label, "update-stage-active");
        }
    }

    if (state->updates.transaction.updates_progress != nullptr) {
        if (phase == "complete") {
            gtk_progress_bar_set_fraction(
                GTK_PROGRESS_BAR(state->updates.transaction.updates_progress), 1.0);
        } else {
            gtk_progress_bar_pulse(
                GTK_PROGRESS_BAR(state->updates.transaction.updates_progress));
        }
    }
}

void update_transaction_meta(WindowState *state)
{
    if (state == nullptr ||
        state->updates.transaction.updates_transaction_meta == nullptr ||
        state->updates.transaction.updates_progress_started_us <= 0) {
        return;
    }

    const gint64 elapsed_us =
        g_get_monotonic_time() -
        state->updates.transaction.updates_progress_started_us;
    const long long elapsed_seconds =
        static_cast<long long>(
            elapsed_us / G_USEC_PER_SEC);

    std::ostringstream meta;
    meta << state->updates.transaction.updates_progress_items
         << (state->updates.transaction.updates_progress_items == 1U
                 ? " package"
                 : " packages");
    if (state->updates.transaction.updates_progress_download_bytes > 0U) {
        meta << "  •  "
             << display_size(
                    state->updates.transaction.updates_progress_download_bytes)
             << " planned download";
    }
    meta << "  •  "
         << elapsed_seconds
         << " s elapsed";
    gtk_label_set_text(
        GTK_LABEL(state->updates.transaction.updates_transaction_meta),
        meta.str().c_str());
}

gboolean update_progress_tick(gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    if (state == nullptr ||
        !state->updates.inventory.updates_busy ||
        state->updates.transaction.updates_progress == nullptr) {
        if (state != nullptr) {
            state->updates.transaction.updates_progress_timer_id = 0U;
        }
        return G_SOURCE_REMOVE;
    }

    if (state->updates.transaction.updates_post_install_refresh) {
        set_update_transaction_phase(
            state,
            "verify",
            "Installation finished; checking installed versions and remaining update candidates.");
    } else {
        const auto progress =
            read_privileged_update_progress();
        if (progress.has_value() &&
            progress->token == state->updates.transaction.updates_progress_token) {
            set_update_transaction_phase(
                state,
                progress->phase,
                progress->detail);
        } else {
            gtk_progress_bar_pulse(
                GTK_PROGRESS_BAR(state->updates.transaction.updates_progress));
        }
    }

    update_transaction_meta(state);
    update_nav_updates_badge(state);
    return G_SOURCE_CONTINUE;
}

void start_update_progress(
    WindowState *state,
    const TransactionPlan &plan)
{
    if (state == nullptr ||
        state->updates.transaction.updates_progress == nullptr) {
        return;
    }

    state->updates.transaction.updates_progress_started_us =
        g_get_monotonic_time();
    state->updates.transaction.updates_progress_items =
        plan.items.size();
    state->updates.transaction.updates_progress_download_bytes =
        plan.download_bytes;
    state->updates.transaction.updates_progress_token =
        std::to_string(
            static_cast<unsigned long long>(getpid())) +
        "-" +
        std::to_string(
            static_cast<unsigned long long>(
                state->updates.transaction.updates_progress_started_us));
    if (state->updates.transaction.updates_transaction_panel != nullptr) {
        gtk_widget_set_visible(
            state->updates.transaction.updates_transaction_panel, true);
    }
    gtk_widget_set_visible(
        state->updates.transaction.updates_progress, true);
    gtk_progress_bar_set_fraction(
        GTK_PROGRESS_BAR(state->updates.transaction.updates_progress), 0.0);
    gtk_progress_bar_set_pulse_step(
        GTK_PROGRESS_BAR(state->updates.transaction.updates_progress), 0.06);
    set_update_transaction_phase(
        state,
        "authorize",
        "Waiting for administrator authorization before the reviewed transaction begins.");
    update_transaction_meta(state);
    update_nav_updates_badge(state);

    if (state->updates.transaction.updates_progress_timer_id == 0U) {
        state->updates.transaction.updates_progress_timer_id =
            g_timeout_add_seconds(
                1U, update_progress_tick, state);
    }
}

void stop_update_progress(WindowState *state)
{
    if (state == nullptr) {
        return;
    }

    if (state->updates.transaction.updates_progress_timer_id != 0U) {
        g_source_remove(state->updates.transaction.updates_progress_timer_id);
        state->updates.transaction.updates_progress_timer_id = 0U;
    }
    state->updates.transaction.updates_progress_started_us = 0;
    state->updates.transaction.updates_progress_items = 0U;
    state->updates.transaction.updates_progress_download_bytes = 0U;
    state->updates.transaction.updates_progress_token.clear();
    state->updates.transaction.updates_progress_phase.clear();
    if (state->updates.transaction.updates_transaction_panel != nullptr) {
        gtk_widget_set_visible(
            state->updates.transaction.updates_transaction_panel, false);
    }
    update_nav_updates_badge(state);
}

void finish_update_progress(
    WindowState *state,
    const bool success,
    const std::string_view detail)
{
    if (state == nullptr) {
        return;
    }

    if (state->updates.transaction.updates_progress_timer_id != 0U) {
        g_source_remove(state->updates.transaction.updates_progress_timer_id);
        state->updates.transaction.updates_progress_timer_id = 0U;
    }
    if (state->updates.transaction.updates_transaction_panel != nullptr) {
        gtk_widget_set_visible(
            state->updates.transaction.updates_transaction_panel, true);
    }
    if (success) {
        set_update_transaction_phase(
            state, "complete", detail);
    } else {
        const int active =
            update_phase_index(
                state->updates.transaction.updates_progress_phase);
        state->updates.transaction.updates_progress_phase = "error";
        if (state->updates.transaction.updates_transaction_phase != nullptr) {
            gtk_label_set_text(
                GTK_LABEL(state->updates.transaction.updates_transaction_phase),
                "Update transaction failed");
        }
        if (state->updates.transaction.updates_transaction_detail != nullptr) {
            const std::string copy(detail);
            gtk_label_set_text(
                GTK_LABEL(state->updates.transaction.updates_transaction_detail),
                copy.c_str());
        }
        if (state->updates.transaction.updates_progress != nullptr) {
            gtk_progress_bar_set_fraction(
                GTK_PROGRESS_BAR(state->updates.transaction.updates_progress), 0.0);
        }
        if (active >= 0 && active < 6 &&
            state->updates.transaction.updates_stage_labels[active] != nullptr) {
            gtk_widget_remove_css_class(
                state->updates.transaction.updates_stage_labels[active],
                "update-stage-active");
            gtk_widget_add_css_class(
                state->updates.transaction.updates_stage_labels[active],
                "update-stage-error");
        }
    }
    update_transaction_meta(state);
    state->updates.transaction.updates_progress_token.clear();
    update_nav_updates_badge(state);
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

    const std::filesystem::path temporary =
        path.string() + ".tmp." +
        std::to_string(
            static_cast<unsigned long long>(getpid()));
    std::ofstream output(
        temporary,
        std::ios::out | std::ios::trunc);
    if (!output) {
        return;
    }
    output << value << '\n';
    output.close();
    if (!output) {
        std::filesystem::remove(temporary, ec);
        return;
    }

    std::filesystem::rename(
        temporary,
        path,
        ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
    }
}

std::string update_identity(const PackageRecord &package)
{
    return package.package_name.empty()
        ? package.id
        : package.package_name;
}

void update_selection_controls(WindowState *state)
{
    if (state == nullptr || state->updates.updates_install == nullptr) {
        return;
    }

    const std::size_t selected = state->updates.inventory.selected_update_ids.size();
    if (selected == 0U) {
        gtk_button_set_label(
            GTK_BUTTON(state->updates.updates_install),
            "Install selected updates");
    } else if (selected == state->updates.inventory.update_records.size()) {
        gtk_button_set_label(
            GTK_BUTTON(state->updates.updates_install),
            state->updates.inventory.update_records.size() == 1U
                ? "Install update"
                : "Install all updates");
    } else {
        const std::string label =
            "Install " + std::to_string(selected) + " selected";
        gtk_button_set_label(
            GTK_BUTTON(state->updates.updates_install), label.c_str());
    }

    gtk_widget_set_sensitive(
        state->updates.updates_install,
        !state->updates.inventory.updates_busy && selected != 0U);
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
        state->updates.inventory.selected_update_ids.insert(identity);
    } else {
        state->updates.inventory.selected_update_ids.erase(identity);
    }
    update_selection_controls(state);
}

void select_security_updates(GtkButton *, gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    if (state == nullptr || state->updates.inventory.updates_busy) {
        return;
    }

    state->updates.inventory.selected_update_ids.clear();
    for (const PackageRecord &package : state->updates.inventory.update_records) {
        if (!package.security_update) {
            continue;
        }
        const std::string identity = update_identity(package);
        if (!identity.empty()) {
            state->updates.inventory.selected_update_ids.insert(identity);
        }
    }

    rebuild_updates(state);
    update_selection_controls(state);
}

void select_kernel_updates(GtkButton *, gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    if (state == nullptr || state->updates.inventory.updates_busy) {
        return;
    }

    state->updates.inventory.selected_update_ids.clear();
    for (const PackageRecord &package : state->updates.inventory.update_records) {
        if (package.kind !=
            infiltrator::software::PackageKind::kernel) {
            continue;
        }
        const std::string identity = update_identity(package);
        if (!identity.empty()) {
            state->updates.inventory.selected_update_ids.insert(identity);
        }
    }

    rebuild_updates(state);
    update_selection_controls(state);
}

struct ChangelogTaskData {
    PackageRecord package;
};

struct ChangelogResult {
    std::string text;
    std::string source;
    std::string error;
};

std::size_t changelog_write(
    char *data,
    const std::size_t size,
    const std::size_t count,
    void *user_data)
{
    auto *text =
        static_cast<std::string *>(user_data);
    const std::size_t bytes = size * count;
    if (text == nullptr ||
        bytes > 2U * 1024U * 1024U ||
        text->size() >
            2U * 1024U * 1024U - bytes) {
        return 0U;
    }
    text->append(data, bytes);
    return bytes;
}

bool fetch_changelog_url(
    const std::string &url,
    std::string &text)
{
    CURL *curl = curl_easy_init();
    if (curl == nullptr) {
        return false;
    }

    text.clear();
    curl_easy_setopt(
        curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(
        curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(
        curl, CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(
        curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(
        curl, CURLOPT_USERAGENT,
        "Infiltrator-Software/" INFILTRATOR_SOFTWARE_VERSION);
    curl_easy_setopt(
        curl, CURLOPT_WRITEFUNCTION,
        changelog_write);
    curl_easy_setopt(
        curl, CURLOPT_WRITEDATA, &text);

    const CURLcode result =
        curl_easy_perform(curl);
    long response = 0;
    (void)curl_easy_getinfo(
        curl,
        CURLINFO_RESPONSE_CODE,
        &response);
    curl_easy_cleanup(curl);

    return result == CURLE_OK &&
           response >= 200L &&
           response < 300L &&
           !text.empty();
}

std::optional<std::string> ppa_pool_base(
    const std::string &origin)
{
    if (origin.rfind("LP-PPA-", 0U) != 0U) {
        return std::nullopt;
    }

    std::vector<std::filesystem::path> files{
        "/etc/apt/sources.list"};
    std::error_code ec;
    const std::filesystem::path directory{
        "/etc/apt/sources.list.d"};
    if (std::filesystem::is_directory(
            directory, ec) &&
        !ec) {
        for (const auto &entry :
             std::filesystem::directory_iterator(
                 directory, ec)) {
            if (ec) {
                break;
            }
            if (entry.is_regular_file(ec) &&
                !ec) {
                files.push_back(
                    entry.path());
            }
            ec.clear();
        }
    }

    for (const auto &path : files) {
        std::ifstream input(path);
        std::string line;
        while (std::getline(input, line)) {
            const std::size_t host =
                line.find(
                    "ppa.launchpadcontent.net/");
            const std::size_t legacy =
                line.find(
                    "ppa.launchpad.net/");
            const std::size_t start =
                host != std::string::npos
                    ? host +
                          std::strlen(
                              "ppa.launchpadcontent.net/")
                    : legacy != std::string::npos
                        ? legacy +
                              std::strlen(
                                  "ppa.launchpad.net/")
                        : std::string::npos;
            if (start ==
                std::string::npos) {
                continue;
            }

            const std::size_t owner_end =
                line.find('/', start);
            if (owner_end ==
                std::string::npos) {
                continue;
            }
            const std::size_t name_end =
                line.find(
                    '/',
                    owner_end + 1U);
            if (name_end ==
                std::string::npos) {
                continue;
            }
            const std::string owner =
                line.substr(
                    start,
                    owner_end - start);
            const std::string name =
                line.substr(
                    owner_end + 1U,
                    name_end -
                        owner_end - 1U);
            if ("LP-PPA-" + owner +
                    "-" + name !=
                origin) {
                continue;
            }

            return
                "https://ppa.launchpadcontent.net/" +
                owner + "/" + name +
                "/ubuntu/pool/main";
        }
    }
    return std::nullopt;
}

std::vector<std::string> changelog_urls(
    const PackageRecord &package)
{
    std::string source =
        package.source_package.empty()
            ? package.package_name
            : package.source_package;
    if (source.empty() ||
        package.available_version.empty()) {
        return {};
    }

    std::string version =
        package.available_version;
    const std::size_t epoch = version.find(':');
    if (epoch != std::string::npos) {
        version.erase(0U, epoch + 1U);
    }

    const std::string prefix =
        source.rfind("lib", 0U) == 0U &&
        source.size() >= 4U
            ? source.substr(0U, 4U)
            : source.substr(0U, 1U);

    std::string origin =
        package.repository_origin;
    std::transform(
        origin.begin(),
        origin.end(),
        origin.begin(),
        [](const unsigned char ch) {
            return static_cast<char>(
                std::tolower(ch));
        });

    std::vector<std::string> urls;
    if (origin.find("ubuntu") !=
        std::string::npos) {
        if (source == "linux" ||
            source.rfind("linux-", 0U) == 0U) {
            const std::string kernel =
                version.substr(
                    0U,
                    version.find('~'));
            urls.push_back(
                "https://changelogs.ubuntu.com/changelogs/pool/main/l/linux/linux_" +
                kernel + "/changelog");
        } else {
            for (const char *component :
                 {"main", "universe",
                  "multiverse", "restricted"}) {
                urls.push_back(
                    "https://changelogs.ubuntu.com/changelogs/pool/" +
                    std::string(component) + "/" +
                    prefix + "/" + source + "/" +
                    source + "_" + version +
                    "/changelog");
            }
        }
    } else if (
        origin.find("debian") !=
        std::string::npos) {
        for (const char *component :
             {"main", "contrib",
              "non-free", "non-free-firmware"}) {
            urls.push_back(
                "https://metadata.ftp-master.debian.org/changelogs/" +
                std::string(component) + "/" +
                prefix + "/" + source + "/" +
                source + "_" + version +
                "_changelog");
        }
    } else if (
        origin.find("linuxmint") !=
        std::string::npos) {
        urls.push_back(
            "https://packages.linuxmint.com/dev/" +
            source + "_" + version +
            "_amd64.changes");
        urls.push_back(
            "https://packages.linuxmint.com/dev/" +
            source + "_" + version +
            "_i386.changes");
    } else if (
        package.repository_origin.rfind(
            "LP-PPA-", 0U) == 0U) {
        const auto pool =
            ppa_pool_base(
                package.repository_origin);
        if (pool.has_value()) {
            urls.push_back(
                *pool + "/" +
                prefix + "/" + source +
                "/" + source + "_" +
                version +
                "_source.changes");
            urls.push_back(
                *pool + "/" +
                prefix + "/" + source +
                "/" + source + "_" +
                version +
                "_amd64.changes");
        }
    }
    return urls;
}

void changelog_worker(
    GTask *task,
    gpointer,
    gpointer task_data,
    GCancellable *)
{
    auto *data =
        static_cast<ChangelogTaskData *>(
            task_data);
    auto *result = new ChangelogResult{};
    if (data == nullptr) {
        result->error =
            "Update metadata is unavailable.";
    } else {
        for (const std::string &url :
             changelog_urls(data->package)) {
            if (fetch_changelog_url(
                    url, result->text)) {
                result->source = url;
                break;
            }
        }
        if (result->text.empty()) {
            result->error =
                "No published changelog could be retrieved for this update.";
        }
    }

    g_task_return_pointer(
        task,
        result,
        [](gpointer pointer) {
            delete static_cast<ChangelogResult *>(
                pointer);
        });
}

void changelog_complete(
    GObject *source_object,
    GAsyncResult *async_result,
    gpointer)
{
    GtkWidget *view =
        GTK_WIDGET(source_object);
    auto *result =
        static_cast<ChangelogResult *>(
            g_task_propagate_pointer(
                G_TASK(async_result),
                nullptr));
    if (view == nullptr ||
        result == nullptr) {
        delete result;
        return;
    }

    GtkTextBuffer *buffer =
        gtk_text_view_get_buffer(
            GTK_TEXT_VIEW(view));
    const std::string text =
        result->error.empty()
            ? result->text
            : result->error;
    gtk_text_buffer_set_text(
        buffer,
        text.c_str(),
        -1);
    if (!result->source.empty()) {
        gtk_widget_set_tooltip_text(
            view,
            result->source.c_str());
    }
    delete result;
}

void update_changelog_clicked(
    GtkButton *button,
    gpointer user_data)
{
    auto *state =
        static_cast<WindowState *>(user_data);
    auto *package =
        static_cast<PackageRecord *>(
            g_object_get_data(
                G_OBJECT(button),
                "update-package"));
    if (state == nullptr ||
        state->window == nullptr ||
        package == nullptr) {
        return;
    }

    GtkWidget *window =
        gtk_window_new();
    gtk_window_set_title(
        GTK_WINDOW(window),
        "Update changelog");
    gtk_window_set_transient_for(
        GTK_WINDOW(window),
        state->window);
    gtk_window_set_destroy_with_parent(
        GTK_WINDOW(window),
        true);
    gtk_window_set_default_size(
        GTK_WINDOW(window),
        760, 620);

    GtkWidget *scroll =
        gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_AUTOMATIC,
        GTK_POLICY_AUTOMATIC);
    GtkWidget *view =
        gtk_text_view_new();
    gtk_text_view_set_editable(
        GTK_TEXT_VIEW(view),
        false);
    gtk_text_view_set_cursor_visible(
        GTK_TEXT_VIEW(view),
        false);
    gtk_text_view_set_wrap_mode(
        GTK_TEXT_VIEW(view),
        GTK_WRAP_WORD_CHAR);
    gtk_text_buffer_set_text(
        gtk_text_view_get_buffer(
            GTK_TEXT_VIEW(view)),
        "Downloading changelog…",
        -1);
    gtk_scrolled_window_set_child(
        GTK_SCROLLED_WINDOW(scroll),
        view);
    gtk_window_set_child(
        GTK_WINDOW(window),
        scroll);
    gtk_window_present(
        GTK_WINDOW(window));

    auto *data =
        new ChangelogTaskData{*package};
    GTask *task =
        g_task_new(
            G_OBJECT(view),
            nullptr,
            changelog_complete,
            nullptr);
    g_task_set_task_data(
        task,
        data,
        [](gpointer pointer) {
            delete static_cast<ChangelogTaskData *>(
                pointer);
        });
    g_task_run_in_thread(
        task,
        changelog_worker);
    g_object_unref(task);
}

struct UpdateDetailsContext {
    WindowState *state{};
    PackageRecord package;
    GtkWidget *status{};
};

void destroy_update_details_context(gpointer data)
{
    delete static_cast<UpdateDetailsContext *>(data);
}

void ignore_update_clicked(GtkButton *button, gpointer user_data)
{
    auto *context = static_cast<UpdateDetailsContext *>(user_data);
    if (context == nullptr || context->state == nullptr) {
        return;
    }

    std::string identity =
        context->package.source_package.empty()
            ? context->package.package_name
            : context->package.source_package;
    if (identity.empty()) {
        identity = context->package.id;
    }
    if (identity.empty()) {
        return;
    }

    const bool version_only =
        button != nullptr &&
        GPOINTER_TO_INT(
            g_object_get_data(
                G_OBJECT(button),
                "ignore-version-only")) != 0;
    std::string rule = identity;
    if (version_only &&
        !context->package.available_version.empty()) {
        rule += "=" +
            context->package.available_version;
    }

    auto &rules = context->state->preferences.ignored_packages;
    if (std::find(rules.begin(), rules.end(), rule) ==
        rules.end()) {
        rules.push_back(rule);
    }

    std::string error;
    if (!save_software_preferences(
            context->state->preferences,
            error)) {
        if (context->status != nullptr) {
            gtk_label_set_text(
                GTK_LABEL(context->status),
                error.c_str());
        }
        return;
    }

    auto &updates = context->state->updates.inventory.update_records;
    updates.erase(
        std::remove_if(
            updates.begin(), updates.end(),
            [&](const PackageRecord &item) {
                return update_is_ignored(
                    item, context->state->preferences);
            }),
        updates.end());

    context->state->updates.inventory.selected_update_ids.clear();
    for (const PackageRecord &item : updates) {
        const std::string key = update_identity(item);
        if (!key.empty()) {
            context->state->updates.inventory.selected_update_ids.insert(key);
        }
    }

    rebuild_updates(context->state);
    update_selection_controls(context->state);

    if (context->status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(context->status),
            version_only
                ? "This update version is ignored. A later version will appear normally."
                : "All future updates for this source package are ignored. Remove the rule in Software Preferences to restore them.");
    }
}

void update_details_clicked(GtkButton *button, gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    auto *package = static_cast<PackageRecord *>(
        g_object_get_data(
            G_OBJECT(button),
            "update-package"));
    if (state == nullptr || state->window == nullptr ||
        package == nullptr) {
        return;
    }

    GtkWidget *window = gtk_window_new();
    gtk_window_set_title(
        GTK_WINDOW(window),
        "Update details");
    gtk_window_set_transient_for(
        GTK_WINDOW(window),
        state->window);
    gtk_window_set_modal(
        GTK_WINDOW(window), true);
    gtk_window_set_default_size(
        GTK_WINDOW(window), 620, 520);

    GtkWidget *box =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(
        box, "preferences-page");
    gtk_widget_set_margin_top(box, 18);
    gtk_widget_set_margin_bottom(box, 18);
    gtk_widget_set_margin_start(box, 18);
    gtk_widget_set_margin_end(box, 18);

    std::string title = package->name;
    if (package->security_update) {
        title += "  •  Security update";
    }
    gtk_box_append(
        GTK_BOX(box),
        make_label(
            title.c_str(),
            "preferences-title"));

    if (!package->description.empty()) {
        GtkWidget *description =
            make_label(
                package->description.c_str(),
                "preferences-copy");
        gtk_label_set_wrap(
            GTK_LABEL(description), true);
        gtk_box_append(
            GTK_BOX(box), description);
    }

    gtk_box_append(
        GTK_BOX(box),
        detail_row(
            "Installed",
            package->installed_version));
    gtk_box_append(
        GTK_BOX(box),
        detail_row(
            "Available",
            package->available_version));

    const std::string source_identity =
        package->source_package.empty()
            ? package->package_name
            : package->source_package;
    gtk_box_append(
        GTK_BOX(box),
        detail_row(
            "Source package",
            source_identity));
    gtk_box_append(
        GTK_BOX(box),
        detail_row(
            "Repository",
            package->repository_origin));
    gtk_box_append(
        GTK_BOX(box),
        detail_row(
            "Site",
            package->repository_site));
    gtk_box_append(
        GTK_BOX(box),
        detail_row(
            "Download",
            display_size(
                package->download_size_bytes)));

    std::string binaries;
    for (const PackageRecord &candidate :
         state->updates.inventory.update_records) {
        const std::string candidate_source =
            candidate.source_package.empty()
                ? candidate.package_name
                : candidate.source_package;
        if (candidate_source != source_identity) {
            continue;
        }

        if (!binaries.empty()) {
            binaries += ", ";
        }
        binaries += candidate.package_name;
    }
    gtk_box_append(
        GTK_BOX(box),
        detail_row(
            "Binary packages",
            binaries));

    auto *context = new UpdateDetailsContext{
        state, *package, nullptr};

    GtkWidget *changelog =
        gtk_button_new_with_label(
            "Changelog");
    gtk_widget_set_halign(
        changelog, GTK_ALIGN_START);
    g_object_set_data_full(
        G_OBJECT(changelog),
        "update-package",
        new PackageRecord(*package),
        package_record_destroy);
    g_signal_connect(
        changelog,
        "clicked",
        G_CALLBACK(update_changelog_clicked),
        state);
    gtk_box_append(
        GTK_BOX(box), changelog);

    GtkWidget *ignore_version =
        gtk_button_new_with_label(
            "Ignore this update version");
    gtk_widget_set_halign(
        ignore_version, GTK_ALIGN_START);
    g_object_set_data(
        G_OBJECT(ignore_version),
        "ignore-version-only",
        GINT_TO_POINTER(1));
    g_signal_connect(
        ignore_version,
        "clicked",
        G_CALLBACK(ignore_update_clicked),
        context);
    gtk_box_append(
        GTK_BOX(box), ignore_version);

    GtkWidget *ignore =
        gtk_button_new_with_label(
            "Ignore all future updates for this package");
    gtk_widget_set_halign(
        ignore, GTK_ALIGN_START);
    g_object_set_data(
        G_OBJECT(ignore),
        "ignore-version-only",
        GINT_TO_POINTER(0));
    g_signal_connect(
        ignore,
        "clicked",
        G_CALLBACK(ignore_update_clicked),
        context);
    gtk_box_append(
        GTK_BOX(box), ignore);

    context->status =
        make_label(
            "",
            "preferences-copy");
    gtk_box_append(
        GTK_BOX(box), context->status);

    g_object_set_data_full(
        G_OBJECT(window),
        "update-details-context",
        context,
        destroy_update_details_context);

    gtk_window_set_child(
        GTK_WINDOW(window), box);
    gtk_window_present(
        GTK_WINDOW(window));
}


GtkWidget *make_update_row(
    WindowState *state,
    const PackageRecord &package)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(row, "package-row");
    gtk_widget_add_css_class(row, "update-item");
    gtk_widget_set_margin_top(row, 2);
    gtk_widget_set_margin_bottom(row, 2);
    gtk_widget_set_margin_start(row, 4);
    gtk_widget_set_margin_end(row, 4);

    const std::string identity_key = update_identity(package);
    GtkWidget *selected = gtk_check_button_new();
    gtk_widget_add_css_class(selected, "update-selector");
    gtk_widget_set_tooltip_text(
        selected, "Include this package in the update");
    gtk_widget_set_valign(selected, GTK_ALIGN_CENTER);
    gtk_check_button_set_active(
        GTK_CHECK_BUTTON(selected),
        state != nullptr &&
            state->updates.inventory.selected_update_ids.find(identity_key) !=
                state->updates.inventory.selected_update_ids.end());
    g_object_set_data_full(
        G_OBJECT(selected),
        "update-identity",
        g_strdup(identity_key.c_str()),
        g_free);
    g_signal_connect(
        selected, "toggled",
        G_CALLBACK(update_selection_toggled), state);
    gtk_box_append(GTK_BOX(row), selected);

    GtkWidget *icon_well =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(
        icon_well, "update-icon-well");
    gtk_widget_set_size_request(icon_well, 40, 40);
    GtkWidget *icon =
        make_icon(update_icon_name(package), 22);
    gtk_widget_add_css_class(icon, "package-icon");
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(icon_well), icon);
    gtk_box_append(GTK_BOX(row), icon_well);

    GtkWidget *identity =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_set_hexpand(identity, true);

    GtkWidget *title_line =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 7);
    GtkWidget *name =
        make_label(package.name.c_str(), "update-name");
    gtk_label_set_ellipsize(
        GTK_LABEL(name), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(name, true);
    gtk_box_append(GTK_BOX(title_line), name);

    const std::string kind_text(
        infiltrator::software::package_kind_name(
            package.kind));
    GtkWidget *kind =
        make_label(
            kind_text.c_str(),
            "update-kind-chip");
    gtk_box_append(GTK_BOX(title_line), kind);

    GtkWidget *recommended =
        make_label(
            package.system_critical
                ? "System-critical"
                : "Recommended",
            package.system_critical
                ? "state-warning"
                : "state-available");
    gtk_widget_add_css_class(
        recommended, "update-recommendation-chip");
    gtk_widget_set_valign(
        recommended, GTK_ALIGN_CENTER);
    gtk_box_append(
        GTK_BOX(title_line), recommended);

    if (package.security_update) {
        GtkWidget *security =
            make_label(
                "SECURITY",
                "update-security-chip");
        gtk_box_append(
            GTK_BOX(title_line),
            security);
    }

    GtkWidget *details =
        gtk_button_new_with_label("Details");
    gtk_widget_add_css_class(
        details, "update-details-button");
    gtk_widget_set_valign(
        details, GTK_ALIGN_CENTER);
    auto *package_copy =
        new PackageRecord(package);
    g_object_set_data_full(
        G_OBJECT(details),
        "update-package",
        package_copy,
        package_record_destroy);
    g_signal_connect(
        details,
        "clicked",
        G_CALLBACK(update_details_clicked),
        state);
    gtk_box_append(GTK_BOX(title_line), details);

    gtk_box_append(GTK_BOX(identity), title_line);

    std::string source_name = package.repository_origin;
    if (source_name.empty()) {
        source_name = package.repository_site;
    }
    if (source_name.empty()) {
        source_name = package.source;
    }
    if (source_name.empty()) {
        source_name = "Repository";
    }

    GtkWidget *meta_line =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 7);
    GtkWidget *from =
        make_label(
            package.installed_version.empty()
                ? "Not installed"
                : package.installed_version.c_str(),
            "version-chip");
    GtkWidget *arrow =
        make_label("→", "version-arrow", 0.5F);
    GtkWidget *to =
        make_label(
            package.available_version.c_str(),
            "version-chip-new");
    gtk_box_append(GTK_BOX(meta_line), from);
    gtk_box_append(GTK_BOX(meta_line), arrow);
    gtk_box_append(GTK_BOX(meta_line), to);
    gtk_box_append(
        GTK_BOX(meta_line),
        make_label("•", "update-meta-separator", 0.5F));

    GtkWidget *source_icon =
        make_icon("network-workgroup-symbolic", 13);
    gtk_widget_add_css_class(
        source_icon, "update-source-icon");
    GtkWidget *source =
        make_label(
            source_name.c_str(), "update-source");
    gtk_label_set_ellipsize(
        GTK_LABEL(source), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(source, true);
    gtk_box_append(GTK_BOX(meta_line), source_icon);
    gtk_box_append(GTK_BOX(meta_line), source);

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

    std::string explanation =
        "Selection policy: " + policy_name;
    if (package.candidate_priority != 0) {
        explanation +=
            " • priority " +
            std::to_string(package.candidate_priority);
    }
    if (!package.policy_reason.empty()) {
        explanation += " • " + package.policy_reason;
    }
    if (!package.selection_reason.empty()) {
        explanation += " " + package.selection_reason;
    }
    gtk_widget_set_tooltip_text(
        meta_line, explanation.c_str());

    gtk_box_append(GTK_BOX(identity), meta_line);
    gtk_box_append(GTK_BOX(row), identity);

    return row;
}

int update_group_index(const PackageRecord &package) noexcept
{
    if (package.system_critical ||
        package.kind == infiltrator::software::PackageKind::kernel ||
        package.kind == infiltrator::software::PackageKind::driver ||
        package.kind == infiltrator::software::PackageKind::system) {
        return 0;
    }
    if (package.kind ==
        infiltrator::software::PackageKind::application) {
        return 1;
    }
    if (package.kind ==
            infiltrator::software::PackageKind::library ||
        package.kind ==
            infiltrator::software::PackageKind::runtime) {
        return 2;
    }
    return 3;
}

const char *update_group_title(const int group) noexcept
{
    switch (group) {
    case 0: return "System";
    case 1: return "Applications";
    case 2: return "Libraries & runtimes";
    default: return "Other software";
    }
}

const char *update_group_subtitle(const int group) noexcept
{
    switch (group) {
    case 0:
        return "Kernels, drivers and operating-system components";
    case 1:
        return "Desktop applications and user-facing tools";
    case 2:
        return "Shared components used by installed software";
    default:
        return "Additional package updates";
    }
}

const char *update_group_icon(const int group) noexcept
{
    switch (group) {
    case 0: return "computer-symbolic";
    case 1: return "view-grid-symbolic";
    case 2: return "applications-system-symbolic";
    default: return "package-x-generic-symbolic";
    }
}

GtkWidget *make_update_group_header(
    const int group,
    const std::size_t count)
{
    GtkWidget *row =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(
        row, "update-group-header");

    GtkWidget *icon_well =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(
        icon_well, "update-group-icon-well");
    GtkWidget *icon =
        make_icon(update_group_icon(group), 20);
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(icon_well), icon);
    gtk_box_append(GTK_BOX(row), icon_well);

    GtkWidget *copy =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gtk_widget_set_hexpand(copy, true);
    GtkWidget *title =
        make_label(
            update_group_title(group),
            "update-group-title");
    GtkWidget *subtitle =
        make_label(
            update_group_subtitle(group),
            "update-group-subtitle");
    gtk_box_append(GTK_BOX(copy), title);
    gtk_box_append(GTK_BOX(copy), subtitle);
    gtk_box_append(GTK_BOX(row), copy);

    const std::string count_text =
        std::to_string(count);
    GtkWidget *count_label =
        make_label(
            count_text.c_str(),
            "update-group-count");
    gtk_widget_set_valign(
        count_label, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(row), count_label);
    return row;
}

void rebuild_updates(WindowState *state)
{
    if (state == nullptr || state->updates.updates_list == nullptr) {
        return;
    }

    GtkWidget *child =
        gtk_widget_get_first_child(
            GTK_WIDGET(state->updates.updates_list));
    while (child != nullptr) {
        GtkWidget *next =
            gtk_widget_get_next_sibling(child);
        gtk_list_box_remove(
            state->updates.updates_list, child);
        child = next;
    }

    std::size_t critical_count = 0U;
    std::size_t group_counts[4] = {0U, 0U, 0U, 0U};
    for (const PackageRecord &package :
         state->updates.inventory.update_records) {
        if (package.system_critical) {
            ++critical_count;
        }
        ++group_counts[
            update_group_index(package)];
    }

    for (int group = 0; group < 4; ++group) {
        if (group_counts[group] == 0U) {
            continue;
        }

        GtkWidget *header_row =
            gtk_list_box_row_new();
        gtk_widget_add_css_class(
            header_row, "update-group-row");
        gtk_list_box_row_set_activatable(
            GTK_LIST_BOX_ROW(header_row), false);
        gtk_list_box_row_set_selectable(
            GTK_LIST_BOX_ROW(header_row), false);
        gtk_list_box_row_set_child(
            GTK_LIST_BOX_ROW(header_row),
            make_update_group_header(
                group, group_counts[group]));
        gtk_list_box_append(
            state->updates.updates_list, header_row);

        for (const PackageRecord &package :
             state->updates.inventory.update_records) {
            if (update_group_index(package) != group) {
                continue;
            }
            GtkWidget *row =
                gtk_list_box_row_new();
            gtk_list_box_row_set_child(
                GTK_LIST_BOX_ROW(row),
                make_update_row(state, package));
            gtk_list_box_append(
                state->updates.updates_list, row);
        }
    }

    if (state->updates.updates_count != nullptr) {
        const std::string count =
            std::to_string(
                state->updates.inventory.update_records.size());
        gtk_label_set_text(
            GTK_LABEL(state->updates.updates_count),
            count.c_str());
    }
    if (state->updates.updates_critical != nullptr) {
        const std::string critical =
            std::to_string(critical_count);
        gtk_label_set_text(
            GTK_LABEL(state->updates.updates_critical),
            critical.c_str());
    }
}

std::string flatpak_selection_key(const ExternalUpdate &update)
{
    return std::string(update.user_installation ? "user:" : "system:") +
        update.ref;
}

std::string cinnamon_selection_key(const ExternalUpdate &update)
{
    return std::string(
               external_update_kind_name(update.kind)) +
        ":" + update.id;
}

GtkWidget *make_external_update_row(
    WindowState *state,
    const ExternalUpdate &update)
{
    GtkWidget *row =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(row, "package-row");
    gtk_widget_add_css_class(row, "external-update-row");

    const bool flatpak = update.backend == "Flatpak";
    const std::string key =
        flatpak
            ? flatpak_selection_key(update)
            : cinnamon_selection_key(update);
    GtkWidget *check = gtk_check_button_new();
    gtk_check_button_set_active(
        GTK_CHECK_BUTTON(check),
        flatpak
            ? state->updates.external.selected_flatpak_refs.count(key) != 0U
            : state->updates.external.selected_cinnamon_refs.count(key) != 0U);
    g_object_set_data_full(
        G_OBJECT(check),
        "external-selection-key",
        new std::string(key),
        +[](gpointer pointer) {
            delete static_cast<std::string *>(pointer);
        });
    g_object_set_data(
        G_OBJECT(check),
        "external-selection-flatpak",
        GINT_TO_POINTER(flatpak));
    g_signal_connect(
        check,
        "toggled",
        G_CALLBACK(+[](GtkCheckButton *button, gpointer user_data) {
            auto *window_state =
                static_cast<WindowState *>(user_data);
            const auto *selection_key =
                static_cast<const std::string *>(
                    g_object_get_data(
                        G_OBJECT(button),
                        "external-selection-key"));
            if (window_state == nullptr ||
                selection_key == nullptr) {
                return;
            }

            const bool is_flatpak =
                GPOINTER_TO_INT(
                    g_object_get_data(
                        G_OBJECT(button),
                        "external-selection-flatpak")) != 0;
            auto &selection =
                is_flatpak
                    ? window_state->updates.external.selected_flatpak_refs
                    : window_state->updates.external.selected_cinnamon_refs;
            if (gtk_check_button_get_active(button)) {
                selection.insert(*selection_key);
            } else {
                selection.erase(*selection_key);
            }

            GtkWidget *apply =
                is_flatpak
                    ? window_state->updates.external.external_flatpak_apply
                    : window_state->updates.external.external_cinnamon_apply;
            if (apply != nullptr) {
                gtk_widget_set_sensitive(
                    apply,
                    !selection.empty() &&
                        !window_state->updates.inventory.updates_busy);
            }
        }),
        state);
    gtk_widget_set_sensitive(check, !state->updates.inventory.updates_busy);
    gtk_box_append(GTK_BOX(row), check);

    const char *icon_name =
        flatpak
            ? "application-x-executable-symbolic"
            : update.kind == ExternalUpdateKind::nemo_action
                ? "system-file-manager-symbolic"
                : "preferences-desktop-symbolic";
    GtkWidget *icon_well =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(icon_well, "update-icon-well");
    gtk_widget_set_size_request(icon_well, 46, 46);
    GtkWidget *icon = make_icon(icon_name, 24);
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(icon_well), icon);
    gtk_box_append(GTK_BOX(row), icon_well);

    GtkWidget *copy =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_set_hexpand(copy, true);

    GtkWidget *title =
        make_label(
            update.name.empty()
                ? update.id.c_str()
                : update.name.c_str(),
            "update-name");
    gtk_label_set_ellipsize(
        GTK_LABEL(title),
        PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(copy), title);

    std::string detail =
        std::string(external_update_kind_name(update.kind));
    if (!update.version.empty()) {
        detail += " • " + update.version;
    }
    if (update.download_bytes > 0U) {
        detail += " • " +
            display_size(update.download_bytes);
    }
    if (!update.detail.empty()) {
        detail += " • " + update.detail;
    }
    GtkWidget *detail_label =
        make_label(detail.c_str(), "update-source");
    gtk_label_set_ellipsize(
        GTK_LABEL(detail_label),
        PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(copy), detail_label);
    gtk_box_append(GTK_BOX(row), copy);

    GtkWidget *backend =
        make_label(
            update.backend.c_str(),
            "update-kind-chip");
    gtk_widget_set_valign(backend, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(row), backend);
    return row;
}

void rebuild_external_updates(
    WindowState *state)
{
    if (state == nullptr ||
        state->updates.external.external_updates_list == nullptr) {
        return;
    }

    GtkWidget *child =
        gtk_widget_get_first_child(
            GTK_WIDGET(
                state->updates.external.external_updates_list));
    while (child != nullptr) {
        GtkWidget *next =
            gtk_widget_get_next_sibling(child);
        gtk_list_box_remove(
            state->updates.external.external_updates_list,
            child);
        child = next;
    }

    std::size_t flatpak_count = 0U;
    std::size_t cinnamon_count = 0U;
    for (const ExternalUpdate &update :
         state->updates.external.external_update_records) {
        if (update.backend == "Flatpak") {
            ++flatpak_count;
        } else if (update.backend == "Cinnamon") {
            ++cinnamon_count;
        }

        GtkWidget *row =
            gtk_list_box_row_new();
        gtk_list_box_row_set_activatable(
            GTK_LIST_BOX_ROW(row), false);
        gtk_list_box_row_set_selectable(
            GTK_LIST_BOX_ROW(row), false);
        gtk_list_box_row_set_child(
            GTK_LIST_BOX_ROW(row),
            make_external_update_row(state, update));
        gtk_list_box_append(
            state->updates.external.external_updates_list,
            row);
    }

    if (state->updates.external.external_updates_status != nullptr) {
        std::string status;
        if (state->updates.external.external_update_records.empty()) {
            status =
                "Flatpak and Cinnamon sources are up to date.";
        } else {
            status =
                std::to_string(flatpak_count) +
                " Flatpak • " +
                std::to_string(cinnamon_count) +
                " Cinnamon";
        }
        gtk_label_set_text(
            GTK_LABEL(
                state->updates.external.external_updates_status),
            status.c_str());
    }

    if (state->updates.external.external_flatpak_apply != nullptr) {
        gtk_widget_set_sensitive(
            state->updates.external.external_flatpak_apply,
            flatpak_count > 0U &&
                !state->updates.external.selected_flatpak_refs.empty() &&
                !state->updates.inventory.updates_busy);
    }
    if (state->updates.external.external_cinnamon_apply != nullptr) {
        gtk_widget_set_sensitive(
            state->updates.external.external_cinnamon_apply,
            cinnamon_count > 0U &&
                !state->updates.external.selected_cinnamon_refs.empty() &&
                !state->updates.inventory.updates_busy);
    }
}

struct ExternalApplyTaskData {
    bool flatpak{false};
    std::vector<ExternalUpdate> selected;
};

struct ExternalApplyResult {
    bool flatpak{false};
    bool success{false};
    std::vector<ExternalUpdate> selected;
    std::vector<ExternalUpdate> completed;
    std::string error;
};

struct ExternalProgressNotice {
    GtkWindow *window{};
    std::string message;
};

void publish_external_progress(GTask *task, std::string_view message)
{
    auto *queued_notice = new ExternalProgressNotice{
        GTK_WINDOW(g_object_ref(g_task_get_source_object(task))),
        std::string(message)};
    g_idle_add_full(G_PRIORITY_DEFAULT, +[](gpointer data) -> gboolean {
        auto *progress_notice =
            static_cast<ExternalProgressNotice *>(data);
        auto *state = static_cast<WindowState *>(g_object_get_data(
            G_OBJECT(progress_notice->window),
            kWindowStateDataKey));
        if (state != nullptr && state->updates.external.external_updates_active) {
            if (state->updates.external.external_updates_status != nullptr)
                gtk_label_set_text(GTK_LABEL(state->updates.external.external_updates_status),
                    progress_notice->message.c_str());
            if (state->updates.updates_status != nullptr)
                gtk_label_set_text(GTK_LABEL(state->updates.updates_status),
                    progress_notice->message.c_str());
        }
        g_object_unref(progress_notice->window);
        delete progress_notice;
        return G_SOURCE_REMOVE;
    }, queued_notice, nullptr);
}

void external_apply_worker(
    GTask *task,
    gpointer,
    gpointer task_data,
    GCancellable *)
{
    auto *data =
        static_cast<ExternalApplyTaskData *>(
            task_data);
    auto *result =
        new ExternalApplyResult{};
    result->flatpak =
        data != nullptr && data->flatpak;

    if (data == nullptr) {
        result->error =
            "External update task state is unavailable.";
    } else if (data->flatpak) {
        result->selected = data->selected;
        result->success =
            apply_flatpak_updates_selected(
                data->selected,
                result->error,
                [task](std::string_view message) {
                    publish_external_progress(task, message);
                },
                &result->completed);
    } else {
        result->selected = data->selected;
        result->success =
            apply_cinnamon_updates_selected(
                data->selected,
                result->error,
                [task](std::string_view message) {
                    publish_external_progress(task, message);
                },
                &result->completed);
    }

    g_task_return_pointer(
        task,
        result,
        [](gpointer value) {
            delete static_cast<
                ExternalApplyResult *>(value);
        });
}

void external_apply_complete(
    GObject *source_object,
    GAsyncResult *async_result,
    gpointer)
{
    auto *window =
        GTK_WINDOW(source_object);
    auto *state =
        window == nullptr
            ? nullptr
            : static_cast<WindowState *>(
                  g_object_get_data(
                      G_OBJECT(window),
                      kWindowStateDataKey));
    auto *result =
        static_cast<ExternalApplyResult *>(
            g_task_propagate_pointer(
                G_TASK(async_result),
                nullptr));

    if (state == nullptr ||
        result == nullptr) {
        delete result;
        return;
    }

    state->updates.external.external_updates_active = false;
    if (state->updates.external.external_updates_spinner != nullptr) {
        gtk_spinner_stop(GTK_SPINNER(state->updates.external.external_updates_spinner));
        gtk_widget_set_visible(state->updates.external.external_updates_spinner, false);
    }

    if (!result->completed.empty()) {
        TransactionPlan history_plan;
        for (const ExternalUpdate &update : result->completed) {
            TransactionItem item;
            item.package_id =
                update.backend + ":" + update.id;
            item.action = TransactionAction::upgrade;
            item.to_version = update.version;
            item.download_bytes = update.download_bytes;
            item.source = update.backend;
            item.requested = true;
            history_plan.download_bytes +=
                update.download_bytes;
            history_plan.items.emplace_back(
                std::move(item));
        }
        record_transaction_history(
            history_plan,
            true,
            result->success
                ? (result->flatpak
                    ? "Selected Flatpak updates completed successfully."
                    : "Selected Cinnamon and Nemo updates completed successfully.")
                : "External update batch stopped after partially successful work; completed items are recorded here.");
    }

    if (result->success) {
        if (state->updates.external.external_updates_status != nullptr) {
            gtk_label_set_text(
                GTK_LABEL(
                    state->updates.external.external_updates_status),
                result->flatpak
                    ? "Flatpak updates complete. Rechecking…"
                    : "Cinnamon Spice updates complete. Rechecking…");
        }
        state->updates.inventory.updates_busy = false;
        update_nav_updates_badge(state);
        delete result;
        refresh_updates(state, false);
        return;
    }

    state->updates.inventory.updates_busy = false;
    update_nav_updates_badge(state);
    if (state->updates.external.external_updates_status != nullptr) {
        const std::string message =
            result->error.empty()
                ? "External update failed."
                : result->error;
        gtk_label_set_text(
            GTK_LABEL(
                state->updates.external.external_updates_status),
            message.c_str());
    }
    delete result;
    refresh_updates(state, false);
}

void begin_external_apply(WindowState *state, const bool flatpak)
{
    if (state == nullptr ||
        state->window == nullptr ||
        state->updates.inventory.updates_busy) {
        return;
    }

    std::vector<ExternalUpdate> selected;
    for (const ExternalUpdate &update :
         state->updates.external.external_update_records) {
        if (flatpak &&
            update.backend == "Flatpak" &&
            state->updates.external.selected_flatpak_refs.count(
                flatpak_selection_key(update)) != 0U) {
            selected.push_back(update);
        } else if (!flatpak &&
                   update.backend == "Cinnamon" &&
                   state->updates.external.selected_cinnamon_refs.count(
                       cinnamon_selection_key(update)) != 0U) {
            selected.push_back(update);
        }
    }
    if (selected.empty()) return;

    state->updates.inventory.updates_busy = true;
    state->updates.external.external_updates_active = true;
    update_nav_updates_badge(state);
    rebuild_external_updates(state);
    if (state->updates.external.external_updates_spinner != nullptr) {
        gtk_widget_set_visible(
            state->updates.external.external_updates_spinner, true);
        gtk_spinner_start(
            GTK_SPINNER(state->updates.external.external_updates_spinner));
    }
    if (state->updates.external.external_flatpak_apply != nullptr) {
        gtk_widget_set_sensitive(
            state->updates.external.external_flatpak_apply, false);
    }
    if (state->updates.external.external_cinnamon_apply != nullptr) {
        gtk_widget_set_sensitive(
            state->updates.external.external_cinnamon_apply, false);
    }
    if (state->updates.external.external_updates_status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->updates.external.external_updates_status),
            flatpak
                ? "Updating selected Flatpaks…"
                : "Updating selected Cinnamon items…");
    }

    auto *data = new ExternalApplyTaskData{};
    data->flatpak = flatpak;
    data->selected = std::move(selected);

    GTask *task =
        g_task_new(
            G_OBJECT(state->window),
            nullptr,
            external_apply_complete,
            nullptr);
    g_task_set_task_data(
        task,
        data,
        [](gpointer value) {
            delete static_cast<ExternalApplyTaskData *>(value);
        });
    g_task_run_in_thread(task, external_apply_worker);
    g_object_unref(task);
}

void external_apply_clicked(GtkButton *button, gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    if (state == nullptr ||
        state->window == nullptr ||
        state->updates.inventory.updates_busy) {
        return;
    }
    const bool flatpak =
        button == GTK_BUTTON(state->updates.external.external_flatpak_apply);

    std::ostringstream review;
    std::size_t count = 0U;
    for (const ExternalUpdate &update :
         state->updates.external.external_update_records) {
        if (flatpak) {
            if (update.backend != "Flatpak" ||
                state->updates.external.selected_flatpak_refs.count(
                    flatpak_selection_key(update)) == 0U) {
                continue;
            }
            review
                << (update.user_installation ? "User" : "System")
                << " • " << update.ref;
        } else {
            if (update.backend != "Cinnamon" ||
                state->updates.external.selected_cinnamon_refs.count(
                    cinnamon_selection_key(update)) == 0U) {
                continue;
            }
            review
                << external_update_kind_name(update.kind)
                << " • "
                << (update.name.empty() ? update.id : update.name)
                << " [" << update.id << "]";
        }
        if (!update.version.empty()) {
            review << " → " << update.version;
        }
        review << '\n';
        ++count;
    }
    if (count == 0U) return;

    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    GtkWidget *dialog =
        gtk_dialog_new_with_buttons(
            flatpak
                ? "Review selected Flatpak updates"
                : "Review selected Cinnamon updates",
            state->window,
            static_cast<GtkDialogFlags>(
                GTK_DIALOG_MODAL |
                GTK_DIALOG_DESTROY_WITH_PARENT),
            "Cancel", GTK_RESPONSE_CANCEL,
            "Update selected", GTK_RESPONSE_ACCEPT,
            nullptr);
    gtk_window_set_default_size(
        GTK_WINDOW(dialog), 560, 380);
    GtkWidget *content =
        gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_box_set_spacing(GTK_BOX(content), 12);
    gtk_widget_set_margin_start(content, 16);
    gtk_widget_set_margin_end(content, 16);
    gtk_widget_set_margin_top(content, 16);
    gtk_widget_set_margin_bottom(content, 16);

    GtkWidget *summary =
        make_label(
            flatpak
                ? "Only the selected Flatpak refs will be requested. Dependencies may also change."
                : "Only the selected Cinnamon applets, desklets, extensions, themes and Nemo actions will be updated.",
            "card-copy");
    gtk_label_set_wrap(GTK_LABEL(summary), true);
    gtk_box_append(GTK_BOX(content), summary);

    GtkWidget *scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(scroller),
        GTK_POLICY_NEVER,
        GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroller, true);
    GtkWidget *list =
        make_label(review.str().c_str(), "card-copy");
    gtk_label_set_wrap(GTK_LABEL(list), true);
    gtk_label_set_selectable(GTK_LABEL(list), true);
    gtk_scrolled_window_set_child(
        GTK_SCROLLED_WINDOW(scroller), list);
    gtk_box_append(GTK_BOX(content), scroller);

    g_object_set_data(
        G_OBJECT(dialog),
        "external-flatpak",
        GINT_TO_POINTER(flatpak));
    g_signal_connect_data(
        dialog,
        "response",
        G_CALLBACK(+[](GtkDialog *response_dialog,
                       gint response,
                       gpointer response_data) {
            auto *response_window =
                GTK_WINDOW(response_data);
            const bool apply_flatpak =
                GPOINTER_TO_INT(
                    g_object_get_data(
                        G_OBJECT(response_dialog),
                        "external-flatpak")) != 0;
            gtk_window_destroy(
                GTK_WINDOW(response_dialog));
            auto *window_state =
                static_cast<WindowState *>(
                    g_object_get_data(
                        G_OBJECT(response_window),
                        kWindowStateDataKey));
            if (response == GTK_RESPONSE_ACCEPT &&
                window_state != nullptr) {
                begin_external_apply(
                    window_state,
                    apply_flatpak);
            }
        }),
        g_object_ref(state->window),
        +[](gpointer data, GClosure *) {
            g_object_unref(data);
        },
        GConnectFlags(0));
    gtk_window_present(GTK_WINDOW(dialog));
}

void updates_worker(
    GTask *task,
    gpointer,
    gpointer task_data,
    GCancellable *)
{
    auto *data =
        static_cast<UpdatesRefreshRequest *>(task_data);
    auto *result = new UpdatesRefreshResult{};
    if (data == nullptr) {
        result->error =
            "Update task state is unavailable.";
    } else {
        *result = refresh_updates_data(*data);
    }

    g_task_return_pointer(
        task,
        result,
        [](gpointer pointer) {
            delete static_cast<UpdatesRefreshResult *>(pointer);
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
            kWindowStateDataKey));
    if (state != nullptr && !state->updates.inventory.updates_busy) {
        refresh_updates_internal(state, true, false);
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
            G_OBJECT(window), kWindowStateDataKey));
    auto *result = static_cast<UpdatesRefreshResult *>(
        g_task_propagate_pointer(G_TASK(async_result), nullptr));

    if (state == nullptr || result == nullptr) {
        delete result;
        return;
    }

    if (result->generation != state->updates.inventory.updates_generation) {
        delete result;
        return;
    }

    const bool post_install =
        state->updates.transaction.updates_post_install_refresh;
    state->updates.inventory.updates_busy = false;
    if (!post_install) {
        stop_update_progress(state);
    }
    state->updates.inventory.update_records = std::move(result->records);
    state->updates.inventory.update_records.erase(
        std::remove_if(
            state->updates.inventory.update_records.begin(),
            state->updates.inventory.update_records.end(),
            [&](const PackageRecord &package) {
                return update_is_ignored(
                    package, state->preferences);
            }),
        state->updates.inventory.update_records.end());

    /*
     * Match Mint Update Manager's priority-update rule.  Software must update
     * its own package engine/client contract before attempting unrelated APT,
     * Flatpak or Cinnamon changes.  This also guarantees that migrations such
     * as an engine API bump cannot be hidden in a larger transaction.
     */
    const bool self_update =
        std::any_of(
            state->updates.inventory.update_records.begin(),
            state->updates.inventory.update_records.end(),
            [](const PackageRecord &package) {
                const std::string_view name =
                    package.package_name.empty()
                        ? std::string_view(package.id)
                        : std::string_view(package.package_name);
                return name == "infiltrator-software";
            });
    if (self_update) {
        state->updates.inventory.update_records.erase(
            std::remove_if(
                state->updates.inventory.update_records.begin(),
                state->updates.inventory.update_records.end(),
                [](const PackageRecord &package) {
                    const std::string_view name =
                        package.package_name.empty()
                            ? std::string_view(package.id)
                            : std::string_view(package.package_name);
                    return name != "infiltrator-software";
                }),
            state->updates.inventory.update_records.end());
        result->external_refreshed = true;
        result->external_records.clear();
        result->external_error.clear();
    }

    notify_updates_state_changed(state);
    update_nav_updates_badge(state);

    if (state->updates.updates_reboot_banner != nullptr) {
        const bool required = reboot_required();
        gtk_widget_set_visible(
            state->updates.updates_reboot_banner,
            required);
        if (required &&
            state->updates.updates_reboot_detail != nullptr) {
            const auto packages =
                reboot_required_packages();
            std::string detail =
                "A restart is required to finish applying system updates.";
            if (!packages.empty()) {
                detail += " Requested by: ";
                for (std::size_t index = 0U;
                     index < packages.size();
                     ++index) {
                    if (index != 0U) {
                        detail += ", ";
                    }
                    detail += packages[index];
                    if (index == 5U &&
                        packages.size() > 6U) {
                        detail += ", …";
                        break;
                    }
                }
            }
            gtk_label_set_text(
                GTK_LABEL(state->updates.updates_reboot_detail),
                detail.c_str());
        }
    }
    state->updates.inventory.selected_update_ids.clear();
    const std::string selection_preset =
        std::exchange(
            state->updates.inventory.pending_update_selection,
            std::string{});
    for (const PackageRecord &package : state->updates.inventory.update_records) {
        const bool include =
            selection_preset == "security"
                ? package.security_update
                : selection_preset == "kernel"
                    ? package.kind ==
                        infiltrator::software::PackageKind::kernel
                    : true;
        if (!include) continue;
        const std::string identity = update_identity(package);
        if (!identity.empty()) {
            state->updates.inventory.selected_update_ids.insert(identity);
        }
    }
    const bool external_refreshed =
        result->external_refreshed;
    const std::string external_error =
        external_refreshed
            ? result->external_error
            : std::string{};
    if (external_refreshed) {
        state->updates.external.external_update_records =
            std::move(result->external_records);
        state->updates.external.selected_flatpak_refs.clear();
        state->updates.external.selected_cinnamon_refs.clear();
        for (const ExternalUpdate &update :
             state->updates.external.external_update_records) {
            if (update.backend == "Flatpak") {
                state->updates.external.selected_flatpak_refs.insert(
                    flatpak_selection_key(update));
            } else if (update.backend == "Cinnamon") {
                state->updates.external.selected_cinnamon_refs.insert(
                    cinnamon_selection_key(update));
            }
        }
        rebuild_external_updates(state);
    }

    const bool refreshed_metadata =
        result->refreshed_metadata;
    const std::string error =
        result->error;
    delete result;

    if (refreshed_metadata && error.empty()) {
        state->updates.inventory.updates_last_metadata_refresh_us =
            g_get_monotonic_time();
    }

    state->updates.transaction.updates_post_install_refresh = false;

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
        state->updates.inventory.updates_auto_refresh_pending;
    if (schedule_auto_refresh || refreshed_metadata) {
        state->updates.inventory.updates_auto_refresh_pending = false;
    }

    if (state->updates.updates_backend != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->updates.updates_backend),
            "Native engine");
    }

    rebuild_updates(state);

    if (state->updates.updates_status != nullptr) {
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
                GTK_LABEL(state->updates.updates_status), message.c_str());
        } else if (post_install) {
            const std::string message =
                state->updates.inventory.update_records.empty()
                    ? "Installation complete. Final package state verified; the system is up to date."
                    : "Installation complete. Final package state verified; " +
                        std::to_string(state->updates.inventory.update_records.size()) +
                        (state->updates.inventory.update_records.size() == 1U
                             ? " preferred update remains."
                             : " preferred updates remain.");
            gtk_label_set_text(
                GTK_LABEL(state->updates.updates_status), message.c_str());
        } else if (schedule_auto_refresh) {
            gtk_label_set_text(
                GTK_LABEL(state->updates.updates_status),
                state->updates.inventory.update_records.empty()
                    ? "Cached package state has no updates; checking repositories for newer metadata…"
                    : "Cached updates loaded; checking repositories for newer metadata…");
        } else if (state->updates.inventory.update_records.empty()) {
            gtk_label_set_text(
                GTK_LABEL(state->updates.updates_status),
                "Your system is up to date.");
        } else {
            const std::string message =
                std::to_string(state->updates.inventory.update_records.size()) +
                (state->updates.inventory.update_records.size() == 1U
                     ? " update is available."
                     : " updates are available.");
            gtk_label_set_text(
                GTK_LABEL(state->updates.updates_status), message.c_str());
        }
    }

    if (state->updates.updates_install != nullptr) {
        if (!error.empty()) {
            gtk_widget_set_sensitive(state->updates.updates_install, false);
        } else {
            update_selection_controls(state);
        }
    }
    if (state->updates.updates_refresh != nullptr) {
        gtk_widget_set_sensitive(
            state->updates.updates_refresh, true);
    }

    if (!external_error.empty() &&
        state->updates.external.external_updates_status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(
                state->updates.external.external_updates_status),
            external_error.c_str());
    }

    if (error.empty()) {
        set_update_runtime_state({});
    } else {
        set_update_runtime_state(
            "error:" + one_line(error));
    }

    if (post_install) {
        finish_update_progress(
            state,
            error.empty(),
            error.empty()
                ? "Installation complete. Final installed state has been verified."
                : "Packages were applied, but final state verification did not complete successfully.");
        if (error.empty() &&
            state->preferences.hide_window_after_update &&
            state->window != nullptr) {
            gtk_widget_set_visible(
                GTK_WIDGET(state->window), false);
        }
        if (error.empty()) {
            /*
             * The native refresh has now published the authoritative
             * post-transaction generation.  Only now may other pages reload.
             */
            notify_update_transaction_completed(state);

            if (state->updates.transaction.updates_restart_after_verify) {
                state->updates.transaction.updates_restart_after_verify = false;
                /*
                 * Replace this process with the newly installed executable.
                 * Spawning another GtkApplication first is racy: while this
                 * process still owns the application bus name, the new
                 * executable can simply forward --updates back to the old
                 * instance and exit. exec() gives a deterministic hand-off.
                 */
                (void)execl(
                    "/usr/bin/infiltrator-software",
                    "infiltrator-software",
                    "--updates",
                    static_cast<char *>(nullptr));
                g_warning(
                    "Software updated successfully but could not replace "
                    "the running process: %s",
                    g_strerror(errno));
            }
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

void refresh_updates_internal(
    WindowState *state,
    const bool refresh_metadata,
    const bool refresh_external)
{
    if (state == nullptr || state->window == nullptr ||
        state->updates.updates_list == nullptr || state->updates.inventory.updates_busy) {
        return;
    }

    if (refresh_metadata) {
        state->updates.inventory.updates_auto_refresh_pending = false;
    }

    state->updates.inventory.updates_loaded = true;
    state->updates.inventory.updates_busy = true;
    ++state->updates.inventory.updates_generation;

    if (state->updates.updates_status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->updates.updates_status),
            state->updates.transaction.updates_post_install_refresh
                ? "Installation finished. Verifying installed versions and remaining updates…"
                : refresh_metadata
                    ? "Refreshing repository metadata without administrator access…"
                    : "Checking installed versions and available updates…");
    }
    if (state->updates.updates_install != nullptr) {
        gtk_widget_set_sensitive(state->updates.updates_install, false);
    }
    if (state->updates.updates_refresh != nullptr) {
        gtk_widget_set_sensitive(state->updates.updates_refresh, false);
    }

    set_update_runtime_state("checking");

    auto *data = new UpdatesRefreshRequest{
        state->updates.inventory.updates_generation,
        refresh_metadata,
        refresh_external,
        state->preferences.show_flatpak_updates,
        state->preferences.show_cinnamon_updates};
    GTask *task = g_task_new(
        G_OBJECT(state->window),
        nullptr,
        updates_complete,
        nullptr);
    g_task_set_task_data(
        task, data,
        [](gpointer pointer) {
            delete static_cast<UpdatesRefreshRequest *>(pointer);
        });
    g_task_run_in_thread(task, updates_worker);
    g_object_unref(task);
}

void refresh_updates(
    WindowState *state,
    const bool refresh_metadata)
{
    refresh_updates_internal(
        state,
        refresh_metadata,
        true);
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
                      kWindowStateDataKey));

    const bool success =
        communicated && g_subprocess_get_successful(process);
    const bool no_changes_required =
        success &&
        stdout_text != nullptr &&
        std::string_view(stdout_text).find(
            "INFILTRATOR_NO_CHANGES_REQUIRED") !=
            std::string_view::npos;

    if (state != nullptr) {
        state->updates.inventory.updates_busy = false;

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
            notify_transaction_history_changed(state);
        }

        if (success) {
            set_update_runtime_state({});
            if (state->updates.updates_status != nullptr) {
                gtk_label_set_text(
                    GTK_LABEL(state->updates.updates_status),
                    run->operation == "refresh"
                        ? "Package lists refreshed. Checking updates…"
                        : no_changes_required
                            ? "Selected updates were already installed. Checking current package state…"
                            : "Updates installed. Checking system state…");
            }

            state->updates.transaction.updates_post_install_refresh =
                run->operation == "install";
            if (run->operation == "install") {
                state->updates.transaction.updates_restart_after_verify =
                    std::any_of(
                        run->plan.items.begin(),
                        run->plan.items.end(),
                        [](const TransactionItem &item) {
                            const std::string_view id(
                                item.package_id);
                            return id == "infiltrator-software" ||
                                   id.rfind(
                                       "infiltrator-software:",
                                       0U) == 0U;
                        });
            }
            /*
             * Re-read authoritative dpkg state first. Repository metadata was
             * already refreshed by the privileged helper before mutation, so
             * repeating the full network reconciliation here only delays the
             * UI and can leave completed packages visible as stale updates.
             */
            refresh_updates_internal(
                state,
                run->operation == "refresh",
                run->operation == "refresh");
        } else {
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

            if (state->updates.updates_status != nullptr) {
                gtk_label_set_text(
                    GTK_LABEL(state->updates.updates_status),
                    message.c_str());
            }
            finish_update_progress(
                state, false, message);
            if (state->updates.updates_install != nullptr) {
                update_selection_controls(state);
            }
            if (state->updates.updates_refresh != nullptr) {
                gtk_widget_set_sensitive(
                    state->updates.updates_refresh, true);
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

    std::string start_error;
    GSubprocess *process = start_subprocess(
        arguments,
        static_cast<GSubprocessFlags>(
            G_SUBPROCESS_FLAGS_STDOUT_PIPE |
            G_SUBPROCESS_FLAGS_STDERR_PIPE),
        start_error);

    if (process == nullptr) {
        state->updates.inventory.updates_busy = false;
        std::string message =
            operation == "refresh"
                ? "Unable to start package-list refresh."
                : "Unable to start update installation.";
        if (!start_error.empty()) {
            message += " ";
            message += one_line(start_error);
        }
        if (state->updates.updates_status != nullptr) {
            gtk_label_set_text(
                GTK_LABEL(state->updates.updates_status),
                message.c_str());
        }
        set_update_runtime_state(
            "error:" + one_line(message));
        if (state->updates.updates_refresh != nullptr) {
            gtk_widget_set_sensitive(
                state->updates.updates_refresh, true);
        }
        if (state->updates.updates_install != nullptr) {
            update_selection_controls(state);
        }
        finish_update_progress(
            state, false, message);
        if (!plan.items.empty()) {
            record_transaction_history(
                plan, false, message);
            notify_transaction_history_changed(state);
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
    if (state == nullptr || state->updates.inventory.updates_busy) {
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
    if (state == nullptr || !state->updates.transaction.pending_update_plan.has_value()) {
        return;
    }

    std::vector<std::string> specs;
    std::string plan_error;
    if (!exact_transaction_specs(
            *state->updates.transaction.pending_update_plan, specs, plan_error)) {
        state->updates.inventory.updates_busy = false;
        state->updates.transaction.pending_update_plan.reset();
        if (state->updates.updates_status != nullptr) {
            gtk_label_set_text(
                GTK_LABEL(state->updates.updates_status),
                plan_error.c_str());
        }
        if (state->updates.updates_install != nullptr) {
            update_selection_controls(state);
        }
        if (state->updates.updates_refresh != nullptr) {
            gtk_widget_set_sensitive(state->updates.updates_refresh, true);
        }
        set_update_runtime_state("error:" + one_line(plan_error));
        return;
    }

    const TransactionPlan approved_plan =
        *state->updates.transaction.pending_update_plan;

    state->updates.inventory.updates_busy = true;
    if (state->updates.updates_status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->updates.updates_status),
            "Starting the approved update transaction; administrator authentication may be requested…");
    }
    if (state->updates.updates_install != nullptr) {
        gtk_widget_set_sensitive(state->updates.updates_install, false);
    }
    if (state->updates.updates_refresh != nullptr) {
        gtk_widget_set_sensitive(state->updates.updates_refresh, false);
    }
    set_update_runtime_state("installing");
    start_update_progress(
        state, approved_plan);

    std::vector<std::string> arguments{
        "pkexec",
        "/usr/libexec/infiltrator-software-update-helper",
        "apply-plan",
        "--progress-token=" +
            state->updates.transaction.updates_progress_token};
    if (state->preferences.keep_configuration) {
        arguments.emplace_back(
            "--force-confold");
    }
    arguments.reserve(
        specs.size() + 5U);
    arguments.insert(
        arguments.end(),
        specs.begin(),
        specs.end());

    state->updates.transaction.pending_update_plan.reset();
    start_update_process(
        state,
        std::move(arguments),
        "install",
        approved_plan);
}

struct UpdateSnapshotRun {
    GtkWindow *window{};
};

void update_snapshot_complete(
    GObject *source_object,
    GAsyncResult *async_result,
    gpointer user_data)
{
    auto *run =
        static_cast<UpdateSnapshotRun *>(
            user_data);
    auto *process =
        G_SUBPROCESS(source_object);
    GError *error = nullptr;
    gchar *out = nullptr;
    gchar *err = nullptr;
    const gboolean communicated =
        g_subprocess_communicate_utf8_finish(
            process,
            async_result,
            &out,
            &err,
            &error);

    auto *state =
        run == nullptr ||
        run->window == nullptr
            ? nullptr
            : static_cast<WindowState *>(
                  g_object_get_data(
                      G_OBJECT(run->window),
                      kWindowStateDataKey));

    const bool success =
        communicated &&
        g_subprocess_get_successful(process);
    if (state != nullptr) {
        if (success) {
            if (state->updates.updates_status != nullptr) {
                gtk_label_set_text(
                    GTK_LABEL(
                        state->updates.updates_status),
                    "Pre-update snapshot created. Starting the reviewed update transaction…");
            }
            begin_apply_updates(state);
        } else {
            state->updates.inventory.updates_busy = false;
            state->updates.transaction.pending_update_plan.reset();
            std::string message =
                "The system update was not started because the requested pre-update snapshot failed.";
            if (err != nullptr &&
                *err != '\0') {
                message += " ";
                message += one_line(err);
            } else if (
                error != nullptr &&
                error->message != nullptr) {
                message += " ";
                message += one_line(
                    error->message);
            }
            if (state->updates.updates_status != nullptr) {
                gtk_label_set_text(
                    GTK_LABEL(
                        state->updates.updates_status),
                    message.c_str());
            }
            if (state->updates.updates_install != nullptr) {
                update_selection_controls(state);
            }
            if (state->updates.updates_refresh != nullptr) {
                gtk_widget_set_sensitive(
                    state->updates.updates_refresh,
                    true);
            }
        }
    }

    g_free(out);
    g_free(err);
    g_clear_error(&error);
    if (run != nullptr) {
        if (run->window != nullptr) {
            g_object_unref(run->window);
        }
        delete run;
    }
}

bool create_snapshot_before_update(
    WindowState *state)
{
    if (state == nullptr ||
        state->window == nullptr) {
        return false;
    }

    const char *timeshift =
        g_find_program_in_path(
            "timeshift");
    if (timeshift == nullptr) {
        if (state->updates.updates_status != nullptr) {
            gtk_label_set_text(
                GTK_LABEL(
                    state->updates.updates_status),
                "A pre-update snapshot was requested, but Timeshift is not installed.");
        }
        return false;
    }
    g_free(
        const_cast<char *>(timeshift));

    const std::vector<std::string> arguments{
        "pkexec",
        "timeshift",
        "--create",
        "--comments",
        "Infiltrator Software system update",
        "--tags",
        "D"};
    std::string start_error;
    GSubprocess *process = start_subprocess(
        arguments,
        static_cast<GSubprocessFlags>(
            G_SUBPROCESS_FLAGS_STDOUT_PIPE |
            G_SUBPROCESS_FLAGS_STDERR_PIPE),
        start_error);
    if (process == nullptr) {
        if (state->updates.updates_status != nullptr) {
            gtk_label_set_text(
                GTK_LABEL(
                    state->updates.updates_status),
                start_error.empty()
                    ? "Unable to start the snapshot tool."
                    : start_error.c_str());
        }
        return false;
    }

    if (state->updates.updates_status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(
                state->updates.updates_status),
            "Creating the requested pre-update system snapshot…");
    }
    auto *run =
        new UpdateSnapshotRun{
            GTK_WINDOW(
                g_object_ref(
                    state->window))};
    g_subprocess_communicate_utf8_async(
        process,
        nullptr,
        nullptr,
        update_snapshot_complete,
        run);
    g_object_unref(process);
    return true;
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
        if (state->updates.transaction.pending_update_plan.has_value() &&
            state->updates.transaction.pending_update_plan->touches_system &&
            state->preferences.snapshot_before_system_updates) {
            if (!create_snapshot_before_update(state)) {
                state->updates.inventory.updates_busy = false;
                state->updates.transaction.pending_update_plan.reset();
                if (state->updates.updates_install != nullptr) {
                    update_selection_controls(state);
                }
                if (state->updates.updates_refresh != nullptr) {
                    gtk_widget_set_sensitive(
                        state->updates.updates_refresh,
                        true);
                }
            }
            return;
        }
        begin_apply_updates(state);
        return;
    }

    state->updates.inventory.updates_busy = false;
    state->updates.transaction.pending_update_plan.reset();
    if (state->updates.updates_status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->updates.updates_status),
            "Update installation cancelled.");
    }
    if (state->updates.updates_install != nullptr) {
        update_selection_controls(state);
    }
    if (state->updates.updates_refresh != nullptr) {
        gtk_widget_set_sensitive(state->updates.updates_refresh, true);
    }
    set_update_runtime_state({});
}

void update_plan_worker(
    GTask *task,
    gpointer,
    gpointer task_data,
    GCancellable *)
{
    auto *data =
        static_cast<UpdatePlanRequest *>(task_data);
    auto *result = new UpdatePlanResult{};
    if (data == nullptr) {
        result->error =
            "No updates are available to plan.";
    } else {
        *result = plan_updates(*data);
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
            G_OBJECT(window), kWindowStateDataKey));
    auto *result = static_cast<UpdatePlanResult *>(
        g_task_propagate_pointer(G_TASK(async_result), nullptr));

    if (state == nullptr || result == nullptr) {
        delete result;
        return;
    }

    if (!result->plan.has_value()) {
        state->updates.inventory.updates_busy = false;
        state->updates.transaction.pending_update_plan.reset();
        std::string message =
            "Unable to plan updates: " + one_line(result->error);
        if (state->updates.updates_status != nullptr) {
            gtk_label_set_text(
                GTK_LABEL(state->updates.updates_status),
                message.c_str());
        }
        if (state->updates.updates_install != nullptr) {
            update_selection_controls(state);
        }
        if (state->updates.updates_refresh != nullptr) {
            gtk_widget_set_sensitive(state->updates.updates_refresh, true);
        }
        set_update_runtime_state(
            "error:" + one_line(message));
        delete result;
        return;
    }

    const infiltrator::software::TransactionPlan plan =
        *result->plan;
    delete result;

    state->updates.transaction.pending_update_plan = plan;

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
        make_transaction_review_dialog(
            state->window,
            "Review updates",
            heading.str(),
            "Install updates",
            plan);
    g_signal_connect(
        dialog, "response",
        G_CALLBACK(update_confirm_response), state);
    gtk_window_present(GTK_WINDOW(dialog));
}

void updates_select_all_clicked(GtkButton *, gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    if (state == nullptr || state->updates.inventory.updates_busy) return;

    state->updates.inventory.selected_update_ids.clear();
    for (const PackageRecord &package : state->updates.inventory.update_records) {
        const std::string identity = update_identity(package);
        if (!identity.empty()) state->updates.inventory.selected_update_ids.insert(identity);
    }
    rebuild_updates(state);
    update_selection_controls(state);
}

void updates_clear_selection_clicked(GtkButton *, gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    if (state == nullptr || state->updates.inventory.updates_busy) return;

    state->updates.inventory.selected_update_ids.clear();
    rebuild_updates(state);
    update_selection_controls(state);
}

void update_install_clicked(GtkButton *, gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    if (state == nullptr || state->window == nullptr ||
        state->updates.inventory.updates_busy || state->updates.inventory.update_records.empty() ||
        state->updates.inventory.selected_update_ids.empty()) {
        return;
    }

    state->updates.inventory.updates_busy = true;
    state->updates.transaction.pending_update_plan.reset();
    if (state->updates.updates_status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->updates.updates_status),
            "Resolving the complete update transaction…");
    }
    if (state->updates.updates_install != nullptr) {
        gtk_widget_set_sensitive(state->updates.updates_install, false);
    }
    if (state->updates.updates_refresh != nullptr) {
        gtk_widget_set_sensitive(state->updates.updates_refresh, false);
    }
    set_update_runtime_state("checking");

    auto *data = new UpdatePlanRequest{};
    data->install_recommends =
        state->preferences.install_recommends;
    data->package_ids.reserve(state->updates.inventory.selected_update_ids.size());
    for (const PackageRecord &package : state->updates.inventory.update_records) {
        const std::string identity = update_identity(package);
        if (state->updates.inventory.selected_update_ids.find(identity) !=
            state->updates.inventory.selected_update_ids.end()) {
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
            delete static_cast<UpdatePlanRequest *>(pointer);
        });
    g_task_run_in_thread(task, update_plan_worker);
    g_object_unref(task);
}

void restart_system_clicked(
    GtkButton *,
    gpointer user_data)
{
    auto *state = static_cast<WindowState *>(user_data);
    if (state == nullptr || state->window == nullptr) {
        return;
    }

    const char *desktop = g_getenv("XDG_CURRENT_DESKTOP");
    std::vector<std::vector<std::string>> candidates;
    if (desktop != nullptr &&
        (std::string_view(desktop) == "Cinnamon" ||
         std::string_view(desktop) == "X-Cinnamon")) {
        candidates.push_back(
            {"cinnamon-session-quit", "--reboot"});
    } else if (desktop != nullptr &&
               std::string_view(desktop).find("XFCE") !=
                   std::string_view::npos) {
        candidates.push_back(
            {"xfce4-session-logout", "--reboot"});
    }
    candidates.push_back({"systemctl", "reboot"});

    std::string last_error;
    for (const std::vector<std::string> &command : candidates) {
        std::string error;
        GSubprocess *process = start_subprocess(
            command,
            G_SUBPROCESS_FLAGS_NONE,
            error);
        if (process != nullptr) {
            /* The desktop/session manager owns restart after launch. */
            g_object_unref(process);
            return;
        }
        if (!error.empty()) {
            last_error = std::move(error);
        }
    }

    if (state->updates.updates_status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(state->updates.updates_status),
            last_error.empty()
                ? "Unable to request a system restart."
                : last_error.c_str());
    }
}

GtkWidget *make_updates_page(WindowState *state)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
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
            &state->updates.updates_count),
        0, 0, 1, 1);
    gtk_grid_attach(
        GTK_GRID(stats),
        make_stat_card(
            "SYSTEM-CRITICAL", "0", "stat-warning",
            &state->updates.updates_critical),
        1, 0, 1, 1);
    gtk_grid_attach(
        GTK_GRID(stats),
        make_stat_card(
            "BACKEND", "Loading", "stat-info",
            &state->updates.updates_backend),
        2, 0, 1, 1);
    gtk_box_append(GTK_BOX(page), stats);

    GtkWidget *controls =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(controls, "card");
    gtk_widget_add_css_class(controls, "updates-action-bar");

    GtkWidget *updates_status_icon =
        make_icon("view-refresh-symbolic", 24);
    gtk_widget_add_css_class(
        updates_status_icon, "updates-status-icon");
    gtk_widget_set_valign(
        updates_status_icon, GTK_ALIGN_CENTER);
    gtk_box_append(
        GTK_BOX(controls), updates_status_icon);

    state->updates.updates_status =
        make_label("Checking for updates…", "card-copy");
    gtk_label_set_wrap(GTK_LABEL(state->updates.updates_status), true);
    gtk_widget_set_hexpand(state->updates.updates_status, true);
    gtk_box_append(GTK_BOX(controls), state->updates.updates_status);

    GtkWidget *select_all =
        gtk_button_new_with_label("Select all");
    gtk_widget_add_css_class(select_all, "discover-details");
    g_signal_connect(
        select_all, "clicked",
        G_CALLBACK(updates_select_all_clicked), state);

    state->updates.updates_security =
        gtk_button_new_with_label("Security");
    gtk_widget_add_css_class(
        state->updates.updates_security,
        "discover-details");
    gtk_widget_set_tooltip_text(
        state->updates.updates_security,
        "Select only updates classified as security fixes (Ctrl+S).");
    g_signal_connect(
        state->updates.updates_security,
        "clicked",
        G_CALLBACK(select_security_updates),
        state);

    GtkWidget *kernel_only =
        gtk_button_new_with_label("Kernel");
    gtk_widget_add_css_class(
        kernel_only,
        "discover-details");
    gtk_widget_set_tooltip_text(
        kernel_only,
        "Select only Linux kernel package updates (Ctrl+K).");
    g_signal_connect(
        kernel_only,
        "clicked",
        G_CALLBACK(select_kernel_updates),
        state);

    GtkWidget *clear_selection =
        gtk_button_new_with_label("Clear");
    gtk_widget_add_css_class(clear_selection, "discover-details");
    g_signal_connect(
        clear_selection, "clicked",
        G_CALLBACK(updates_clear_selection_clicked), state);

    state->updates.updates_refresh =
        gtk_button_new_with_label("Check again");
    gtk_widget_add_css_class(
        state->updates.updates_refresh, "discover-details");
    g_signal_connect(
        state->updates.updates_refresh, "clicked",
        G_CALLBACK(update_refresh_clicked), state);
    gtk_box_append(GTK_BOX(controls), state->updates.updates_refresh);

    state->updates.updates_install =
        gtk_button_new_with_label("Install selected updates");
    gtk_widget_add_css_class(
        state->updates.updates_install, "suggested-action");
    gtk_widget_set_sensitive(state->updates.updates_install, false);
    g_signal_connect(
        state->updates.updates_install, "clicked",
        G_CALLBACK(update_install_clicked), state);
    gtk_box_append(GTK_BOX(controls), state->updates.updates_install);

    gtk_box_append(GTK_BOX(page), controls);

    state->updates.transaction.updates_transaction_panel =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(
        state->updates.transaction.updates_transaction_panel,
        "update-transaction-panel");
    gtk_widget_set_visible(
        state->updates.transaction.updates_transaction_panel,
        false);

    GtkWidget *transaction_header =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *transaction_icon_well =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(
        transaction_icon_well,
        "update-transaction-icon");
    GtkWidget *transaction_icon =
        make_icon("software-update-available-symbolic", 25);
    gtk_widget_set_halign(
        transaction_icon, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(
        transaction_icon, GTK_ALIGN_CENTER);
    gtk_box_append(
        GTK_BOX(transaction_icon_well),
        transaction_icon);
    gtk_box_append(
        GTK_BOX(transaction_header),
        transaction_icon_well);

    GtkWidget *transaction_copy =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(transaction_copy, true);
    gtk_box_append(
        GTK_BOX(transaction_copy),
        make_label(
            "Update transaction",
            "update-transaction-title"));
    state->updates.transaction.updates_transaction_phase =
        make_label(
            "Preparing updates",
            "update-transaction-phase");
    gtk_box_append(
        GTK_BOX(transaction_copy),
        state->updates.transaction.updates_transaction_phase);
    gtk_box_append(
        GTK_BOX(transaction_header),
        transaction_copy);
    state->updates.transaction.updates_transaction_meta =
        make_label(
            "",
            "update-transaction-meta",
            1.0F);
    gtk_widget_set_valign(
        state->updates.transaction.updates_transaction_meta,
        GTK_ALIGN_CENTER);
    gtk_box_append(
        GTK_BOX(transaction_header),
        state->updates.transaction.updates_transaction_meta);
    gtk_box_append(
        GTK_BOX(state->updates.transaction.updates_transaction_panel),
        transaction_header);

    GtkWidget *stage_strip =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(
        stage_strip, "update-stage-strip");
    static constexpr const char *stage_names[] = {
        "Authorize",
        "Refresh",
        "Validate",
        "Download",
        "Install",
        "Verify"
    };
    for (int stage = 0; stage < 6; ++stage) {
        state->updates.transaction.updates_stage_labels[stage] =
            make_label(
                stage_names[stage],
                "update-stage",
                0.5F);
        gtk_widget_set_hexpand(
            state->updates.transaction.updates_stage_labels[stage],
            true);
        gtk_box_append(
            GTK_BOX(stage_strip),
            state->updates.transaction.updates_stage_labels[stage]);
    }
    gtk_box_append(
        GTK_BOX(state->updates.transaction.updates_transaction_panel),
        stage_strip);

    state->updates.transaction.updates_progress = gtk_progress_bar_new();
    gtk_widget_add_css_class(
        state->updates.transaction.updates_progress,
        "update-transaction-progress");
    gtk_progress_bar_set_show_text(
        GTK_PROGRESS_BAR(state->updates.transaction.updates_progress),
        false);
    gtk_widget_set_tooltip_text(
        state->updates.transaction.updates_progress,
        "Live graphical state for the approved update transaction.");
    gtk_box_append(
        GTK_BOX(state->updates.transaction.updates_transaction_panel),
        state->updates.transaction.updates_progress);

    state->updates.transaction.updates_transaction_detail =
        make_label(
            "",
            "update-transaction-detail");
    gtk_label_set_wrap(
        GTK_LABEL(state->updates.transaction.updates_transaction_detail),
        true);
    gtk_box_append(
        GTK_BOX(state->updates.transaction.updates_transaction_panel),
        state->updates.transaction.updates_transaction_detail);
    gtk_box_append(
        GTK_BOX(page),
        state->updates.transaction.updates_transaction_panel);

    state->updates.updates_reboot_banner =
        gtk_box_new(
            GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(
        state->updates.updates_reboot_banner,
        "reboot-required-banner");
    GtkWidget *reboot_icon =
        make_icon(
            "system-reboot-symbolic", 22);
    gtk_box_append(
        GTK_BOX(state->updates.updates_reboot_banner),
        reboot_icon);
    state->updates.updates_reboot_detail =
        make_label(
            "A restart is required to finish applying system updates.",
            "reboot-required-copy");
    gtk_label_set_wrap(
        GTK_LABEL(state->updates.updates_reboot_detail),
        true);
    gtk_widget_set_hexpand(
        state->updates.updates_reboot_detail,
        true);
    gtk_box_append(
        GTK_BOX(state->updates.updates_reboot_banner),
        state->updates.updates_reboot_detail);
    GtkWidget *restart_now =
        gtk_button_new_with_label("Restart");
    gtk_widget_add_css_class(
        restart_now,
        "suggested-action");
    gtk_widget_set_tooltip_text(
        restart_now,
        "Restart the system now so installed kernel and system updates take effect.");
    g_signal_connect(
        restart_now,
        "clicked",
        G_CALLBACK(restart_system_clicked),
        state);
    gtk_box_append(
        GTK_BOX(state->updates.updates_reboot_banner),
        restart_now);
    gtk_widget_set_visible(
        state->updates.updates_reboot_banner,
        reboot_required());
    gtk_box_append(
        GTK_BOX(page),
        state->updates.updates_reboot_banner);

    GtkWidget *card =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(card, "card");
    gtk_widget_add_css_class(card, "card-info");
    gtk_widget_set_vexpand(card, true);

    GtkWidget *heading_row =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *heading_icon =
        make_icon("software-update-available-symbolic", 22);
    gtk_widget_add_css_class(
        heading_icon, "updates-section-icon");
    GtkWidget *heading_copy =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gtk_widget_set_hexpand(
        heading_copy, true);
    GtkWidget *heading =
        make_label("Available updates", "card-title");
    GtkWidget *heading_note =
        make_label(
            "Grouped by the part of the system they affect.",
            "card-copy");
    gtk_box_append(GTK_BOX(heading_copy), heading);
    gtk_box_append(GTK_BOX(heading_copy), heading_note);
    gtk_box_append(GTK_BOX(heading_row), heading_icon);
    gtk_box_append(GTK_BOX(heading_row), heading_copy);


    GtkWidget *selection_tools =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(
        selection_tools, "updates-selection-tools");
    gtk_widget_set_valign(
        selection_tools, GTK_ALIGN_CENTER);
    gtk_box_append(
        GTK_BOX(selection_tools), select_all);
    gtk_box_append(
        GTK_BOX(selection_tools), state->updates.updates_security);
    gtk_box_append(
        GTK_BOX(selection_tools), kernel_only);
    gtk_box_append(
        GTK_BOX(selection_tools), clear_selection);
    gtk_box_append(
        GTK_BOX(heading_row), selection_tools);
    gtk_box_append(GTK_BOX(card), heading_row);

    GtkWidget *list = gtk_list_box_new();
    state->updates.updates_list = GTK_LIST_BOX(list);
    gtk_widget_add_css_class(list, "package-list");
    gtk_list_box_set_selection_mode(
        state->updates.updates_list, GTK_SELECTION_NONE);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, true);
    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(
        GTK_SCROLLED_WINDOW(scroll), list);
    gtk_box_append(GTK_BOX(card), scroll);

    gtk_box_append(GTK_BOX(page), card);

    GtkWidget *external_card =
        gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(
        external_card, "card");
    gtk_widget_add_css_class(
        external_card, "card-info");

    GtkWidget *external_heading =
        gtk_box_new(
            GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *external_icon =
        make_icon(
            "applications-system-symbolic", 22);
    gtk_box_append(
        GTK_BOX(external_heading),
        external_icon);

    GtkWidget *external_copy =
        gtk_box_new(
            GTK_ORIENTATION_VERTICAL, 1);
    gtk_widget_set_hexpand(
        external_copy, true);
    gtk_box_append(
        GTK_BOX(external_copy),
        make_label(
            "Flatpak & Cinnamon updates",
            "card-title"));
    state->updates.external.external_updates_status =
        make_label(
            "Checking Flatpak and Cinnamon sources…",
            "card-copy");
    gtk_box_append(
        GTK_BOX(external_copy),
        state->updates.external.external_updates_status);
    gtk_box_append(
        GTK_BOX(external_heading),
        external_copy);

    state->updates.external.external_updates_spinner = gtk_spinner_new();
    gtk_widget_set_size_request(state->updates.external.external_updates_spinner, 24, 24);
    gtk_widget_set_visible(state->updates.external.external_updates_spinner, false);
    gtk_box_append(GTK_BOX(external_heading),
        state->updates.external.external_updates_spinner);

    state->updates.external.external_flatpak_apply =
        gtk_button_new_with_label(
            "Update selected Flatpaks");
    gtk_widget_add_css_class(
        state->updates.external.external_flatpak_apply,
        "discover-details");
    gtk_widget_set_sensitive(
        state->updates.external.external_flatpak_apply,
        false);
    g_signal_connect(
        state->updates.external.external_flatpak_apply,
        "clicked",
        G_CALLBACK(external_apply_clicked),
        state);
    gtk_box_append(
        GTK_BOX(external_heading),
        state->updates.external.external_flatpak_apply);

    state->updates.external.external_cinnamon_apply =
        gtk_button_new_with_label(
            "Update selected Cinnamon");
    gtk_widget_add_css_class(
        state->updates.external.external_cinnamon_apply,
        "discover-details");
    gtk_widget_set_sensitive(
        state->updates.external.external_cinnamon_apply,
        false);
    g_signal_connect(
        state->updates.external.external_cinnamon_apply,
        "clicked",
        G_CALLBACK(external_apply_clicked),
        state);
    gtk_box_append(
        GTK_BOX(external_heading),
        state->updates.external.external_cinnamon_apply);

    gtk_box_append(
        GTK_BOX(external_card),
        external_heading);

    GtkWidget *external_list =
        gtk_list_box_new();
    state->updates.external.external_updates_list =
        GTK_LIST_BOX(external_list);
    gtk_widget_add_css_class(
        external_list, "package-list");
    gtk_list_box_set_selection_mode(
        state->updates.external.external_updates_list,
        GTK_SELECTION_NONE);
    gtk_box_append(
        GTK_BOX(external_card),
        external_list);

    gtk_box_append(
        GTK_BOX(page),
        external_card);

    return page;
}



} // namespace infiltrator::software::app
