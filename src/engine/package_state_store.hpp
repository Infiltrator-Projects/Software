// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_PACKAGE_STATE_STORE_HPP
#define INFILTRATOR_SOFTWARE_PACKAGE_STATE_STORE_HPP

#include "core/model.hpp"
#include "engine/debian_package_index.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace infiltrator::software {

struct PackageStateSnapshot {
    std::uint64_t generation{0};
    std::int64_t published_at_unix{0};
    std::string source_fingerprint;
    std::vector<PackageRecord> installed;
    std::vector<DebianPackageVersion> available;
};

class PackageStateStore final {
public:
    explicit PackageStateStore(std::string path);

    [[nodiscard]] const std::string &path() const noexcept;

    bool initialise(std::string &error) const;

    bool publish(
        const std::vector<PackageRecord> &installed,
        const std::vector<DebianPackageVersion> &available,
        std::string_view source_fingerprint,
        std::uint64_t &published_generation,
        std::string &error) const;

    std::optional<PackageStateSnapshot> load_current(
        std::string &error) const;

private:
    std::string path_;
};

} // namespace infiltrator::software

#endif
