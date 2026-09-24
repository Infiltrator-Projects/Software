// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_SOURCE_INVENTORY_HPP
#define INFILTRATOR_SOFTWARE_SOURCE_INVENTORY_HPP

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace infiltrator::software {

enum class SourceKind {
    infiltrator,
    apt,
    flatpak
};

struct SourceRecord {
    SourceKind kind{SourceKind::apt};
    std::string name;
    std::string location;
    std::string detail;
    std::string scope;
    std::string backing_file;
    std::size_t entry_index{0U};
    bool enabled{true};
};

std::string_view source_kind_name(SourceKind kind) noexcept;

class SourceInventory {
public:
    std::vector<SourceRecord> list(std::string &error) const;

    static std::vector<SourceRecord> parse_apt_list(
        std::string_view content,
        std::string_view backing_file);

    static std::vector<SourceRecord> parse_apt_deb822(
        std::string_view content,
        std::string_view backing_file);
};

} // namespace infiltrator::software

#endif
