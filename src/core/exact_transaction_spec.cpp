// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/exact_transaction_spec.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace infiltrator::software {
namespace {

char hex_digit(const unsigned value)
{
    return value < 10U
        ? static_cast<char>('0' + value)
        : static_cast<char>('A' + (value - 10U));
}

int hex_value(const char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

std::string encode_field(const std::string_view value)
{
    std::string encoded;
    encoded.reserve(value.size());
    for (const unsigned char ch : value) {
        if ((ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            encoded.push_back(static_cast<char>(ch));
            continue;
        }
        encoded.push_back('%');
        encoded.push_back(hex_digit((ch >> 4U) & 0x0fU));
        encoded.push_back(hex_digit(ch & 0x0fU));
    }
    return encoded;
}

bool decode_field(
    const std::string_view value,
    std::string &decoded)
{
    decoded.clear();
    decoded.reserve(value.size());
    for (std::size_t index = 0U; index < value.size(); ++index) {
        if (value[index] != '%') {
            const unsigned char ch =
                static_cast<unsigned char>(value[index]);
            if (ch < 0x20U || ch == 0x7fU) return false;
            decoded.push_back(value[index]);
            continue;
        }
        if (index + 2U >= value.size()) return false;
        const int high = hex_value(value[index + 1U]);
        const int low = hex_value(value[index + 2U]);
        if (high < 0 || low < 0) return false;
        const unsigned char ch =
            static_cast<unsigned char>((high << 4) | low);
        if (ch < 0x20U || ch == 0x7fU) return false;
        decoded.push_back(static_cast<char>(ch));
        index += 2U;
    }
    return true;
}

bool valid_sha256(const std::string_view value)
{
    return value.size() == 64U &&
           std::all_of(
               value.begin(),
               value.end(),
               [](const unsigned char ch) {
                   return std::isxdigit(ch) != 0;
               });
}

std::vector<std::string_view> split_fields(
    const std::string_view value)
{
    std::vector<std::string_view> fields;
    std::size_t start = 0U;
    while (start <= value.size()) {
        const std::size_t end = value.find('|', start);
        fields.push_back(value.substr(
            start,
            end == std::string_view::npos
                ? value.size() - start
                : end - start));
        if (end == std::string_view::npos) break;
        start = end + 1U;
    }
    return fields;
}

} // namespace

bool encode_exact_transaction_spec(
    const TransactionItem &item,
    std::string &spec,
    std::string &error)
{
    spec.clear();
    error.clear();

    if (item.package_id.empty()) {
        error =
            "The resolved transaction contains a package without a stable identity.";
        return false;
    }

    const bool removal =
        item.action == TransactionAction::remove;
    const std::string &version =
        removal ? item.from_version : item.to_version;
    if (version.empty()) {
        error = removal
            ? "The resolved removal contains a package without its exact installed version."
            : "The resolved transaction contains a package without an exact target version.";
        return false;
    }

    if (!removal &&
        (item.source.empty() ||
         item.filename.empty() ||
         !valid_sha256(item.sha256))) {
        error =
            "The resolved install/upgrade is missing its reviewed repository "
            "source, filename, or SHA-256 artifact identity.";
        return false;
    }

    spec =
        "x2|" +
        std::string(removal ? "R" : "I") + "|" +
        encode_field(item.package_id) + "|" +
        encode_field(version) + "|" +
        encode_field(item.architecture) + "|" +
        encode_field(item.source) + "|" +
        encode_field(item.filename) + "|" +
        encode_field(item.sha256);
    return true;
}

bool exact_transaction_specs(
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
    for (const TransactionItem &item : plan.items) {
        std::string spec;
        if (!encode_exact_transaction_spec(item, spec, error)) {
            specs.clear();
            return false;
        }
        specs.emplace_back(std::move(spec));
    }
    return true;
}

bool decode_exact_transaction_spec(
    const std::string_view spec,
    ExactTransactionSpec &decoded,
    std::string &error)
{
    decoded = {};
    error.clear();

    const std::vector<std::string_view> fields =
        split_fields(spec);
    if (fields.size() != 8U || fields[0] != "x2" ||
        (fields[1] != "I" && fields[1] != "R")) {
        error = "Unsupported exact transaction specification.";
        return false;
    }

    if (!decode_field(fields[2], decoded.package_id) ||
        !decode_field(fields[3], decoded.version) ||
        !decode_field(fields[4], decoded.architecture) ||
        !decode_field(fields[5], decoded.source) ||
        !decode_field(fields[6], decoded.filename) ||
        !decode_field(fields[7], decoded.sha256) ||
        decoded.package_id.empty() ||
        decoded.version.empty()) {
        error = "Malformed exact transaction specification.";
        return false;
    }

    decoded.action =
        fields[1] == "R"
            ? TransactionAction::remove
            : TransactionAction::install;

    if (decoded.action != TransactionAction::remove &&
        (decoded.source.empty() ||
         decoded.filename.empty() ||
         !valid_sha256(decoded.sha256))) {
        error =
            "Exact install/upgrade specification lacks a complete artifact identity.";
        return false;
    }
    if (decoded.action == TransactionAction::remove &&
        (!decoded.source.empty() ||
         !decoded.filename.empty() ||
         !decoded.sha256.empty())) {
        error =
            "Exact removal specification unexpectedly contains repository artifact data.";
        return false;
    }

    return true;
}

} // namespace infiltrator::software
