// SPDX-License-Identifier: GPL-3.0-or-later
#include "client/engine_client.hpp"

#include <gio/gio.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace infiltrator::software {
namespace {

constexpr const char *kBusName =
    "net.ssmith.infiltrator.software.Engine";
constexpr const char *kObjectPath =
    "/net/ssmith/infiltrator/software/Engine";
constexpr const char *kInterfaceName =
    "net.ssmith.infiltrator.software.Engine";
constexpr int kCallTimeoutMs = 10000;

std::string consume_error(GError *error)
{
    if (error == nullptr) {
        return "Package engine call failed.";
    }

    (void)g_dbus_error_strip_remote_error(error);
    const std::string message =
        error->message == nullptr
            ? "Package engine call failed."
            : std::string(error->message);
    g_error_free(error);
    return message;
}

GVariant *call_engine(
    const char *method,
    GVariant *parameters,
    const GVariantType *reply_type,
    std::string &error)
{
    error.clear();

    GError *gerror = nullptr;
    GDBusConnection *connection =
        g_bus_get_sync(
            G_BUS_TYPE_SESSION,
            nullptr,
            &gerror);
    if (connection == nullptr) {
        error = consume_error(gerror);
        return nullptr;
    }

    GVariant *reply =
        g_dbus_connection_call_sync(
            connection,
            kBusName,
            kObjectPath,
            kInterfaceName,
            method,
            parameters,
            reply_type,
            G_DBUS_CALL_FLAGS_NONE,
            kCallTimeoutMs,
            nullptr,
            &gerror);
    g_object_unref(connection);

    if (reply == nullptr) {
        error = consume_error(gerror);
        return nullptr;
    }
    return reply;
}

std::string lookup_string(
    GVariant *dictionary,
    const char *key)
{
    const gchar *value = nullptr;
    if (dictionary != nullptr &&
        g_variant_lookup(
            dictionary, key, "&s", &value) &&
        value != nullptr) {
        return value;
    }
    return {};
}

std::uint64_t lookup_u64(
    GVariant *dictionary,
    const char *key)
{
    guint64 value = 0U;
    if (dictionary != nullptr) {
        (void)g_variant_lookup(
            dictionary, key, "t", &value);
    }
    return static_cast<std::uint64_t>(value);
}

std::int64_t lookup_i64(
    GVariant *dictionary,
    const char *key)
{
    gint64 value = 0;
    if (dictionary != nullptr) {
        (void)g_variant_lookup(
            dictionary, key, "x", &value);
    }
    return static_cast<std::int64_t>(value);
}

bool lookup_bool(
    GVariant *dictionary,
    const char *key)
{
    gboolean value = FALSE;
    if (dictionary != nullptr) {
        (void)g_variant_lookup(
            dictionary, key, "b", &value);
    }
    return value != FALSE;
}

PackageKind parse_kind(const std::string_view value) noexcept
{
    if (value == "Application") return PackageKind::application;
    if (value == "System") return PackageKind::system;
    if (value == "Library") return PackageKind::library;
    if (value == "Driver") return PackageKind::driver;
    if (value == "Kernel") return PackageKind::kernel;
    if (value == "Runtime") return PackageKind::runtime;
    return PackageKind::unknown;
}

TransactionAction parse_action(
    const std::string_view value) noexcept
{
    if (value == "Upgrade") {
        return TransactionAction::upgrade;
    }
    if (value == "Remove") {
        return TransactionAction::remove;
    }
    return TransactionAction::install;
}

PackageRecord parse_package(GVariant *dictionary)
{
    PackageRecord package;
    package.id = lookup_string(dictionary, "id");
    package.name = lookup_string(dictionary, "name");
    package.package_name =
        lookup_string(dictionary, "package-name");
    package.architecture =
        lookup_string(dictionary, "architecture");
    package.installed_version =
        lookup_string(dictionary, "installed-version");
    package.available_version =
        lookup_string(dictionary, "available-version");
    package.source = lookup_string(dictionary, "source");
    package.asset = lookup_string(dictionary, "filename");
    package.package_sha256 =
        lookup_string(dictionary, "sha256");
    package.installed_size_bytes =
        lookup_u64(dictionary, "installed-size-bytes");
    package.download_size_bytes =
        lookup_u64(dictionary, "download-bytes");
    package.system_critical =
        lookup_bool(dictionary, "system-critical");
    package.kind =
        parse_kind(lookup_string(dictionary, "kind"));

    if (!package.available_version.empty() &&
        package.available_version !=
            package.installed_version) {
        package.state = InstallState::upgradable;
    } else if (!package.installed_version.empty()) {
        package.state = InstallState::installed;
    }
    return package;
}

bool parse_packages_reply(
    GVariant *reply,
    std::vector<PackageRecord> &packages,
    std::string &error)
{
    packages.clear();
    if (reply == nullptr) {
        error = "Package engine returned no package reply.";
        return false;
    }

    GVariant *array = nullptr;
    g_variant_get(reply, "(@aa{sv})", &array);
    g_variant_unref(reply);
    if (array == nullptr) {
        error = "Package engine returned an invalid package array.";
        return false;
    }

    GVariantIter iterator;
    g_variant_iter_init(&iterator, array);
    GVariant *dictionary = nullptr;
    while ((dictionary =
                g_variant_iter_next_value(&iterator)) != nullptr) {
        PackageRecord package =
            parse_package(dictionary);
        g_variant_unref(dictionary);
        if (package.id.empty() ||
            package.package_name.empty()) {
            g_variant_unref(array);
            packages.clear();
            error =
                "Package engine returned a package without a stable identity.";
            return false;
        }
        if (package.name.empty()) {
            package.name = package.package_name;
        }
        packages.emplace_back(std::move(package));
    }

    g_variant_unref(array);
    error.clear();
    return true;
}

TransactionItem parse_item(GVariant *dictionary)
{
    TransactionItem item;
    item.package_id =
        lookup_string(dictionary, "package-id");
    item.action =
        parse_action(lookup_string(dictionary, "action"));
    item.architecture =
        lookup_string(dictionary, "architecture");
    item.from_version =
        lookup_string(dictionary, "from-version");
    item.to_version =
        lookup_string(dictionary, "to-version");
    item.source =
        lookup_string(dictionary, "source");
    item.filename =
        lookup_string(dictionary, "filename");
    item.sha256 =
        lookup_string(dictionary, "sha256");
    item.disk_delta_bytes =
        lookup_i64(dictionary, "disk-delta-bytes");
    item.download_bytes =
        lookup_u64(dictionary, "download-bytes");
    item.requested =
        lookup_bool(dictionary, "requested");
    item.system_critical =
        lookup_bool(dictionary, "system-critical");
    return item;
}

std::string action_text(const TransactionAction action)
{
    switch (action) {
    case TransactionAction::install: return "install";
    case TransactionAction::upgrade: return "upgrade";
    case TransactionAction::remove: return "remove";
    }
    return {};
}

} // namespace

bool EngineClient::list_installed(
    std::vector<PackageRecord> &packages,
    std::string &error) const
{
    GVariant *reply =
        call_engine(
            "ListInstalled",
            nullptr,
            G_VARIANT_TYPE("(aa{sv})"),
            error);
    return reply != nullptr &&
           parse_packages_reply(
               reply, packages, error);
}

bool EngineClient::list_updates(
    std::vector<PackageRecord> &packages,
    std::string &error) const
{
    GVariant *reply =
        call_engine(
            "ListUpdates",
            nullptr,
            G_VARIANT_TYPE("(aa{sv})"),
            error);
    return reply != nullptr &&
           parse_packages_reply(
               reply, packages, error);
}

std::optional<TransactionPlan> EngineClient::plan(
    const TransactionRequest &request,
    std::string &error) const
{
    const std::string action =
        action_text(request.action);
    if (action.empty() ||
        request.package_ids.empty()) {
        error = "Invalid package-engine transaction request.";
        return std::nullopt;
    }

    std::vector<const gchar *> ids;
    ids.reserve(request.package_ids.size() + 1U);
    for (const std::string &id : request.package_ids) {
        ids.push_back(id.c_str());
    }
    ids.push_back(nullptr);

    GVariant *reply =
        call_engine(
            "PlanTransaction",
            g_variant_new(
                "(s^as)",
                action.c_str(),
                ids.data()),
            G_VARIANT_TYPE("(a{sv})"),
            error);
    if (reply == nullptr) {
        return std::nullopt;
    }

    GVariant *dictionary = nullptr;
    g_variant_get(reply, "(@a{sv})", &dictionary);
    g_variant_unref(reply);
    if (dictionary == nullptr) {
        error = "Package engine returned an invalid transaction plan.";
        return std::nullopt;
    }

    TransactionPlan plan;
    plan.state_generation =
        lookup_u64(dictionary, "state-generation");
    plan.source_fingerprint =
        lookup_string(dictionary, "source-fingerprint");
    plan.download_bytes =
        lookup_u64(dictionary, "download-bytes");
    plan.disk_delta_bytes =
        lookup_i64(dictionary, "disk-delta-bytes");
    plan.touches_system =
        lookup_bool(dictionary, "touches-system");

    GVariant *items =
        g_variant_lookup_value(
            dictionary,
            "items",
            G_VARIANT_TYPE("aa{sv}"));
    if (items == nullptr) {
        g_variant_unref(dictionary);
        error =
            "Package engine transaction plan contains no item array.";
        return std::nullopt;
    }

    GVariantIter iterator;
    g_variant_iter_init(&iterator, items);
    GVariant *item_dictionary = nullptr;
    while ((item_dictionary =
                g_variant_iter_next_value(&iterator)) != nullptr) {
        TransactionItem item =
            parse_item(item_dictionary);
        g_variant_unref(item_dictionary);
        if (item.package_id.empty()) {
            g_variant_unref(items);
            g_variant_unref(dictionary);
            error =
                "Package engine transaction plan contains an invalid item.";
            return std::nullopt;
        }
        plan.items.emplace_back(std::move(item));
    }

    g_variant_unref(items);
    g_variant_unref(dictionary);

    if (plan.items.empty()) {
        error = "Package engine returned an empty transaction plan.";
        return std::nullopt;
    }

    error.clear();
    return plan;
}

bool EngineClient::reload(std::string &error) const
{
    GVariant *reply =
        call_engine(
            "ReloadState",
            nullptr,
            G_VARIANT_TYPE("(a{sv})"),
            error);
    if (reply == nullptr) {
        return false;
    }
    g_variant_unref(reply);
    error.clear();
    return true;
}

} // namespace infiltrator::software
