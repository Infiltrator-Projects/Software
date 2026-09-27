// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_ENGINE_COMPATIBILITY_HPP
#define INFILTRATOR_SOFTWARE_ENGINE_COMPATIBILITY_HPP

#include <string_view>

namespace infiltrator::software {

enum class EngineVersionRelation {
    invalid,
    older,
    same,
    newer
};

EngineVersionRelation compare_engine_version(
    std::string_view engine_version,
    std::string_view client_version) noexcept;

bool engine_version_is_compatible_with_client(
    std::string_view engine_version,
    std::string_view client_version) noexcept;

} // namespace infiltrator::software

#endif
