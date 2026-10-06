// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_MIRROR_HEALTH_HPP
#define INFILTRATOR_SOFTWARE_MIRROR_HEALTH_HPP

#include <curl/curl.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace infiltrator::software::sources {
namespace detail {

inline std::string one_line(std::string value)
{
    for (char &ch : value) {
        if (ch == '\n' || ch == '\r' || ch == '\t') {
            ch = ' ';
        }
    }
    return value;
}

inline std::optional<curl_off_t> url_file_time(
    const std::string &url,
    const std::string &user_agent)
{
    CURL *curl = curl_easy_init();
    if (curl == nullptr) {
        return std::nullopt;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_FILETIME, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 6L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 12L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent.c_str());

    const CURLcode code = curl_easy_perform(curl);
    long response = 0L;
    curl_off_t file_time = -1;
    (void)curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response);
    (void)curl_easy_getinfo(curl, CURLINFO_FILETIME_T, &file_time);
    curl_easy_cleanup(curl);

    if (code != CURLE_OK || response < 200L || response >= 400L ||
        file_time < 0) {
        return std::nullopt;
    }
    return file_time;
}

} // namespace detail

/*
 * Repository/source health belongs to the source boundary, not the GTK
 * controller. The application asks for a health summary and renders it; it
 * does not parse host source files or perform mirror-network probes itself.
 */
inline std::string mint_mirror_status(const std::string_view user_agent)
{
    const std::filesystem::path sources{
        "/etc/apt/sources.list.d/official-package-repositories.list"};
    std::ifstream input(sources);
    if (!input) {
        return {};
    }

    std::string mirror;
    std::string line;
    while (std::getline(input, line)) {
        const std::string clean = detail::one_line(line);
        if (clean.rfind("deb ", 0U) != 0U ||
            clean.find("main upstream import") == std::string::npos) {
            continue;
        }
        std::istringstream words(clean);
        std::string deb;
        words >> deb >> mirror;
        break;
    }
    while (!mirror.empty() && mirror.back() == '/') {
        mirror.pop_back();
    }
    if (mirror.empty()) {
        return {};
    }

    if (mirror == "http://packages.linuxmint.com" ||
        mirror == "https://packages.linuxmint.com") {
        return "The default Linux Mint repository is in use. A local mirror may be faster; use Mint mirrors… to choose one.";
    }

    const std::string agent(user_agent);
    const auto reference = detail::url_file_time(
        "https://packages.linuxmint.com/db/version", agent);
    const auto selected = detail::url_file_time(mirror + "/db/version", agent);

    if (reference.has_value() && !selected.has_value()) {
        return mirror +
            " is unreachable. Use Mint mirrors… to choose another mirror.";
    }
    if (reference.has_value() && selected.has_value()) {
        static constexpr curl_off_t day = 24 * 60 * 60;
        if (*reference - *selected > 2 * day) {
            const curl_off_t days = (*reference - *selected) / day;
            return mirror + " is about " +
                std::to_string(static_cast<long long>(days)) +
                " days behind the Linux Mint reference repository. Use Mint mirrors… to switch.";
        }
        return "Linux Mint mirror is reachable and current.";
    }
    return {};
}

} // namespace infiltrator::software::sources

#endif
