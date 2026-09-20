// SPDX-License-Identifier: GPL-3.0-or-later
#include "backends/apt/apt_backend.hpp"
#include "engine/debian_installed_state.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace infiltrator::software {
namespace {

bool run_command(
    const std::vector<std::string> &arguments,
    std::string &output,
    std::string &error)
{
    output.clear();
    error.clear();
    if (arguments.empty()) {
        error = "No command was supplied.";
        return false;
    }

    std::vector<char *> argv;
    argv.reserve(arguments.size() + 1U);
    for (const std::string &argument : arguments) {
        argv.push_back(const_cast<char *>(argument.c_str()));
    }
    argv.push_back(nullptr);

    int pipe_fd[2]{};
    if (pipe(pipe_fd) != 0) {
        error = "Unable to create package-manager pipe.";
        return false;
    }

    const pid_t child = fork();
    if (child < 0) {
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        error = "Unable to start package-manager command.";
        return false;
    }

    if (child == 0) {
        close(pipe_fd[0]);
        if (dup2(pipe_fd[1], STDOUT_FILENO) < 0 ||
            dup2(pipe_fd[1], STDERR_FILENO) < 0) {
            _exit(127);
        }
        close(pipe_fd[1]);
        execvp(argv[0], argv.data());
        _exit(127);
    }

    close(pipe_fd[1]);
    std::array<char, 8192> buffer{};
    for (;;) {
        const ssize_t count = read(pipe_fd[0], buffer.data(), buffer.size());
        if (count > 0) {
            output.append(buffer.data(), static_cast<std::size_t>(count));
            continue;
        }
        if (count == 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        close(pipe_fd[0]);
        error = "Unable to read package-manager output.";
        int ignored_status = 0;
        (void)waitpid(child, &ignored_status, 0);
        return false;
    }
    close(pipe_fd[0]);

    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) {
            continue;
        }
        error = "Unable to collect package-manager status.";
        return false;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        error = output.empty() ? "Package-manager command failed." : output;
        return false;
    }

    return true;
}

bool apt_get_available() noexcept
{
    return access("/usr/bin/apt-get", X_OK) == 0 ||
           access("/bin/apt-get", X_OK) == 0;
}

std::string package_key(std::string value)
{
    const std::size_t colon = value.find(':');
    if (colon != std::string::npos) {
        value.erase(colon);
    }
    return value;
}

void classify(PackageRecord &package)
{
    const std::string id = package_key(package.id);

    if (id.rfind("linux-image", 0U) == 0U ||
        id.rfind("linux-modules", 0U) == 0U ||
        id.rfind("linux-headers", 0U) == 0U) {
        package.kind = PackageKind::kernel;
        package.system_critical = true;
        return;
    }

    if (id == "apt" || id == "dpkg" || id == "systemd" ||
        id.rfind("libc6", 0U) == 0U ||
        id.rfind("linux-base", 0U) == 0U) {
        package.kind = PackageKind::system;
        package.system_critical = true;
        return;
    }

    if (id.rfind("lib", 0U) == 0U) {
        package.kind = PackageKind::library;
        return;
    }

    package.kind = PackageKind::application;
}

std::string candidate_version(const std::string_view line)
{
    const std::size_t open = line.find('(');
    if (open == std::string_view::npos || open + 1U >= line.size()) {
        return {};
    }
    const std::size_t end = line.find_first_of(" )", open + 1U);
    if (end == std::string_view::npos || end <= open + 1U) {
        return {};
    }
    return std::string(line.substr(open + 1U, end - open - 1U));
}

std::string package_token(
    const std::string_view line, const std::string_view prefix)
{
    if (line.rfind(prefix, 0U) != 0U) {
        return {};
    }
    const std::size_t start = prefix.size();
    const std::size_t end = line.find(' ', start);
    if (end == std::string_view::npos || end <= start) {
        return {};
    }
    return std::string(line.substr(start, end - start));
}

std::unordered_map<std::string, PackageRecord> installed_map(
    std::vector<PackageRecord> installed)
{
    std::unordered_map<std::string, PackageRecord> result;
    result.reserve(installed.size());
    for (PackageRecord &record : installed) {
        result.emplace(package_key(record.package_name), std::move(record));
    }
    return result;
}

} // namespace

std::string_view AptBackend::name() const noexcept
{
    return "APT/.deb";
}

bool AptBackend::available() const noexcept
{
    return DebianInstalledState::available();
}

BackendCapabilities AptBackend::capabilities() const noexcept
{
    BackendCapabilities result;
    result.installed_inventory = DebianInstalledState::available();
    result.update_inventory =
        result.installed_inventory && apt_get_available();
    result.transaction_planning = result.update_inventory;
    return result;
}

std::vector<PackageRecord> AptBackend::list_installed(std::string &error)
{
    std::vector<PackageRecord> packages =
        DebianInstalledState::read(error);
    for (PackageRecord &package : packages) {
        classify(package);
    }
    return packages;
}

std::vector<PackageRecord> AptBackend::search(
    const std::string_view, std::string &error)
{
    error =
        "APT catalogue search is provided by the repository catalogue layer.";
    return {};
}

std::vector<PackageRecord> AptBackend::list_updates(std::string &error)
{
    error.clear();

    std::string installed_error;
    auto installed =
        installed_map(list_installed(installed_error));
    if (!installed_error.empty()) {
        error = installed_error;
        return {};
    }

    std::string output;
    if (!run_command(
            {"env", "LC_ALL=C", "apt-get", "-s",
             "-o", "Debug::NoLocking=1", "dist-upgrade"},
            output, error)) {
        return {};
    }

    std::vector<PackageRecord> updates;
    std::unordered_set<std::string> seen;

    std::size_t start = 0U;
    while (start < output.size()) {
        const std::size_t end = output.find('\n', start);
        const std::string_view line{
            output.data() + start,
            (end == std::string::npos ? output.size() : end) - start};

        const std::string token = package_token(line, "Inst ");
        if (!token.empty()) {
            const std::string key = package_key(token);
            const auto found = installed.find(key);
            const std::string candidate = candidate_version(line);
            if (found != installed.end() && !candidate.empty() &&
                candidate != found->second.installed_version &&
                seen.insert(key).second) {
                PackageRecord package = found->second;
                package.id = token;
                package.package_name = token;
                package.name = token;
                package.available_version = candidate;
                package.state = InstallState::upgradable;
                package.source = "APT";
                classify(package);
                updates.emplace_back(std::move(package));
            }
        }

        if (end == std::string::npos) {
            break;
        }
        start = end + 1U;
    }

    std::sort(
        updates.begin(), updates.end(),
        [](const PackageRecord &left, const PackageRecord &right) {
            if (left.system_critical != right.system_critical) {
                return left.system_critical > right.system_critical;
            }
            if (left.kind != right.kind) {
                return static_cast<int>(left.kind) <
                       static_cast<int>(right.kind);
            }
            return left.name < right.name;
        });
    return updates;
}

std::optional<TransactionPlan> AptBackend::plan(
    const TransactionRequest &request, std::string &error)
{
    error.clear();
    if (request.action != TransactionAction::upgrade) {
        error = "APT planning currently supports upgrades only.";
        return std::nullopt;
    }
    if (request.package_ids.empty()) {
        error = "No packages were selected for upgrade.";
        return std::nullopt;
    }

    std::string installed_error;
    auto installed =
        installed_map(list_installed(installed_error));
    if (!installed_error.empty()) {
        error = installed_error;
        return std::nullopt;
    }

    std::vector<std::string> arguments{
        "env", "LC_ALL=C", "apt-get", "-s",
        "-o", "Debug::NoLocking=1",
        "--no-remove", "install"};
    arguments.insert(
        arguments.end(),
        request.package_ids.begin(), request.package_ids.end());

    std::string output;
    if (!run_command(arguments, output, error)) {
        return std::nullopt;
    }

    TransactionPlan plan;
    std::unordered_set<std::string> seen;

    std::size_t start = 0U;
    while (start < output.size()) {
        const std::size_t end = output.find('\n', start);
        const std::string_view line{
            output.data() + start,
            (end == std::string::npos ? output.size() : end) - start};

        std::string token = package_token(line, "Inst ");
        if (!token.empty()) {
            const std::string key = package_key(token);
            if (seen.insert("I:" + key).second) {
                TransactionItem item;
                item.package_id = token;
                const auto found = installed.find(key);
                if (found != installed.end()) {
                    item.action = TransactionAction::upgrade;
                    item.from_version = found->second.installed_version;
                } else {
                    item.action = TransactionAction::install;
                }
                item.to_version = candidate_version(line);

                PackageRecord classification;
                classification.id = token;
                classification.name = token;
                classify(classification);
                item.system_critical = classification.system_critical;
                plan.touches_system =
                    plan.touches_system || item.system_critical;
                plan.items.emplace_back(std::move(item));
            }
        } else {
            token = package_token(line, "Remv ");
            if (!token.empty()) {
                const std::string key = package_key(token);
                if (seen.insert("R:" + key).second) {
                    TransactionItem item;
                    item.package_id = token;
                    item.action = TransactionAction::remove;
                    const auto found = installed.find(key);
                    if (found != installed.end()) {
                        item.from_version = found->second.installed_version;
                    }
                    item.system_critical = true;
                    plan.touches_system = true;
                    plan.items.emplace_back(std::move(item));
                }
            }
        }

        if (end == std::string::npos) {
            break;
        }
        start = end + 1U;
    }

    if (plan.items.empty()) {
        error =
            "APT produced no transaction changes for the selected updates.";
        return std::nullopt;
    }

    return plan;
}

} // namespace infiltrator::software
