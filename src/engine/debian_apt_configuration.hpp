// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SOFTWARE_DEBIAN_APT_CONFIGURATION_HPP
#define SOFTWARE_DEBIAN_APT_CONFIGURATION_HPP

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace infiltrator::software {

class DebianAptConfiguration final {
public:
    static std::optional<DebianAptConfiguration> read(std::string &error);

    static std::optional<DebianAptConfiguration> parse(
        std::string_view content,
        std::string_view origin,
        std::string &error);

    [[nodiscard]] std::optional<std::string> value(
        std::string_view key) const;

    [[nodiscard]] std::filesystem::path path(
        std::string_view key,
        const std::filesystem::path &fallback) const;

private:
    std::map<std::string, std::string> values_;
};

} // namespace infiltrator::software

#endif
