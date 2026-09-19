// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_CATALOGUE_SOURCE_HPP
#define INFILTRATOR_SOFTWARE_CATALOGUE_SOURCE_HPP

#include "core/model.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace infiltrator::software {

struct CatalogueSnapshot {
    std::vector<PackageRecord> records;
    bool from_cache{false};
    std::string source;
};

class CatalogueSource {
public:
    virtual ~CatalogueSource() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    virtual CatalogueSnapshot refresh(std::string &error) = 0;
};

} // namespace infiltrator::software

#endif
