// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_CATALOGUE_SNAPSHOT_STORE_HPP
#define INFILTRATOR_SOFTWARE_CATALOGUE_SNAPSHOT_STORE_HPP

#include "catalogue/catalogue_source.hpp"

#include <string>

namespace infiltrator::software {

class CatalogueSnapshotStore final {
public:
    explicit CatalogueSnapshotStore(std::string path = default_path());

    [[nodiscard]] const std::string &path() const noexcept;

    bool load(
        CatalogueSnapshot &snapshot,
        std::string &error) const;

    bool save(
        const CatalogueSnapshot &snapshot,
        std::string &error) const;

    [[nodiscard]] static std::string default_path();

private:
    std::string path_;
};

} // namespace infiltrator::software

#endif
