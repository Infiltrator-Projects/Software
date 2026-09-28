// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/repository_controller.hpp"

#include <curl/curl.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#ifndef INFILTRATOR_SOFTWARE_VERSION
#define INFILTRATOR_SOFTWARE_VERSION "0.0.0"
#endif

namespace infiltrator::software {
namespace {

struct RepositoryResult {
    unsigned int generation{0U};
    std::vector<SourceRecord> sources;
    std::string mirror_status;
    std::string error;
};

struct RepositoryTaskData {
    unsigned int generation{0U};
};

std::string one_line(std::string value)
{
    for (char &ch : value) {
        if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
    }
    return value;
}

std::optional<curl_off_t> url_file_time(
    const std::string &url)
{
    CURL *curl = curl_easy_init();
    if (curl == nullptr) return std::nullopt;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_FILETIME, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 6L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 12L);
    curl_easy_setopt(
        curl,
        CURLOPT_USERAGENT,
        "Infiltrator-Software/" INFILTRATOR_SOFTWARE_VERSION);

    const CURLcode code = curl_easy_perform(curl);
    long response = 0L;
    curl_off_t file_time = -1;
    (void)curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response);
    (void)curl_easy_getinfo(curl, CURLINFO_FILETIME_T, &file_time);
    curl_easy_cleanup(curl);

    if (code != CURLE_OK ||
        response < 200L ||
        response >= 400L ||
        file_time < 0) {
        return std::nullopt;
    }
    return file_time;
}

std::string mint_mirror_status()
{
    const std::filesystem::path sources{
        "/etc/apt/sources.list.d/official-package-repositories.list"};
    std::ifstream input(sources);
    if (!input) return {};

    std::string mirror;
    std::string line;
    while (std::getline(input, line)) {
        const std::string clean = one_line(line);
        if (clean.rfind("deb ", 0U) != 0U ||
            clean.find("main upstream import") == std::string::npos) {
            continue;
        }
        std::istringstream words(clean);
        std::string deb;
        words >> deb >> mirror;
        break;
    }
    while (!mirror.empty() && mirror.back() == '/') mirror.pop_back();
    if (mirror.empty()) return {};

    if (mirror == "http://packages.linuxmint.com" ||
        mirror == "https://packages.linuxmint.com") {
        return "The default Linux Mint repository is in use. A local mirror may be faster; use Mint mirrors… to choose one.";
    }

    const auto reference =
        url_file_time("https://packages.linuxmint.com/db/version");
    const auto selected =
        url_file_time(mirror + "/db/version");

    if (reference.has_value() && !selected.has_value()) {
        return mirror +
            " is unreachable. Use Mint mirrors… to choose another mirror.";
    }
    if (reference.has_value() && selected.has_value()) {
        static constexpr curl_off_t day = 24 * 60 * 60;
        if (*reference - *selected > 2 * day) {
            const curl_off_t days = (*reference - *selected) / day;
            return mirror + " is about " +
                std::to_string(static_cast<long long>(days)) +
                " days behind the Linux Mint reference repository. Use Mint mirrors… to switch.";
        }
        return "Linux Mint mirror is reachable and current.";
    }
    return {};
}

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
    if (result->error.empty()) {
        result->mirror_status = mint_mirror_status();
    }

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
    gpointer user_data)
{
    auto *controller =
        static_cast<RepositoryController *>(user_data);
    auto *result =
        static_cast<RepositoryResult *>(
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
    controller->records = std::move(result->sources);

    if (controller->flow != nullptr) {
        GtkWidget *child =
            gtk_widget_get_first_child(controller->flow);
        while (child != nullptr) {
            GtkWidget *next =
                gtk_widget_get_next_sibling(child);
            gtk_flow_box_remove(
                GTK_FLOW_BOX(controller->flow), child);
            child = next;
        }

        if (controller->make_card != nullptr) {
            for (const SourceRecord &source : controller->records) {
                GtkWidget *card =
                    controller->make_card(
                        controller->callback_data,
                        source);
                if (card != nullptr) {
                    gtk_flow_box_append(
                        GTK_FLOW_BOX(controller->flow),
                        card);
                }
            }
        }
    }

    std::size_t enabled = 0U;
    for (const SourceRecord &source : controller->records) {
        if (source.enabled) ++enabled;
    }

    if (controller->count != nullptr) {
        const std::string count =
            std::to_string(controller->records.size());
        gtk_label_set_text(
            GTK_LABEL(controller->count),
            count.c_str());
    }

    if (controller->status != nullptr) {
        if (!result->error.empty()) {
            gtk_label_set_text(
                GTK_LABEL(controller->status),
                result->error.c_str());
        } else {
            std::ostringstream status;
            status << enabled << " enabled source"
                   << (enabled == 1U ? "" : "s")
                   << " detected. APT sources and Flatpak remotes feed Discover.";
            if (!result->mirror_status.empty()) {
                status << "  " << result->mirror_status;
            }
            gtk_label_set_text(
                GTK_LABEL(controller->status),
                status.str().c_str());
        }
    }

    if (controller->changed != nullptr) {
        controller->changed(controller->callback_data);
    }

    delete result;
}

} // namespace

void configure_repository_controller(
    RepositoryController *controller,
    GtkWindow *window,
    GtkWidget *flow,
    GtkWidget *count,
    GtkWidget *status,
    GtkWidget *(*make_card)(gpointer, const SourceRecord &),
    void (*changed)(gpointer),
    gpointer callback_data)
{
    if (controller == nullptr) return;
    controller->window = window;
    controller->flow = flow;
    controller->count = count;
    controller->status = status;
    controller->make_card = make_card;
    controller->changed = changed;
    controller->callback_data = callback_data;
}

void refresh_repository_controller(
    RepositoryController *controller)
{
    if (controller == nullptr ||
        controller->window == nullptr ||
        controller->flow == nullptr ||
        controller->busy) {
        return;
    }

    controller->loaded = true;
    controller->busy = true;
    ++controller->generation;

    if (controller->status != nullptr) {
        gtk_label_set_text(
            GTK_LABEL(controller->status),
            "Reading configured software sources…");
    }

    auto *data = new RepositoryTaskData{
        controller->generation};
    GTask *task =
        g_task_new(
            G_OBJECT(controller->window),
            nullptr,
            repositories_complete,
            controller);
    g_task_set_task_data(
        task,
        data,
        [](gpointer pointer) {
            delete static_cast<RepositoryTaskData *>(pointer);
        });
    g_task_run_in_thread(task, repositories_worker);
    g_object_unref(task);
}

} // namespace infiltrator::software
