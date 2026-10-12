// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/page_support.hpp"
#include "app/ui_components.hpp"
#include "core/transaction_history.hpp"
#include <infiltratr/core.h>
#include <cctype>
#include <filesystem>

namespace infiltrator::software::app {
using infiltrator::software::TransactionHistoryStore;
using infiltrator::software::make_label;

namespace {
std::filesystem::path transaction_history_path()
{
    const std::string path =
        infiltrator::software::user_transaction_history_path();
    return path.empty() ? std::filesystem::path{} : std::filesystem::path(path);
}
} // namespace

std::string display_size(const std::uint64_t bytes)
{
    char text[32];
    const char *formatted = infiltratr_format_bytes(bytes, text, sizeof(text));
    return formatted != nullptr ? std::string(formatted) : std::string();
}

std::string one_line(std::string value)
{
    for (char &ch : value) {
        if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
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
        return "view-refresh-symbolic";
    }
    return "view-refresh-symbolic";
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

void record_transaction_history(
    const TransactionPlan &plan,
    const bool success,
    const std::string_view message)
{
    const std::filesystem::path path = transaction_history_path();
    if (path.empty() || plan.items.empty()) return;
    TransactionHistoryStore store(path.string());
    std::string error;
    if (!store.append(plan, success, message, error)) {
        g_warning("Unable to record Software transaction history: %s", error.c_str());
    }
}

} // namespace infiltrator::software::app
