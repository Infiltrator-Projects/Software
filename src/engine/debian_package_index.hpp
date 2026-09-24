// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_DEBIAN_PACKAGE_INDEX_HPP
#define INFILTRATOR_SOFTWARE_DEBIAN_PACKAGE_INDEX_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace infiltrator::software {

struct DebianPackageVersion {
    std::string package;
    std::string version;
    std::string architecture;
    std::string filename;
    std::string sha256;
    std::string source;
    std::string priority;
    int pin_priority{0};

    // Ephemeral repository identity used while reconciling APT policy.
    // Only pin_priority is persisted in the package-state database.
    std::string release_origin;
    std::string release_label;
    std::string release_version;
    std::string release_archive;
    std::string release_codename;
    std::string component;
    std::string site;
    std::string multi_arch;
    std::string depends;
    std::string pre_depends;
    std::string recommends;
    std::string provides;
    std::string conflicts;
    std::string breaks;
    std::string replaces;
    std::string description;
    std::uint64_t size_bytes{0};
    std::uint64_t installed_size_bytes{0};
    bool essential{false};
};

class DebianPackageIndex final {
public:
    static std::vector<DebianPackageVersion> read_file(
        const std::string &path,
        std::string_view source_id,
        std::string &error);

    static std::vector<DebianPackageVersion> parse(
        std::string_view content,
        std::string_view source_id,
        std::string &error);
};

} // namespace infiltrator::software

#endif
