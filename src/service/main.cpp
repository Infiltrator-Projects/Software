// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/engine_service_core.hpp"

#include <gio/gio.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using infiltrator::software::EngineServiceCore;
using infiltrator::software::EngineServiceStatus;
using infiltrator::software::PackageRecord;
using infiltrator::software::TransactionAction;
using infiltrator::software::TransactionItem;
using infiltrator::software::TransactionPlan;
using infiltrator::software::TransactionRequest;

constexpr const char *kBusName =
    "net.ssmith.infiltrator.software.Engine";
constexpr const char *kObjectPath =
    "/net/ssmith/infiltrator/software/Engine";
constexpr const char *kInterfaceName =
    "net.ssmith.infiltrator.software.Engine";
constexpr guint kApiVersion = 1U;
constexpr std::size_t kMaximumPlanPackages = 4096U;

constexpr const char *kIntrospectionXml = R"XML(
<node>
  <interface name="net.ssmith.infiltrator.software.Engine">
    <method name="GetStatus">
      <arg name="status" type="a{sv}" direction="out"/>
    </method>
    <method name="ListInstalled">
      <arg name="packages" type="aa{sv}" direction="out"/>
    </method>
    <method name="ListUpdates">
      <arg name="packages" type="aa{sv}" direction="out"/>
    </method>
    <method name="PlanTransaction">
      <arg name="action" type="s" direction="in"/>
      <arg name="package_ids" type="as" direction="in"/>
      <arg name="plan" type="a{sv}" direction="out"/>
    </method>
    <method name="ReloadState">
      <arg name="status" type="a{sv}" direction="out"/>
    </method>
    <method name="RefreshState">
      <arg name="status" type="a{sv}" direction="out"/>
    </method>
    <signal name="StateChanged">
      <arg name="generation" type="t"/>
    </signal>
    <signal name="HealthChanged">
      <arg name="healthy" type="b"/>
      <arg name="detail" type="s"/>
    </signal>
  </interface>
</node>
)XML";

struct ServiceState {
    explicit ServiceState(std::string database_path)
        : core(std::move(database_path))
    {
    }

    EngineServiceCore core;
    GMainLoop *loop{};
    GDBusConnection *connection{};
    GDBusNodeInfo *node_info{};
    GFileMonitor *monitor{};
    guint registration_id{0U};
    guint debounce_id{0U};
    guint fallback_poll_id{0U};
    EngineServiceStatus last_status;
};

GVariant *status_variant(const EngineServiceStatus &status)
{
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(
        &builder, "{sv}", "api-version",
        g_variant_new_uint32(kApiVersion));
    g_variant_builder_add(
        &builder, "{sv}", "generation",
        g_variant_new_uint64(status.generation));
    g_variant_builder_add(
        &builder, "{sv}", "published-at-unix",
        g_variant_new_int64(status.published_at_unix));
    g_variant_builder_add(
        &builder, "{sv}", "source-fingerprint",
        g_variant_new_string(status.source_fingerprint.c_str()));
    g_variant_builder_add(
        &builder, "{sv}", "installed-count",
        g_variant_new_uint64(
            static_cast<guint64>(status.installed_count)));
    g_variant_builder_add(
        &builder, "{sv}", "available-count",
        g_variant_new_uint64(
            static_cast<guint64>(status.available_count)));
    g_variant_builder_add(
        &builder, "{sv}", "update-count",
        g_variant_new_uint64(
            static_cast<guint64>(status.update_count)));
    g_variant_builder_add(
        &builder, "{sv}", "healthy",
        g_variant_new_boolean(status.healthy));
    g_variant_builder_add(
        &builder, "{sv}", "detail",
        g_variant_new_string(status.detail.c_str()));
    return g_variant_builder_end(&builder);
}

GVariant *package_variant(const PackageRecord &package)
{
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(
        &builder, "{sv}", "id",
        g_variant_new_string(package.id.c_str()));
    g_variant_builder_add(
        &builder, "{sv}", "name",
        g_variant_new_string(package.name.c_str()));
    g_variant_builder_add(
        &builder, "{sv}", "package-name",
        g_variant_new_string(package.package_name.c_str()));
    g_variant_builder_add(
        &builder, "{sv}", "architecture",
        g_variant_new_string(package.architecture.c_str()));
    g_variant_builder_add(
        &builder, "{sv}", "installed-version",
        g_variant_new_string(package.installed_version.c_str()));
    g_variant_builder_add(
        &builder, "{sv}", "available-version",
        g_variant_new_string(package.available_version.c_str()));
    g_variant_builder_add(
        &builder, "{sv}", "source",
        g_variant_new_string(package.source.c_str()));
    g_variant_builder_add(
        &builder, "{sv}", "filename",
        g_variant_new_string(package.asset.c_str()));
    g_variant_builder_add(
        &builder, "{sv}", "sha256",
        g_variant_new_string(package.package_sha256.c_str()));
    g_variant_builder_add(
        &builder, "{sv}", "installed-size-bytes",
        g_variant_new_uint64(package.installed_size_bytes));
    g_variant_builder_add(
        &builder, "{sv}", "download-bytes",
        g_variant_new_uint64(package.download_size_bytes));
    g_variant_builder_add(
        &builder, "{sv}", "system-critical",
        g_variant_new_boolean(package.system_critical));
    g_variant_builder_add(
        &builder, "{sv}", "kind",
        g_variant_new_string(
            std::string(
                infiltrator::software::package_kind_name(
                    package.kind)).c_str()));
    return g_variant_builder_end(&builder);
}

GVariant *packages_variant(
    const std::vector<PackageRecord> &packages)
{
    GVariantBuilder builder;
    g_variant_builder_init(
        &builder,
        G_VARIANT_TYPE("aa{sv}"));
    for (const PackageRecord &package : packages) {
        g_variant_builder_add_value(
            &builder,
            package_variant(package));
    }
    return g_variant_builder_end(&builder);
}

GVariant *item_variant(const TransactionItem &item)
{
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(
        &builder, "{sv}", "package-id",
        g_variant_new_string(item.package_id.c_str()));
    g_variant_builder_add(
        &builder, "{sv}", "action",
        g_variant_new_string(
            std::string(
                infiltrator::software::transaction_action_name(
                    item.action)).c_str()));
    g_variant_builder_add(
        &builder, "{sv}", "architecture",
        g_variant_new_string(item.architecture.c_str()));
    g_variant_builder_add(
        &builder, "{sv}", "from-version",
        g_variant_new_string(item.from_version.c_str()));
    g_variant_builder_add(
        &builder, "{sv}", "to-version",
        g_variant_new_string(item.to_version.c_str()));
    g_variant_builder_add(
        &builder, "{sv}", "source",
        g_variant_new_string(item.source.c_str()));
    g_variant_builder_add(
        &builder, "{sv}", "filename",
        g_variant_new_string(item.filename.c_str()));
    g_variant_builder_add(
        &builder, "{sv}", "sha256",
        g_variant_new_string(item.sha256.c_str()));
    g_variant_builder_add(
        &builder, "{sv}", "disk-delta-bytes",
        g_variant_new_int64(item.disk_delta_bytes));
    g_variant_builder_add(
        &builder, "{sv}", "download-bytes",
        g_variant_new_uint64(item.download_bytes));
    g_variant_builder_add(
        &builder, "{sv}", "requested",
        g_variant_new_boolean(item.requested));
    g_variant_builder_add(
        &builder, "{sv}", "system-critical",
        g_variant_new_boolean(item.system_critical));
    return g_variant_builder_end(&builder);
}

GVariant *plan_variant(const TransactionPlan &plan)
{
    GVariantBuilder items;
    g_variant_builder_init(
        &items,
        G_VARIANT_TYPE("aa{sv}"));
    for (const TransactionItem &item : plan.items) {
        g_variant_builder_add_value(
            &items,
            item_variant(item));
    }

    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(
        &builder, "{sv}", "state-generation",
        g_variant_new_uint64(plan.state_generation));
    g_variant_builder_add(
        &builder, "{sv}", "source-fingerprint",
        g_variant_new_string(plan.source_fingerprint.c_str()));
    g_variant_builder_add(
        &builder, "{sv}", "download-bytes",
        g_variant_new_uint64(plan.download_bytes));
    g_variant_builder_add(
        &builder, "{sv}", "disk-delta-bytes",
        g_variant_new_int64(plan.disk_delta_bytes));
    g_variant_builder_add(
        &builder, "{sv}", "touches-system",
        g_variant_new_boolean(plan.touches_system));
    g_variant_builder_add(
        &builder, "{sv}", "items",
        g_variant_builder_end(&items));
    return g_variant_builder_end(&builder);
}

void emit_state_changed(
    ServiceState *state,
    const std::uint64_t generation)
{
    if (state == nullptr ||
        state->connection == nullptr) {
        return;
    }
    GError *error = nullptr;
    g_dbus_connection_emit_signal(
        state->connection,
        nullptr,
        kObjectPath,
        kInterfaceName,
        "StateChanged",
        g_variant_new("(t)", generation),
        &error);
    if (error != nullptr) {
        g_warning("Unable to emit StateChanged: %s", error->message);
        g_error_free(error);
    }
}

void emit_health_changed(
    ServiceState *state,
    const EngineServiceStatus &status)
{
    if (state == nullptr ||
        state->connection == nullptr) {
        return;
    }
    GError *error = nullptr;
    g_dbus_connection_emit_signal(
        state->connection,
        nullptr,
        kObjectPath,
        kInterfaceName,
        "HealthChanged",
        g_variant_new(
            "(bs)",
            status.healthy,
            status.detail.c_str()),
        &error);
    if (error != nullptr) {
        g_warning("Unable to emit HealthChanged: %s", error->message);
        g_error_free(error);
    }
}

bool refresh_and_signal(
    ServiceState *state,
    std::string &error)
{
    if (state == nullptr) {
        error = "Engine service state is unavailable.";
        return false;
    }

    const EngineServiceStatus before = state->last_status;
    const bool refreshed = state->core.refresh(error);
    const EngineServiceStatus after = state->core.status();

    if (after.generation != before.generation) {
        emit_state_changed(state, after.generation);
    }
    if (after.healthy != before.healthy ||
        after.detail != before.detail) {
        emit_health_changed(state, after);
    }
    state->last_status = after;
    return refreshed;
}

void reload_and_signal(ServiceState *state)
{
    if (state == nullptr) {
        return;
    }

    const EngineServiceStatus before = state->last_status;
    std::string error;
    (void)state->core.reload(error);
    const EngineServiceStatus after = state->core.status();

    if (after.generation != before.generation) {
        emit_state_changed(state, after.generation);
    }
    if (after.healthy != before.healthy ||
        after.detail != before.detail) {
        emit_health_changed(state, after);
    }
    state->last_status = after;
}

gboolean debounce_reload(gpointer user_data)
{
    auto *state =
        static_cast<ServiceState *>(user_data);
    if (state != nullptr) {
        state->debounce_id = 0U;
        reload_and_signal(state);
    }
    return G_SOURCE_REMOVE;
}

void state_directory_changed(
    GFileMonitor *,
    GFile *file,
    GFile *,
    GFileMonitorEvent,
    gpointer user_data)
{
    auto *state =
        static_cast<ServiceState *>(user_data);
    if (state == nullptr || file == nullptr) {
        return;
    }

    gchar *basename = g_file_get_basename(file);
    const std::string database_name =
        std::filesystem::path(
            state->core.database_path())
            .filename()
            .string();
    const bool relevant =
        basename != nullptr &&
        std::string_view(basename).rfind(
            database_name, 0U) == 0U;
    g_free(basename);

    if (!relevant) {
        return;
    }

    if (state->debounce_id != 0U) {
        g_source_remove(state->debounce_id);
    }
    state->debounce_id =
        g_timeout_add(
            150U,
            debounce_reload,
            state);
}

gboolean fallback_poll(gpointer user_data)
{
    reload_and_signal(
        static_cast<ServiceState *>(user_data));
    return G_SOURCE_CONTINUE;
}

void start_state_watch(ServiceState *state)
{
    if (state == nullptr) {
        return;
    }

    const std::filesystem::path parent =
        std::filesystem::path(
            state->core.database_path())
            .parent_path();

    if (!parent.empty()) {
        GFile *directory =
            g_file_new_for_path(parent.c_str());
        GError *error = nullptr;
        state->monitor =
            g_file_monitor_directory(
                directory,
                G_FILE_MONITOR_NONE,
                nullptr,
                &error);
        g_object_unref(directory);

        if (state->monitor != nullptr) {
            g_signal_connect(
                state->monitor,
                "changed",
                G_CALLBACK(state_directory_changed),
                state);
        } else if (error != nullptr) {
            g_debug(
                "Package-state directory monitor unavailable: %s",
                error->message);
            g_error_free(error);
        }
    }

    state->fallback_poll_id =
        g_timeout_add_seconds(
            30U,
            fallback_poll,
            state);
}

bool parse_action(
    const std::string_view text,
    TransactionAction &action)
{
    if (text == "install") {
        action = TransactionAction::install;
        return true;
    }
    if (text == "upgrade") {
        action = TransactionAction::upgrade;
        return true;
    }
    if (text == "remove") {
        action = TransactionAction::remove;
        return true;
    }
    return false;
}

void return_engine_error(
    GDBusMethodInvocation *invocation,
    const char *name,
    const std::string &detail)
{
    g_dbus_method_invocation_return_dbus_error(
        invocation,
        name,
        detail.c_str());
}

void handle_method_call(
    GDBusConnection *,
    const gchar *,
    const gchar *,
    const gchar *,
    const gchar *method_name,
    GVariant *parameters,
    GDBusMethodInvocation *invocation,
    gpointer user_data)
{
    auto *state =
        static_cast<ServiceState *>(user_data);
    if (state == nullptr) {
        return_engine_error(
            invocation,
            "net.ssmith.infiltrator.software.Engine.Error.Internal",
            "Engine service state is unavailable.");
        return;
    }

    const std::string method =
        method_name == nullptr
            ? std::string{}
            : std::string(method_name);

    if (method == "GetStatus") {
        g_dbus_method_invocation_return_value(
            invocation,
            g_variant_new(
                "(@a{sv})",
                status_variant(state->core.status())));
        return;
    }

    if (method == "ListInstalled" ||
        method == "ListUpdates") {
        const EngineServiceStatus status = state->core.status();
        if (status.generation == 0U) {
            return_engine_error(
                invocation,
                "net.ssmith.infiltrator.software.Engine.Error.NoState",
                status.detail);
            return;
        }

        const std::vector<PackageRecord> packages =
            method == "ListInstalled"
                ? state->core.installed()
                : state->core.updates();
        g_dbus_method_invocation_return_value(
            invocation,
            g_variant_new(
                "(@aa{sv})",
                packages_variant(packages)));
        return;
    }

    if (method == "ReloadState") {
        reload_and_signal(state);
        g_dbus_method_invocation_return_value(
            invocation,
            g_variant_new(
                "(@a{sv})",
                status_variant(state->core.status())));
        return;
    }

    if (method == "RefreshState") {
        std::string refresh_error;
        if (!refresh_and_signal(state, refresh_error)) {
            return_engine_error(
                invocation,
                "net.ssmith.infiltrator.software.Engine.Error.RefreshFailed",
                refresh_error);
            return;
        }
        g_dbus_method_invocation_return_value(
            invocation,
            g_variant_new(
                "(@a{sv})",
                status_variant(state->core.status())));
        return;
    }

    if (method == "PlanTransaction") {
        const gchar *action_text = nullptr;
        GVariant *package_ids_variant = nullptr;
        g_variant_get(
            parameters,
            "(&s@as)",
            &action_text,
            &package_ids_variant);

        TransactionRequest request;
        if (!parse_action(
                action_text == nullptr
                    ? std::string_view{}
                    : std::string_view(action_text),
                request.action)) {
            g_variant_unref(package_ids_variant);
            return_engine_error(
                invocation,
                "net.ssmith.infiltrator.software.Engine.Error.InvalidRequest",
                "Unknown transaction action.");
            return;
        }

        GVariantIter iterator;
        g_variant_iter_init(&iterator, package_ids_variant);
        const gchar *package_id = nullptr;
        while (g_variant_iter_next(
                   &iterator, "&s", &package_id)) {
            if (request.package_ids.size() >=
                kMaximumPlanPackages) {
                g_variant_unref(package_ids_variant);
                return_engine_error(
                    invocation,
                    "net.ssmith.infiltrator.software.Engine.Error.InvalidRequest",
                    "Transaction contains too many package identities.");
                return;
            }

            const std::string identity =
                package_id == nullptr
                    ? std::string{}
                    : std::string(package_id);
            if (identity.empty() ||
                identity.size() > 512U) {
                g_variant_unref(package_ids_variant);
                return_engine_error(
                    invocation,
                    "net.ssmith.infiltrator.software.Engine.Error.InvalidRequest",
                    "Transaction contains an invalid package identity.");
                return;
            }
            request.package_ids.emplace_back(identity);
        }
        g_variant_unref(package_ids_variant);

        std::string error;
        const auto plan =
            state->core.plan(request, {}, error);
        if (!plan.has_value()) {
            return_engine_error(
                invocation,
                "net.ssmith.infiltrator.software.Engine.Error.PlanFailed",
                error);
            return;
        }

        g_dbus_method_invocation_return_value(
            invocation,
            g_variant_new(
                "(@a{sv})",
                plan_variant(*plan)));
        return;
    }

    return_engine_error(
        invocation,
        "net.ssmith.infiltrator.software.Engine.Error.UnknownMethod",
        "Unknown engine method.");
}

const GDBusInterfaceVTable kInterfaceVTable{
    handle_method_call,
    nullptr,
    nullptr,
    {nullptr}
};

void on_bus_acquired(
    GDBusConnection *connection,
    const gchar *,
    gpointer user_data)
{
    auto *state =
        static_cast<ServiceState *>(user_data);
    if (state == nullptr ||
        state->node_info == nullptr) {
        return;
    }

    state->connection = connection;
    GError *error = nullptr;
    state->registration_id =
        g_dbus_connection_register_object(
            connection,
            kObjectPath,
            state->node_info->interfaces[0],
            &kInterfaceVTable,
            state,
            nullptr,
            &error);
    if (state->registration_id == 0U) {
        g_warning(
            "Unable to register package engine object: %s",
            error == nullptr
                ? "unknown error"
                : error->message);
        if (error != nullptr) {
            g_error_free(error);
        }
        if (state->loop != nullptr) {
            g_main_loop_quit(state->loop);
        }
        return;
    }

    start_state_watch(state);
}

void on_name_lost(
    GDBusConnection *,
    const gchar *,
    gpointer user_data)
{
    auto *state =
        static_cast<ServiceState *>(user_data);
    if (state != nullptr &&
        state->loop != nullptr) {
        g_main_loop_quit(state->loop);
    }
}

} // namespace

int main()
{
    ServiceState state(
        infiltrator::software::default_package_state_path());

    std::string ignored_error;
    (void)state.core.reload(ignored_error);
    state.last_status = state.core.status();

    GError *error = nullptr;
    state.node_info =
        g_dbus_node_info_new_for_xml(
            kIntrospectionXml,
            &error);
    if (state.node_info == nullptr) {
        g_printerr(
            "Unable to parse engine D-Bus interface: %s\n",
            error == nullptr
                ? "unknown error"
                : error->message);
        if (error != nullptr) {
            g_error_free(error);
        }
        return 1;
    }

    state.loop =
        g_main_loop_new(nullptr, FALSE);

    const guint owner_id =
        g_bus_own_name(
            G_BUS_TYPE_SESSION,
            kBusName,
            G_BUS_NAME_OWNER_FLAGS_NONE,
            on_bus_acquired,
            nullptr,
            on_name_lost,
            &state,
            nullptr);

    g_main_loop_run(state.loop);

    if (state.debounce_id != 0U) {
        g_source_remove(state.debounce_id);
    }
    if (state.fallback_poll_id != 0U) {
        g_source_remove(state.fallback_poll_id);
    }
    if (state.monitor != nullptr) {
        g_object_unref(state.monitor);
    }
    if (state.connection != nullptr &&
        state.registration_id != 0U) {
        g_dbus_connection_unregister_object(
            state.connection,
            state.registration_id);
    }

    g_bus_unown_name(owner_id);
    g_main_loop_unref(state.loop);
    g_dbus_node_info_unref(state.node_info);
    return 0;
}
