// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_DEBIAN_REPOSITORY_HPP
#define INFILTRATOR_SOFTWARE_DEBIAN_REPOSITORY_HPP

#include "engine/debian_package_index.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace infiltrator::software {

struct DebianRepositorySource {
    std::string id;
    std::string uri;
    std::string suite;
    std::vector<std::string> components;
    std::vector<std::string> keyrings;
    std::vector<std::string> inline_keys;
    std::vector<std::string> allowed_fingerprints;
    std::vector<std::string> architectures;
    std::vector<std::string> architecture_additions;
    std::vector<std::string> architecture_removals;
    bool verify_signatures{true};
    bool check_valid_until{true};
    bool check_date{true};
    std::uint64_t valid_until_min_seconds{0};
    std::uint64_t valid_until_max_seconds{0};
    std::uint64_t date_max_future_seconds{10};
};

struct DebianReleaseEntry {
    std::string path;
    std::string sha256;
    std::uint64_t size_bytes{0};
};

struct DebianReleaseMetadata {
    std::string origin;
    std::string label;
    std::string version;
    std::string suite;
    std::string codename;
    std::string date;
    std::string valid_until;
    bool not_automatic{false};
    bool but_automatic_upgrades{false};
    std::vector<std::string> architectures;
    std::vector<std::string> components;
    std::vector<DebianReleaseEntry> sha256_entries;

    static DebianReleaseMetadata parse(
        std::string_view content,
        std::string &error);

    [[nodiscard]] const DebianReleaseEntry *find(
        std::string_view path) const noexcept;
};

struct DebianRepositorySnapshot {
    std::string source_id;
    std::string suite;
    std::vector<DebianPackageVersion> packages;
    std::vector<std::string> verified_indexes;
};

class DebianRepositoryRefresh final {
public:
    static DebianRepositorySnapshot refresh(
        const DebianRepositorySource &source,
        std::string_view architecture,
        const std::string &cache_directory,
        std::string &error);
};

} // namespace infiltrator::software

#endif
