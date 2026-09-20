// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_SYSTEM_CATALOGUE_HPP
#define INFILTRATOR_SOFTWARE_SYSTEM_CATALOGUE_HPP

#include "catalogue/catalogue_source.hpp"

namespace infiltrator::software {

class SystemCatalogue final : public CatalogueSource {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    CatalogueSnapshot refresh(std::string &error) override;
};

} // namespace infiltrator::software

#endif
