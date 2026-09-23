// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_DEBIAN_RECONCILE_HPP
#define INFILTRATOR_SOFTWARE_DEBIAN_RECONCILE_HPP

#include "engine/package_state_store.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace infiltrator::software {

class DebianReconciler final {
public:
    static bool reconcile(
        PackageStateStore &store,
        std::string_view target_architecture,
        const std::string &cache_directory,
        std::uint64_t &published_generation,
        std::string &error);
};

[[nodiscard]] std::string default_repository_cache_path();

} // namespace infiltrator::software

#endif
