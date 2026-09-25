// SPDX-License-Identifier: GPL-3.0-or-later
#include "catalogue/catalogue_snapshot_store.hpp"

#include <infiltratr/posix.h>

#include <json-glib/json-glib.h>
#include <glib.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

namespace infiltrator::software {
namespace {

constexpr int kSchemaVersion = 1;

std::string string_member(
    JsonObject *object,
    const char *name)
{
    if (object == nullptr ||
        !json_object_has_member(object, name)) {
        return {};
    }
    const char *value =
        json_object_get_string_member(object, name);
    return value == nullptr
        ? std::string{}
        : std::string(value);
}

std::uint64_t uint_member(
    JsonObject *object,
    const char *name)
{
    if (object == nullptr ||
        !json_object_has_member(object, name)) {
        return 0U;
    }
    const gint64 value =
        json_object_get_int_member(object, name);
    return value < 0
        ? 0U
        : static_cast<std::uint64_t>(value);
}

bool bool_member(
    JsonObject *object,
    const char *name)
{
    return object != nullptr &&
           json_object_has_member(object, name) &&
           json_object_get_boolean_member(object, name);
}

template <typename Enum>
Enum enum_member(
    JsonObject *object,
    const char *name,
    const Enum fallback,
    const int maximum)
{
    if (object == nullptr ||
        !json_object_has_member(object, name)) {
        return fallback;
    }

    const gint64 value =
        json_object_get_int_member(object, name);
    if (value < 0 || value > maximum) {
        return fallback;
    }
    return static_cast<Enum>(value);
}

void add_string(
    JsonBuilder *builder,
    const char *name,
    const std::string &value)
{
    json_builder_set_member_name(builder, name);
    json_builder_add_string_value(
        builder,
        value.c_str());
}

void add_uint(
    JsonBuilder *builder,
    const char *name,
    const std::uint64_t value)
{
    json_builder_set_member_name(builder, name);
    const std::uint64_t maximum =
        static_cast<std::uint64_t>(G_MAXINT64);
    json_builder_add_int_value(
        builder,
        static_cast<gint64>(
            value > maximum ? maximum : value));
}

bool directory_ready(const std::string &path)
{
    const std::filesystem::path file_path(path);
    if (!file_path.has_parent_path()) {
        return true;
    }

    std::error_code error;
    std::filesystem::create_directories(
        file_path.parent_path(),
        error);
    return !error;
}

bool read_file(
    const std::string &path,
    std::string &content)
{
    content.clear();
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }

    content.assign(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
    return input.good() || input.eof();
}

PackageRecord parse_record(JsonObject *object)
{
    PackageRecord record;
    record.id = string_member(object, "id");
    record.name = string_member(object, "name");
    record.package_name =
        string_member(object, "package_name");
    record.publisher =
        string_member(object, "publisher");
    record.category =
        string_member(object, "category");
    record.architecture =
        string_member(object, "architecture");
    record.installed_version =
        string_member(object, "installed_version");
    record.available_version =
        string_member(object, "available_version");
    record.summary =
        string_member(object, "summary");
    record.description =
        string_member(object, "description");
    record.icon_name =
        string_member(object, "icon_name");
    record.icon_url =
        string_member(object, "icon_url");
    record.icon_sha256 =
        string_member(object, "icon_sha256");
    record.cached_icon_path =
        string_member(object, "cached_icon_path");
    record.source =
        string_member(object, "source");
    record.source_url =
        string_member(object, "source_url");
    record.release_url =
        string_member(object, "release_url");
    record.asset =
        string_member(object, "asset");
    record.package_sha256 =
        string_member(object, "package_sha256");
    record.published_at =
        string_member(object, "published_at");
    record.channel =
        enum_member(
            object,
            "channel",
            Channel::unknown,
            static_cast<int>(Channel::unknown));
    record.kind =
        enum_member(
            object,
            "kind",
            PackageKind::unknown,
            static_cast<int>(PackageKind::unknown));
    record.state =
        enum_member(
            object,
            "state",
            InstallState::not_installed,
            static_cast<int>(InstallState::upgradable));
    record.installed_size_bytes =
        uint_member(object, "installed_size_bytes");
    record.download_size_bytes =
        uint_member(object, "download_size_bytes");
    record.system_critical =
        bool_member(object, "system_critical");
    return record;
}

void write_record(
    JsonBuilder *builder,
    const PackageRecord &record)
{
    json_builder_begin_object(builder);

    add_string(builder, "id", record.id);
    add_string(builder, "name", record.name);
    add_string(
        builder,
        "package_name",
        record.package_name);
    add_string(
        builder,
        "publisher",
        record.publisher);
    add_string(
        builder,
        "category",
        record.category);
    add_string(
        builder,
        "architecture",
        record.architecture);
    add_string(
        builder,
        "installed_version",
        record.installed_version);
    add_string(
        builder,
        "available_version",
        record.available_version);
    add_string(
        builder,
        "summary",
        record.summary);
    add_string(
        builder,
        "description",
        record.description);
    add_string(
        builder,
        "icon_name",
        record.icon_name);
    add_string(
        builder,
        "icon_url",
        record.icon_url);
    add_string(
        builder,
        "icon_sha256",
        record.icon_sha256);
    add_string(
        builder,
        "cached_icon_path",
        record.cached_icon_path);
    add_string(
        builder,
        "source",
        record.source);
    add_string(
        builder,
        "source_url",
        record.source_url);
    add_string(
        builder,
        "release_url",
        record.release_url);
    add_string(
        builder,
        "asset",
        record.asset);
    add_string(
        builder,
        "package_sha256",
        record.package_sha256);
    add_string(
        builder,
        "published_at",
        record.published_at);

    json_builder_set_member_name(
        builder,
        "channel");
    json_builder_add_int_value(
        builder,
        static_cast<gint64>(record.channel));
    json_builder_set_member_name(
        builder,
        "kind");
    json_builder_add_int_value(
        builder,
        static_cast<gint64>(record.kind));
    json_builder_set_member_name(
        builder,
        "state");
    json_builder_add_int_value(
        builder,
        static_cast<gint64>(record.state));

    add_uint(
        builder,
        "installed_size_bytes",
        record.installed_size_bytes);
    add_uint(
        builder,
        "download_size_bytes",
        record.download_size_bytes);

    json_builder_set_member_name(
        builder,
        "system_critical");
    json_builder_add_boolean_value(
        builder,
        record.system_critical);

    json_builder_end_object(builder);
}

} // namespace

CatalogueSnapshotStore::CatalogueSnapshotStore(
    std::string path)
    : path_(std::move(path))
{
}

const std::string &
CatalogueSnapshotStore::path() const noexcept
{
    return path_;
}

bool CatalogueSnapshotStore::load(
    CatalogueSnapshot &snapshot,
    std::string &error) const
{
    error.clear();
    snapshot = {};

    if (path_.empty()) {
        error =
            "Catalogue snapshot path is unavailable.";
        return false;
    }

    std::string document;
    if (!read_file(path_, document)) {
        error =
            "No saved software catalogue is available.";
        return false;
    }

    JsonParser *parser = json_parser_new();
    if (parser == nullptr) {
        error =
            "Unable to create catalogue snapshot parser.";
        return false;
    }

    GError *parse_error = nullptr;
    const gboolean loaded =
        json_parser_load_from_data(
            parser,
            document.data(),
            static_cast<gssize>(document.size()),
            &parse_error);
    if (!loaded) {
        error = parse_error != nullptr
            ? std::string(parse_error->message)
            : "Saved software catalogue is invalid JSON.";
        g_clear_error(&parse_error);
        g_object_unref(parser);
        return false;
    }

    JsonNode *root =
        json_parser_get_root(parser);
    if (root == nullptr ||
        !JSON_NODE_HOLDS_OBJECT(root)) {
        error =
            "Saved software catalogue has an invalid root.";
        g_object_unref(parser);
        return false;
    }

    JsonObject *object =
        json_node_get_object(root);
    const gint64 schema =
        json_object_has_member(object, "schema")
            ? json_object_get_int_member(
                  object,
                  "schema")
            : 0;
    if (schema != kSchemaVersion) {
        error =
            "Saved software catalogue has an unsupported schema.";
        g_object_unref(parser);
        return false;
    }

    JsonArray *records =
        json_object_get_array_member(
            object,
            "records");
    if (records == nullptr) {
        error =
            "Saved software catalogue has no records.";
        g_object_unref(parser);
        return false;
    }

    snapshot.source =
        string_member(object, "source");
    snapshot.from_cache = true;

    const guint count =
        json_array_get_length(records);
    snapshot.records.reserve(
        static_cast<std::size_t>(count));

    for (guint index = 0U;
         index < count;
         ++index) {
        JsonObject *record_object =
            json_array_get_object_element(
                records,
                index);
        PackageRecord record =
            parse_record(record_object);
        if (!valid_identity(record)) {
            error =
                "Saved software catalogue contains an invalid record.";
            snapshot = {};
            g_object_unref(parser);
            return false;
        }
        snapshot.records.emplace_back(
            std::move(record));
    }

    g_object_unref(parser);
    return true;
}

bool CatalogueSnapshotStore::save(
    const CatalogueSnapshot &snapshot,
    std::string &error) const
{
    error.clear();

    if (path_.empty() ||
        !directory_ready(path_)) {
        error =
            "Unable to prepare the saved software catalogue path.";
        return false;
    }

    JsonBuilder *builder =
        json_builder_new();
    json_builder_begin_object(builder);

    json_builder_set_member_name(
        builder,
        "schema");
    json_builder_add_int_value(
        builder,
        kSchemaVersion);
    add_string(
        builder,
        "source",
        snapshot.source);

    json_builder_set_member_name(
        builder,
        "records");
    json_builder_begin_array(builder);
    for (const PackageRecord &record :
         snapshot.records) {
        if (!valid_identity(record)) {
            g_object_unref(builder);
            error =
                "Software catalogue contains an invalid record.";
            return false;
        }
        write_record(builder, record);
    }
    json_builder_end_array(builder);
    json_builder_end_object(builder);

    JsonNode *root =
        json_builder_get_root(builder);
    JsonGenerator *generator =
        json_generator_new();
    json_generator_set_root(
        generator,
        root);

    gsize length = 0U;
    gchar *data =
        json_generator_to_data(
            generator,
            &length);
    const bool ok =
        data != nullptr &&
        infiltratr_atomic_file_write_bytes(
            path_.c_str(),
            INFILTRATR_ATOMIC_FILE_PRIVATE,
            data,
            static_cast<std::size_t>(length)) == 0;

    if (!ok) {
        error =
            "Unable to save the software catalogue atomically.";
    }

    g_free(data);
    g_object_unref(generator);
    json_node_free(root);
    g_object_unref(builder);
    return ok;
}

std::string CatalogueSnapshotStore::default_path()
{
    const char *root =
        g_get_user_cache_dir();
    if (root == nullptr ||
        *root == '\0') {
        return {};
    }

    return (
        std::filesystem::path(root) /
        "infiltrator" /
        "software" /
        "catalogue" /
        "discover.json").string();
}

} // namespace infiltrator::software
