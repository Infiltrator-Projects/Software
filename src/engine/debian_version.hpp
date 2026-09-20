// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_DEBIAN_VERSION_HPP
#define INFILTRATOR_SOFTWARE_DEBIAN_VERSION_HPP

#include <string_view>

namespace infiltrator::software {

// Returns < 0 when left is older, 0 when equivalent, and > 0 when left is
// newer according to Debian package-version ordering.
[[nodiscard]] int compare_debian_versions(
    std::string_view left,
    std::string_view right) noexcept;

[[nodiscard]] bool debian_version_is_newer(
    std::string_view candidate,
    std::string_view installed) noexcept;

} // namespace infiltrator::software

#endif
