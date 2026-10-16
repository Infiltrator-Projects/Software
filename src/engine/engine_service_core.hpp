// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SOFTWARE_ENGINE_SERVICE_CORE_HPP
#define SOFTWARE_ENGINE_SERVICE_CORE_HPP

#include "core/model.hpp"
#include "engine/debian_candidate.hpp"
#include "engine/kernel_inventory.hpp"
#include "engine/package_state_store.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace infiltrator::software {

struct EngineServiceStatus {
    std::uint64_t generation{0};
    std::int64_t published_at_unix{0};
    std::string source_fingerprint;
    std::size_t installed_count{0};
    std::size_t available_count{0};
    std::size_t update_count{0};
    bool healthy{false};
    std::string detail;
};

class EngineServiceCore final {
public:
    explicit EngineServiceCore(
        std::string state_database_path,
        DebianCandidatePolicy policy = {});

    bool reload(std::string &error);
    bool refresh_installed(std::string &error);
    bool refresh(std::string &error);

    /*
     * Repository refresh executes in a worker-owned core in the D-Bus
     * service. The live service core remains the presentation/planning
     * authority, so the completion boundary explicitly transfers the outcome
     * instead of letting a failed worker disappear with its local state.
     * A retained generation remains readable after failure, but it is not
     * healthy enough to authorize a new plan until a full refresh succeeds.
     */
    void record_refresh_failure(std::string_view error);
    void record_refresh_success() noexcept;

    [[nodiscard]] EngineServiceStatus status() const;
    [[nodiscard]] std::vector<PackageRecord> installed() const;
    [[nodiscard]] std::vector<PackageRecord> updates() const;
    [[nodiscard]] std::vector<KernelRecord> kernels(
        std::string_view selected_kernel_type) const;
    [[nodiscard]] const std::string &database_path() const noexcept;

    std::optional<TransactionPlan> plan(
        const TransactionRequest &request,
        std::string_view target_architecture,
        std::string &error) const;

private:
    PackageStateStore store_;
    DebianCandidatePolicy policy_;
    std::optional<PackageStateSnapshot> snapshot_;
    std::vector<PackageRecord> updates_;
    bool healthy_{false};
    std::string detail_;
    std::string refresh_error_;
};

[[nodiscard]] std::string default_package_state_path();
[[nodiscard]] std::string native_debian_architecture();

} // namespace infiltrator::software

#endif
