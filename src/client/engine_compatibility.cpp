// SPDX-License-Identifier: GPL-3.0-or-later
#include "client/engine_compatibility.hpp"

#include <array>
#include <cstdint>
#include <limits>

namespace infiltrator::software {
namespace {

bool parse_release_version(
    const std::string_view text,
    std::array<std::uint32_t, 3> &parts) noexcept
{
    parts = {0U, 0U, 0U};
    std::size_t position = 0U;

    for (std::size_t component = 0U;
         component < parts.size();
         ++component) {
        if (position >= text.size() ||
            text[position] < '0' ||
            text[position] > '9') {
            return false;
        }

        std::uint64_t value = 0U;
        while (position < text.size() &&
               text[position] >= '0' &&
               text[position] <= '9') {
            value =
                value * 10U +
                static_cast<std::uint64_t>(
                    text[position] - '0');
            if (value >
                std::numeric_limits<std::uint32_t>::max()) {
                return false;
            }
            ++position;
        }
        parts[component] =
            static_cast<std::uint32_t>(value);

        if (component + 1U < parts.size()) {
            if (position >= text.size() ||
                text[position] != '.') {
                return false;
            }
            ++position;
        }
    }

    return position == text.size();
}

} // namespace

EngineVersionRelation compare_engine_version(
    const std::string_view engine_version,
    const std::string_view client_version) noexcept
{
    std::array<std::uint32_t, 3> engine{};
    std::array<std::uint32_t, 3> client{};
    if (!parse_release_version(engine_version, engine) ||
        !parse_release_version(client_version, client)) {
        return EngineVersionRelation::invalid;
    }

    if (engine < client) {
        return EngineVersionRelation::older;
    }
    if (engine > client) {
        return EngineVersionRelation::newer;
    }
    return EngineVersionRelation::same;
}

bool engine_version_is_compatible_with_client(
    const std::string_view engine_version,
    const std::string_view client_version) noexcept
{
    const EngineVersionRelation relation =
        compare_engine_version(
            engine_version,
            client_version);
    return relation == EngineVersionRelation::same ||
           relation == EngineVersionRelation::newer;
}

} // namespace infiltrator::software
