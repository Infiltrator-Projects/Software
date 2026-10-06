// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_KERNEL_INVENTORY_HPP
#define INFILTRATOR_SOFTWARE_KERNEL_INVENTORY_HPP

#include "core/model.hpp"
#include "engine/debian_package_index.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace infiltrator::software {

struct KernelReleaseWindow {
    std::string codename;
    int release_year{0};
    int release_month{0};
    int support_end_year{0};
    int support_end_month{0};
};

class KernelInventory final {
public:
    static std::vector<KernelRecord> build(
        const std::vector<PackageRecord> &installed,
        const std::vector<DebianPackageVersion> &available,
        std::string_view selected_kernel_type,
        std::string_view active_kernel_release,
        int current_year,
        int current_month,
        const std::vector<KernelReleaseWindow> &release_windows);

    static std::vector<KernelRecord> build_host(
        const std::vector<PackageRecord> &installed,
        const std::vector<DebianPackageVersion> &available,
        std::string_view selected_kernel_type);

    static std::vector<KernelReleaseWindow> read_release_windows();
    static std::string active_kernel_release();

    static bool supported_kernel_type(std::string_view kernel_type) noexcept;
    static std::string default_kernel_type() noexcept;
};

} // namespace infiltrator::software

#endif
