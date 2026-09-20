// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_REPOSITORY_CATALOGUE_HPP
#define INFILTRATOR_SOFTWARE_REPOSITORY_CATALOGUE_HPP

#include "catalogue/catalogue_source.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace infiltrator::software {

class RepositoryCatalogue final : public CatalogueSource {
public:
    RepositoryCatalogue();

    [[nodiscard]] std::string_view name() const noexcept override;
    CatalogueSnapshot load(std::string &error);
    CatalogueSnapshot refresh(std::string &error) override;
    void hydrate_icons(
        std::vector<PackageRecord> &records,
        std::string &error);

    static std::vector<PackageRecord> parse_document(
        std::string_view document,
        std::string_view repository_root,
        std::string &error);

private:
    static bool download(
        std::string_view url,
        std::size_t maximum_bytes,
        std::string &body,
        std::string &error);
    static std::string cache_root();
    static std::string catalogue_cache_path();
    static std::string icon_cache_path(const PackageRecord &record);
    static bool cache_icon(PackageRecord &record, std::string &error);
    static bool read_file(const std::string &path, std::string &content);
    static bool write_file(
        const std::string &path,
        std::string_view content,
        std::string &error);

    std::string endpoint_;
    std::string repository_root_;
};

} // namespace infiltrator::software

#endif
