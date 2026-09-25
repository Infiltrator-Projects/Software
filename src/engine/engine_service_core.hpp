// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_ENGINE_SERVICE_CORE_HPP
#define INFILTRATOR_SOFTWARE_ENGINE_SERVICE_CORE_HPP

#include "core/model.hpp"
#include "engine/debian_candidate.hpp"
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

    [[nodiscard]] EngineServiceStatus status() const;
    [[nodiscard]] std::vector<PackageRecord> installed() const;
    [[nodiscard]] std::vector<PackageRecord> updates() const;
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
};

[[nodiscard]] std::string default_package_state_path();
[[nodiscard]] std::string native_debian_architecture();

} // namespace infiltrator::software

#endif
