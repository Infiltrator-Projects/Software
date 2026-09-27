// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/kernel_inventory.hpp"

#include "engine/debian_version.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/utsname.h>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace infiltrator::software {
namespace {

constexpr std::array<std::string_view, 8> kKernelTypes{
    "-generic", "-lowlatency", "-aws", "-azure",
    "-gcp", "-kvm", "-oem", "-oracle"
};

std::string package_base(std::string value)
{
    const std::size_t colon = value.find(':');
    if (colon != std::string::npos) value.erase(colon);
    return value;
}

bool starts_with(
    const std::string_view value,
    const std::string_view prefix) noexcept
{
    return value.size() >= prefix.size() &&
           value.substr(0U, prefix.size()) == prefix;
}

bool ends_with(
    const std::string_view value,
    const std::string_view suffix) noexcept
{
    return value.size() >= suffix.size() &&
           value.substr(value.size() - suffix.size()) == suffix;
}

struct ParsedImage {
    std::string version;
    std::string type;
    bool unsigned_image{false};
};

std::optional<ParsedImage> parse_image_name(
    const std::string_view raw_name)
{
    constexpr std::string_view prefix = "linux-image-";
    if (!starts_with(raw_name, prefix)) return std::nullopt;

    std::string_view tail = raw_name.substr(prefix.size());
    bool unsigned_image = false;
    constexpr std::string_view unsigned_prefix = "unsigned-";
    if (starts_with(tail, unsigned_prefix)) {
        unsigned_image = true;
        tail.remove_prefix(unsigned_prefix.size());
    }

    for (const std::string_view type : kKernelTypes) {
        if (!ends_with(tail, type) ||
            tail.size() <= type.size()) {
            continue;
        }
        const std::string_view version =
            tail.substr(0U, tail.size() - type.size());
        if (version.empty() ||
            !std::isdigit(
                static_cast<unsigned char>(version.front()))) {
            continue;
        }
        return ParsedImage{
            std::string(version),
            std::string(type),
            unsigned_image};
    }
    return std::nullopt;
}

std::vector<int> numeric_version(const std::string_view value)
{
    std::vector<int> result;
    int current = 0;
    bool in_number = false;
    for (const char raw : value) {
        const unsigned char ch =
            static_cast<unsigned char>(raw);
        if (std::isdigit(ch) != 0) {
            in_number = true;
            current = current * 10 + static_cast<int>(ch - '0');
        } else if (in_number) {
            result.push_back(current);
            current = 0;
            in_number = false;
        }
    }
    if (in_number) result.push_back(current);
    return result;
}

int compare_numeric_versions(
    const std::string_view left,
    const std::string_view right)
{
    const std::vector<int> a = numeric_version(left);
    const std::vector<int> b = numeric_version(right);
    const std::size_t count = std::max(a.size(), b.size());
    for (std::size_t index = 0U; index < count; ++index) {
        const int av = index < a.size() ? a[index] : 0;
        const int bv = index < b.size() ? b[index] : 0;
        if (av < bv) return -1;
        if (av > bv) return 1;
    }
    return 0;
}

std::string series_of(
    const std::string_view version,
    const std::size_t components)
{
    const std::vector<int> parts = numeric_version(version);
    if (parts.empty()) return {};
    std::ostringstream output;
    const std::size_t count =
        std::min(components, parts.size());
    for (std::size_t index = 0U; index < count; ++index) {
        if (index != 0U) output << '.';
        output << parts[index];
    }
    return output.str();
}

std::vector<std::string> split_csv(const std::string &line)
{
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    for (std::size_t index = 0U; index < line.size(); ++index) {
        const char ch = line[index];
        if (ch == '"') {
            if (quoted && index + 1U < line.size() &&
                line[index + 1U] == '"') {
                field.push_back('"');
                ++index;
            } else {
                quoted = !quoted;
            }
        } else if (ch == ',' && !quoted) {
            fields.push_back(field);
            field.clear();
        } else {
            field.push_back(ch);
        }
    }
    fields.push_back(field);
    return fields;
}

bool parse_year_month(
    const std::string_view text,
    int &year,
    int &month)
{
    year = 0;
    month = 0;
    if (text.size() < 7U ||
        text[4] != '-' ||
        !std::isdigit(static_cast<unsigned char>(text[0])) ||
        !std::isdigit(static_cast<unsigned char>(text[1])) ||
        !std::isdigit(static_cast<unsigned char>(text[2])) ||
        !std::isdigit(static_cast<unsigned char>(text[3])) ||
        !std::isdigit(static_cast<unsigned char>(text[5])) ||
        !std::isdigit(static_cast<unsigned char>(text[6]))) {
        return false;
    }
    year =
        (text[0] - '0') * 1000 +
        (text[1] - '0') * 100 +
        (text[2] - '0') * 10 +
        (text[3] - '0');
    month = (text[5] - '0') * 10 + (text[6] - '0');
    return year > 1900 && month >= 1 && month <= 12;
}

int months_between(
    const int from_year,
    const int from_month,
    const int to_year,
    const int to_month)
{
    return (to_year - from_year) * 12 +
           (to_month - from_month);
}

std::pair<int, int> add_months(
    const int year,
    const int month,
    const int duration)
{
    const int zero_based =
        year * 12 + (month - 1) + duration;
    return {zero_based / 12, zero_based % 12 + 1};
}

std::string month_name(const int month)
{
    static constexpr std::array<const char *, 12> names{
        "January", "February", "March", "April",
        "May", "June", "July", "August",
        "September", "October", "November", "December"
    };
    if (month < 1 || month > 12) return {};
    return names[static_cast<std::size_t>(month - 1)];
}

int parse_support_months(const std::string_view value)
{
    if (value.size() < 2U) return 0;
    int number = 0;
    for (std::size_t index = 0U;
         index + 1U < value.size();
         ++index) {
        const unsigned char ch =
            static_cast<unsigned char>(value[index]);
        if (std::isdigit(ch) == 0) return 0;
        number = number * 10 + static_cast<int>(ch - '0');
    }
    if (value.back() == 'y') return number * 12;
    if (value.back() == 'm') return number;
    return 0;
}

std::string release_codename(const std::string_view archive)
{
    const std::size_t dash = archive.find('-');
    return std::string(
        archive.substr(0U, dash));
}

const KernelReleaseWindow *find_window(
    const std::vector<KernelReleaseWindow> &windows,
    const std::string_view codename)
{
    const auto found = std::find_if(
        windows.begin(), windows.end(),
        [&](const KernelReleaseWindow &window) {
            return window.codename == codename;
        });
    return found == windows.end() ? nullptr : &*found;
}

const DebianPackageVersion *best_available(
    const std::vector<DebianPackageVersion> &available,
    const std::string_view package)
{
    const DebianPackageVersion *best = nullptr;
    for (const DebianPackageVersion &candidate : available) {
        if (candidate.package != package) continue;
        if (best == nullptr ||
            compare_debian_versions(
                candidate.version,
                best->version) > 0) {
            best = &candidate;
        }
    }
    return best;
}

bool installed_package(
    const std::unordered_set<std::string> &installed,
    const std::string &name)
{
    return installed.find(name) != installed.end();
}

std::vector<std::string> kernel_package_names(
    const std::string &version,
    const std::string &type,
    const bool removal)
{
    std::vector<std::string> names{
        "linux-headers-" + version,
        "linux-headers-" + version + type,
        "linux-image-" + version + type,
        "linux-modules-" + version + type,
        "linux-modules-extra-" + version + type,
        "linux-image-extra-" + version + type
    };
    if (removal) {
        names.push_back(
            "linux-image-unsigned-" + version + type);
        names.push_back(
            "linux-tools-" + version);
        names.push_back(
            "linux-tools-" + version + type);
    }
    return names;
}

struct Seed {
    KernelRecord record;
    const DebianPackageVersion *candidate{};
    int support_months{0};
    std::string release;
    bool signed_present{false};
    bool unsigned_present{false};
};

std::string seed_key(
    const std::string_view version,
    const std::string_view type)
{
    return std::string(version) + "|" + std::string(type);
}

} // namespace

bool KernelInventory::supported_kernel_type(
    const std::string_view kernel_type) noexcept
{
    return std::find(
               kKernelTypes.begin(),
               kKernelTypes.end(),
               kernel_type) != kKernelTypes.end();
}

std::string KernelInventory::default_kernel_type() noexcept
{
    return "-generic";
}

std::string KernelInventory::active_kernel_release()
{
    struct utsname info {};
    if (uname(&info) != 0) return {};
    return info.release;
}

std::vector<KernelReleaseWindow>
KernelInventory::read_release_windows()
{
    std::vector<KernelReleaseWindow> result;
    const std::array<std::filesystem::path, 2> paths{
        "/usr/share/distro-info/ubuntu.csv",
        "/usr/share/distro-info/debian.csv"
    };

    for (const std::filesystem::path &path : paths) {
        std::ifstream input(path);
        if (!input) continue;

        std::string line;
        bool header = true;
        while (std::getline(input, line)) {
            if (header) {
                header = false;
                continue;
            }
            const std::vector<std::string> fields =
                split_csv(line);
            if (fields.size() < 6U) continue;

            KernelReleaseWindow window;
            window.codename = fields[2];
            if (window.codename.empty() ||
                !parse_year_month(
                    fields[4],
                    window.release_year,
                    window.release_month) ||
                !parse_year_month(
                    fields[5],
                    window.support_end_year,
                    window.support_end_month)) {
                continue;
            }
            result.emplace_back(std::move(window));
        }
    }
    return result;
}

std::vector<KernelRecord> KernelInventory::build_host(
    const std::vector<PackageRecord> &installed,
    const std::vector<DebianPackageVersion> &available,
    const std::string_view selected_kernel_type)
{
    const auto now =
        std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::now());
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    return build(
        installed,
        available,
        selected_kernel_type,
        active_kernel_release(),
        local.tm_year + 1900,
        local.tm_mon + 1,
        read_release_windows());
}

std::vector<KernelRecord> KernelInventory::build(
    const std::vector<PackageRecord> &installed,
    const std::vector<DebianPackageVersion> &available,
    const std::string_view selected_kernel_type,
    const std::string_view active_kernel_release_value,
    const int current_year,
    const int current_month,
    const std::vector<KernelReleaseWindow> &release_windows)
{
    const std::string selected_type =
        supported_kernel_type(selected_kernel_type)
            ? std::string(selected_kernel_type)
            : default_kernel_type();

    std::unordered_set<std::string> installed_names;
    installed_names.reserve(installed.size());
    for (const PackageRecord &package : installed) {
        installed_names.insert(
            package_base(
                package.package_name.empty()
                    ? package.id
                    : package.package_name));
    }

    std::map<std::string, Seed> seeds;
    auto add_image = [&](const std::string &name,
                         const bool is_installed,
                         const DebianPackageVersion *candidate) {
        const auto parsed = parse_image_name(name);
        if (!parsed.has_value()) return;

        Seed &seed =
            seeds[seed_key(parsed->version, parsed->type)];
        if (seed.record.version.empty()) {
            seed.record.version = parsed->version;
            seed.record.kernel_type = parsed->type;
            seed.record.series =
                series_of(parsed->version, 2U);
        }

        if (is_installed) seed.record.installed = true;
        if (parsed->unsigned_image) {
            seed.unsigned_present = true;
        } else {
            seed.signed_present = true;
        }

        if (!parsed->unsigned_image ||
            seed.record.image_package.empty()) {
            seed.record.image_package = name;
        }

        if (candidate != nullptr &&
            (seed.candidate == nullptr ||
             compare_debian_versions(
                 candidate->version,
                 seed.candidate->version) > 0 ||
             (seed.candidate->package.find("unsigned-") !=
                  std::string::npos &&
              candidate->package.find("unsigned-") ==
                  std::string::npos))) {
            seed.candidate = candidate;
            seed.record.package_version = candidate->version;
            seed.record.origin = candidate->release_origin;
            seed.record.archive = candidate->release_archive;
        }
    };

    for (const PackageRecord &package : installed) {
        const std::string name =
            package_base(
                package.package_name.empty()
                    ? package.id
                    : package.package_name);
        add_image(name, true, best_available(available, name));
    }
    for (const DebianPackageVersion &package : available) {
        add_image(
            package.package,
            installed_package(
                installed_names, package.package),
            &package);
    }

    for (auto &[key, seed] : seeds) {
        (void)key;
        KernelRecord &record = seed.record;
        record.active =
            record.version + record.kernel_type ==
            active_kernel_release_value;
        record.installable =
            seed.candidate != nullptr &&
            record.kernel_type == selected_type &&
            !record.installed;

        if (seed.candidate != nullptr) {
            seed.release =
                release_codename(
                    seed.candidate->release_archive);
            seed.support_months =
                parse_support_months(
                    seed.candidate->supported);

            if (seed.support_months == 0 &&
                seed.candidate->release_origin == "Ubuntu" &&
                seed.candidate->release_archive.find(
                    "-proposed") == std::string::npos) {
                if (const KernelReleaseWindow *window =
                        find_window(
                            release_windows,
                            seed.release);
                    window != nullptr) {
                    seed.support_months =
                        months_between(
                            window->release_year,
                            window->release_month,
                            window->support_end_year,
                            window->support_end_month);
                }
            }

            if (seed.support_months > 0 &&
                seed.candidate->source_package.find(
                    "-hwe") != std::string::npos &&
                !seed.candidate->supported.empty() &&
                seed.candidate->supported.back() == 'y') {
                seed.support_months = -1;
            }
        }
    }

    /*
     * Ubuntu HWE packages encode an intentionally open-ended support marker.
     * Resolve it from the point-release cadence, matching Mint's lifecycle
     * model while keeping the calculation in native C++.
     */
    std::map<std::string, std::vector<Seed *>> hwe_groups;
    for (auto &[key, seed] : seeds) {
        (void)key;
        if (seed.support_months == -1 && !seed.release.empty()) {
            hwe_groups[
                seed.release + "|" + seed.record.kernel_type]
                .push_back(&seed);
        }
    }
    for (auto &[group, values] : hwe_groups) {
        (void)group;
        std::sort(
            values.begin(),
            values.end(),
            [](const Seed *left, const Seed *right) {
                return compare_numeric_versions(
                           left->record.series,
                           right->record.series) < 0;
            });
        values.erase(
            std::unique(
                values.begin(),
                values.end(),
                [](const Seed *left, const Seed *right) {
                    return left->record.series ==
                           right->record.series;
                }),
            values.end());

        if (values.empty()) continue;
        const KernelReleaseWindow *window =
            find_window(
                release_windows,
                values.front()->release);
        if (window == nullptr) continue;

        const int since_release =
            months_between(
                window->release_year,
                window->release_month,
                current_year,
                current_month);
        const int full_lifetime =
            months_between(
                window->release_year,
                window->release_month,
                window->support_end_year,
                window->support_end_month);

        std::vector<int> durations(values.size(), -1);
        for (std::size_t index = 0U;
             index < values.size();
             ++index) {
            int duration = values[index]->support_months;
            if (duration != -1) {
                durations[index] = duration;
                continue;
            }

            if (index >= 4U) {
                if (values.size() > 5U &&
                    index + 1U < values.size() &&
                    durations.size() > 3U &&
                    durations[3] > 0) {
                    duration = durations[3];
                } else if (since_release >= 28) {
                    duration = full_lifetime;
                }
            }

            if (index >= 1U && duration == -1) {
                const int maximum_expected =
                    std::max(
                        1,
                        (since_release - 3) / 6 + 1);
                if (static_cast<int>(index) >
                    maximum_expected) {
                    duration =
                        10 + maximum_expected * 6;
                } else {
                    duration =
                        10 +
                        static_cast<int>(index) * 6;
                }
            }

            if (duration == -1) {
                duration = full_lifetime;
            }
            durations[index] = duration;
            values[index]->support_months = duration;
        }
    }

    std::vector<KernelRecord> result;
    result.reserve(seeds.size());

    std::vector<Seed *> ordered;
    ordered.reserve(seeds.size());
    for (auto &[key, seed] : seeds) {
        (void)key;
        ordered.push_back(&seed);
    }
    std::sort(
        ordered.begin(),
        ordered.end(),
        [](const Seed *left, const Seed *right) {
            const int version_order =
                compare_numeric_versions(
                    left->record.version,
                    right->record.version);
            if (version_order != 0) {
                return version_order > 0;
            }
            return left->record.kernel_type <
                   right->record.kernel_type;
        });

    /*
     * Lifecycle support and supersession are distinct.  An available newer
     * kernel must never make the running kernel look unsupported.  For each
     * type/series use the running kernel as the installed reference when
     * present; otherwise use the newest installed kernel.  Older installed
     * kernels may then be labelled superseded while every in-lifecycle kernel
     * still carries its real support window.
     */
    std::map<std::string, Seed *> installed_reference;
    for (Seed *seed : ordered) {
        if (!seed->record.installed) {
            continue;
        }
        const std::string key =
            seed->record.kernel_type + "|" +
            seed->record.series;
        auto &reference = installed_reference[key];
        if (reference == nullptr ||
            seed->record.active) {
            reference = seed;
        }
    }

    for (Seed *seed : ordered) {
        KernelRecord record = seed->record;

        // Match Mint's inventory rule: always show installed kernels of any
        // type, but only offer not-yet-installed kernels for the selected type.
        if (!record.installed &&
            record.kernel_type != selected_type) {
            continue;
        }

        if (seed->support_months > 0 &&
            !seed->release.empty() &&
            seed->candidate != nullptr &&
            seed->candidate->release_origin == "Ubuntu") {
            const KernelReleaseWindow *window =
                find_window(
                    release_windows,
                    seed->release);
            if (window != nullptr) {
                const auto [end_year, end_month] =
                    add_months(
                        window->release_year,
                        window->release_month,
                        seed->support_months);
                if (current_year > end_year ||
                    (current_year == end_year &&
                     current_month > end_month)) {
                    record.end_of_life = true;
                    record.support_status = "End of Life";
                } else {
                    record.supported = true;
                    record.support_end =
                        month_name(end_month) +
                        " " +
                        std::to_string(end_year);

                    const std::string support_key =
                        record.kernel_type + "|" +
                        record.series;
                    const auto reference =
                        installed_reference.find(
                            support_key);
                    const bool older_installed =
                        record.installed &&
                        !record.active &&
                        reference != installed_reference.end() &&
                        reference->second != nullptr &&
                        compare_numeric_versions(
                            record.version,
                            reference->second->record.version) < 0;

                    if (older_installed) {
                        record.superseded = true;
                        record.support_status =
                            "Superseded";
                    } else {
                        record.support_status =
                            "Supported until " +
                            record.support_end;
                    }
                }
            }
        }

        if (record.support_status.empty()) {
            if (seed->support_months == 0) {
                record.support_status = "Unsupported";
            } else if (record.installed) {
                /*
                 * Installed local kernels can outlive repository metadata.
                 * Do not invent lifecycle dates when the signed source no
                 * longer provides enough information.
                 */
                record.support_status =
                    "Installed (support metadata unavailable)";
            } else {
                record.support_status = "Unsupported";
            }
        }

        const std::vector<std::string> install_names =
            kernel_package_names(
                record.version,
                record.kernel_type,
                false);
        for (const std::string &name : install_names) {
            if (best_available(available, name) != nullptr) {
                record.install_package_ids.push_back(name);
            }
        }

        const std::vector<std::string> remove_names =
            kernel_package_names(
                record.version,
                record.kernel_type,
                true);
        bool another_type_same_version = false;
        for (const auto &[other_key, other_seed] : seeds) {
            (void)other_key;
            if (&other_seed == seed) continue;
            if (other_seed.record.installed &&
                other_seed.record.version == record.version &&
                other_seed.record.kernel_type !=
                    record.kernel_type) {
                another_type_same_version = true;
                break;
            }
        }
        for (const std::string &name : remove_names) {
            if (!installed_package(installed_names, name)) {
                continue;
            }
            if (another_type_same_version &&
                name == "linux-headers-" + record.version) {
                continue;
            }
            record.remove_package_ids.push_back(name);
        }

        /*
         * When this is the last installed kernel of its type in a complete
         * major.minor.patch series, remove a stale meta package whose current
         * repository candidate points at that same series.  This mirrors
         * Mint's kernel-window cleanup without guessing from package names
         * alone: the candidate version must prove the association.
         */
        bool last_in_series = true;
        const std::string full_series =
            series_of(record.version, 3U);
        for (const auto &[other_key, other_seed] : seeds) {
            (void)other_key;
            if (&other_seed == seed ||
                !other_seed.record.installed ||
                other_seed.record.kernel_type !=
                    record.kernel_type) {
                continue;
            }
            if (series_of(
                    other_seed.record.version, 3U) ==
                full_series) {
                last_in_series = false;
                break;
            }
        }
        if (record.installed && last_in_series) {
            std::vector<std::string> meta_names;
            const std::string meta_prefix =
                "linux" + record.kernel_type;
            for (const std::string &installed_name :
                 installed_names) {
                if (starts_with(
                        installed_name,
                        meta_prefix)) {
                    meta_names.push_back(installed_name);
                }
            }
            if (record.kernel_type == "-generic" &&
                installed_package(
                    installed_names,
                    "linux-virtual")) {
                meta_names.emplace_back("linux-virtual");
            }

            for (const std::string &meta : meta_names) {
                const DebianPackageVersion *candidate =
                    best_available(available, meta);
                if (candidate == nullptr ||
                    series_of(
                        candidate->version, 3U) !=
                        full_series) {
                    continue;
                }

                const std::array<std::string, 3> related{
                    meta,
                    starts_with(meta, "linux-")
                        ? "linux-image-" + meta.substr(6U)
                        : std::string{},
                    starts_with(meta, "linux-")
                        ? "linux-headers-" + meta.substr(6U)
                        : std::string{}
                };
                for (const std::string &related_name :
                     related) {
                    if (!related_name.empty() &&
                        installed_package(
                            installed_names,
                            related_name) &&
                        std::find(
                            record.remove_package_ids.begin(),
                            record.remove_package_ids.end(),
                            related_name) ==
                            record.remove_package_ids.end()) {
                        record.remove_package_ids.push_back(
                            related_name);
                    }
                }
                if (meta == "linux-virtual" &&
                    installed_package(
                        installed_names,
                        "linux-headers-generic") &&
                    std::find(
                        record.remove_package_ids.begin(),
                        record.remove_package_ids.end(),
                        "linux-headers-generic") ==
                        record.remove_package_ids.end()) {
                    record.remove_package_ids.emplace_back(
                        "linux-headers-generic");
                }
            }
        }

        std::sort(
            record.install_package_ids.begin(),
            record.install_package_ids.end());
        record.install_package_ids.erase(
            std::unique(
                record.install_package_ids.begin(),
                record.install_package_ids.end()),
            record.install_package_ids.end());
        std::sort(
            record.remove_package_ids.begin(),
            record.remove_package_ids.end());
        record.remove_package_ids.erase(
            std::unique(
                record.remove_package_ids.begin(),
                record.remove_package_ids.end()),
            record.remove_package_ids.end());

        result.emplace_back(std::move(record));
    }

    /*
     * Bulk cleanup is intentionally safer than Mint's historic kernel window:
     * only older superseded kernels are preselected, the running kernel is
     * never removable, and one older installed fallback is always retained.
     */
    KernelRecord *fallback = nullptr;
    for (KernelRecord &record : result) {
        if (!record.installed || record.active) continue;
        if (compare_numeric_versions(
                record.version,
                active_kernel_release_value) >= 0) {
            continue;
        }
        if (fallback == nullptr ||
            compare_numeric_versions(
                record.version,
                fallback->version) > 0) {
            fallback = &record;
        }
    }
    for (KernelRecord &record : result) {
        record.safe_to_remove =
            record.installed &&
            !record.active &&
            record.superseded &&
            !record.remove_package_ids.empty() &&
            &record != fallback &&
            compare_numeric_versions(
                record.version,
                active_kernel_release_value) < 0;
    }

    return result;
}

} // namespace infiltrator::software
