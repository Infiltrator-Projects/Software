// SPDX-License-Identifier: GPL-3.0-or-later
#include "client/engine_client.hpp"

#include <gio/gio.h>

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <string>
#include <unistd.h>
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
constexpr int kInventoryCallTimeoutMs = 750;
constexpr int kControlCallTimeoutMs = 5000;
constexpr int kRefreshCallTimeoutMs = 125000;
constexpr guint32 kRequiredApiVersion = 2U;
constexpr const char *kRequiredEngineVersion =
    INFILTRATOR_SOFTWARE_VERSION;
constexpr guint kEngineRestartAttempts = 40U;
constexpr gulong kEngineRestartDelayUs = 50000U;

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
    const int timeout_ms,
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
            timeout_ms,
            nullptr,
            &gerror);
    g_object_unref(connection);

    if (reply == nullptr) {
        error = consume_error(gerror);
        return nullptr;
    }
    return reply;
}

bool engine_identity(
    guint32 &api_version,
    std::string &engine_version,
    std::string &error)
{
    api_version = 0U;
    engine_version.clear();
    GVariant *reply =
        call_engine(
            "GetStatus",
            nullptr,
            G_VARIANT_TYPE("(a{sv})"),
            kControlCallTimeoutMs,
            error);
    if (reply == nullptr) {
        return false;
    }

    GVariant *dictionary = nullptr;
    g_variant_get(reply, "(@a{sv})", &dictionary);
    g_variant_unref(reply);
    if (dictionary == nullptr) {
        error =
            "Package engine returned an invalid status reply.";
        return false;
    }

    const gboolean api_found =
        g_variant_lookup(
            dictionary,
            "api-version",
            "u",
            &api_version);
    const gchar *version_text = nullptr;
    const gboolean version_found =
        g_variant_lookup(
            dictionary,
            "engine-version",
            "&s",
            &version_text);
    if (version_found && version_text != nullptr) {
        engine_version = version_text;
    }
    g_variant_unref(dictionary);

    if (!api_found || api_version == 0U) {
        api_version = 0U;
        error =
            "Package engine did not report an API version.";
        return false;
    }

    error.clear();
    return true;
}

bool engine_owner_value(
    const char *method,
    guint32 &value,
    std::string &error)
{
    value = 0U;
    GError *gerror = nullptr;
    GDBusConnection *connection =
        g_bus_get_sync(
            G_BUS_TYPE_SESSION,
            nullptr,
            &gerror);
    if (connection == nullptr) {
        error = consume_error(gerror);
        return false;
    }

    GVariant *reply =
        g_dbus_connection_call_sync(
            connection,
            "org.freedesktop.DBus",
            "/org/freedesktop/DBus",
            "org.freedesktop.DBus",
            method,
            g_variant_new("(s)", kBusName),
            G_VARIANT_TYPE("(u)"),
            G_DBUS_CALL_FLAGS_NONE,
            kControlCallTimeoutMs,
            nullptr,
            &gerror);
    g_object_unref(connection);
    if (reply == nullptr) {
        error = consume_error(gerror);
        return false;
    }

    g_variant_get(reply, "(u)", &value);
    g_variant_unref(reply);
    error.clear();
    return true;
}

bool wait_for_engine_identity(
    const guint32 required_api_version,
    const std::string_view required_engine_version,
    std::string &error)
{
    for (guint attempt = 0U;
         attempt < kEngineRestartAttempts;
         ++attempt) {
        guint32 api_version = 0U;
        std::string engine_version;
        std::string probe_error;
        if (engine_identity(
                api_version,
                engine_version,
                probe_error) &&
            api_version >= required_api_version &&
            engine_version == required_engine_version) {
            error.clear();
            return true;
        }

        if (attempt + 1U < kEngineRestartAttempts) {
            g_usleep(kEngineRestartDelayUs);
        }
    }

    error =
        "Package engine did not restart as version " +
        std::string(required_engine_version) +
        " with API version " +
        std::to_string(required_api_version) + ".";
    return false;
}

bool recycle_engine_service(
    const guint32 required_api_version,
    const std::string_view required_engine_version,
    std::string &error)
{
    /*
     * D-Bus activation does not replace an already running per-user service
     * when a package upgrade installs a newer engine binary. Ask newer
     * engines to quit cleanly. Pre-v2 engines have no Quit method, so verify
     * the bus owner's uid before terminating that same-user stale process.
     */
    std::string quit_error;
    GVariant *quit_reply =
        call_engine(
            "Quit",
            nullptr,
            G_VARIANT_TYPE("()"),
            kControlCallTimeoutMs,
            quit_error);
    if (quit_reply != nullptr) {
        g_variant_unref(quit_reply);
        return wait_for_engine_identity(
            required_api_version,
            required_engine_version,
            error);
    }

    guint32 owner_uid = 0U;
    if (!engine_owner_value(
            "GetConnectionUnixUser",
            owner_uid,
            error)) {
        return false;
    }
    if (owner_uid != static_cast<guint32>(getuid())) {
        error =
            "Refusing to restart a package engine owned by another user.";
        return false;
    }

    guint32 owner_pid = 0U;
    if (!engine_owner_value(
            "GetConnectionUnixProcessID",
            owner_pid,
            error)) {
        return false;
    }
    if (owner_pid == 0U ||
        owner_pid == static_cast<guint32>(getpid())) {
        error =
            "Package engine reported an invalid process identity.";
        return false;
    }

    if (::kill(
            static_cast<pid_t>(owner_pid),
            SIGTERM) != 0 &&
        errno != ESRCH) {
        error =
            "Unable to stop the stale package engine process.";
        return false;
    }

    return wait_for_engine_identity(
        required_api_version,
        required_engine_version,
        error);
}

bool ensure_engine_identity(std::string &error)
{
    guint32 api_version = 0U;
    std::string engine_version;
    if (!engine_identity(
            api_version,
            engine_version,
            error)) {
        return false;
    }

    if (api_version >= kRequiredApiVersion &&
        engine_version == kRequiredEngineVersion) {
        error.clear();
        return true;
    }

    return recycle_engine_service(
        kRequiredApiVersion,
        kRequiredEngineVersion,
        error);
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

std::int32_t lookup_i32(
    GVariant *dictionary,
    const char *key)
{
    gint32 value = 0;
    if (dictionary != nullptr) {
        (void)g_variant_lookup(
            dictionary, key, "i", &value);
    }
    return static_cast<std::int32_t>(value);
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
    package.repository_origin =
        lookup_string(dictionary, "repository-origin");
    package.repository_site =
        lookup_string(dictionary, "repository-site");
    package.policy_provider =
        lookup_string(dictionary, "policy-provider");
    package.policy_reason =
        lookup_string(dictionary, "policy-reason");
    package.selection_reason =
        lookup_string(dictionary, "selection-reason");
    package.candidate_priority =
        lookup_i32(dictionary, "candidate-priority");
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
    if (!ensure_engine_identity(error)) {
        packages.clear();
        return false;
    }

    GVariant *reply =
        call_engine(
            "ListInstalled",
            nullptr,
            G_VARIANT_TYPE("(aa{sv})"),
            kInventoryCallTimeoutMs,
            error);
    return reply != nullptr &&
           parse_packages_reply(
               reply, packages, error);
}

bool EngineClient::list_updates(
    std::vector<PackageRecord> &packages,
    std::string &error) const
{
    if (!ensure_engine_identity(error)) {
        packages.clear();
        return false;
    }

    GVariant *reply =
        call_engine(
            "ListUpdates",
            nullptr,
            G_VARIANT_TYPE("(aa{sv})"),
            kInventoryCallTimeoutMs,
            error);
    return reply != nullptr &&
           parse_packages_reply(
               reply, packages, error);
}

std::optional<TransactionPlan> EngineClient::plan(
    const TransactionRequest &request,
    std::string &error) const
{
    if (!ensure_engine_identity(error)) {
        return std::nullopt;
    }

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
            kControlCallTimeoutMs,
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
    if (!ensure_engine_identity(error)) {
        return false;
    }

    GVariant *reply =
        call_engine(
            "ReloadState",
            nullptr,
            G_VARIANT_TYPE("(a{sv})"),
            kControlCallTimeoutMs,
            error);
    if (reply == nullptr) {
        return false;
    }
    g_variant_unref(reply);
    error.clear();
    return true;
}

bool EngineClient::refresh_installed(std::string &error) const
{
    if (!ensure_engine_identity(error)) {
        return false;
    }

    GVariant *reply =
        call_engine(
            "RefreshInstalledState",
            nullptr,
            G_VARIANT_TYPE("(a{sv})"),
            15000,
            error);
    if (reply == nullptr) {
        return false;
    }
    g_variant_unref(reply);
    error.clear();
    return true;
}

bool EngineClient::refresh(std::string &error) const
{
    if (!ensure_engine_identity(error)) {
        return false;
    }

    GVariant *reply =
        call_engine(
            "RefreshState",
            nullptr,
            G_VARIANT_TYPE("(a{sv})"),
            kRefreshCallTimeoutMs,
            error);
    if (reply == nullptr) {
        return false;
    }
    g_variant_unref(reply);
    error.clear();
    return true;
}

} // namespace infiltrator::software
