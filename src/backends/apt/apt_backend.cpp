// SPDX-License-Identifier: GPL-3.0-or-later
#include "backends/apt/apt_backend.hpp"

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
#include <utility>
#include <vector>

namespace infiltrator::software {
namespace {

bool run_dpkg_query(std::string &output, std::string &error)
{
    int pipe_fd[2]{};
    if (pipe(pipe_fd) != 0) {
        error = "Unable to create dpkg-query pipe.";
        return false;
    }

    const std::string format =
        "--showformat=${binary:Package}\t${Version}\t${Installed-Size}"
        "\t${db:Status-Abbrev}\n";

    const pid_t child = fork();
    if (child < 0) {
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        error = "Unable to start dpkg-query.";
        return false;
    }

    if (child == 0) {
        close(pipe_fd[0]);
        if (dup2(pipe_fd[1], STDOUT_FILENO) < 0 ||
            dup2(pipe_fd[1], STDERR_FILENO) < 0) {
            _exit(127);
        }
        close(pipe_fd[1]);
        execlp("dpkg-query", "dpkg-query", "--show", format.c_str(),
               static_cast<char *>(nullptr));
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
        error = "Unable to read dpkg-query output.";
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
        error = "Unable to collect dpkg-query status.";
        return false;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        error = output.empty() ? "dpkg-query failed." : output;
        return false;
    }

    return true;
}

std::vector<std::string_view> split_tabs(const std::string_view line)
{
    std::vector<std::string_view> fields;
    std::size_t start = 0;
    for (;;) {
        const std::size_t tab = line.find('\t', start);
        if (tab == std::string_view::npos) {
            fields.emplace_back(line.substr(start));
            return fields;
        }
        fields.emplace_back(line.substr(start, tab - start));
        start = tab + 1;
    }
}

std::uint64_t kib_to_bytes(const std::string_view text)
{
    std::uint64_t kib = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), kib);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        return 0;
    }
    if (kib > std::numeric_limits<std::uint64_t>::max() / 1024U) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return kib * 1024U;
}

void classify(PackageRecord &package)
{
    const std::string_view id = package.id;

    if (id.rfind("linux-image", 0) == 0 ||
        id.rfind("linux-modules", 0) == 0) {
        package.kind = PackageKind::kernel;
        package.system_critical = true;
        return;
    }

    if (id == "apt" || id == "dpkg" || id == "systemd" ||
        id.rfind("libc6", 0) == 0) {
        package.kind = PackageKind::system;
        package.system_critical = true;
        return;
    }

    if (id.rfind("lib", 0) == 0) {
        package.kind = PackageKind::library;
    }
}

} // namespace

std::string_view AptBackend::name() const noexcept
{
    return "APT/.deb";
}

bool AptBackend::available() const noexcept
{
    return access("/usr/bin/dpkg-query", X_OK) == 0 ||
           access("/bin/dpkg-query", X_OK) == 0;
}

BackendCapabilities AptBackend::capabilities() const noexcept
{
    BackendCapabilities result;
    result.installed_inventory = available();
    return result;
}

std::vector<PackageRecord> AptBackend::list_installed(std::string &error)
{
    error.clear();
    std::vector<PackageRecord> packages;

    std::string output;
    if (!run_dpkg_query(output, error)) {
        return packages;
    }

    std::size_t start = 0;
    while (start < output.size()) {
        const std::size_t end = output.find('\n', start);
        const std::string_view line{
            output.data() + start,
            (end == std::string::npos ? output.size() : end) - start};

        const auto fields = split_tabs(line);
        if (fields.size() == 4U && fields[3].rfind("ii", 0) == 0) {
            PackageRecord package;
            package.id.assign(fields[0]);
            package.name = package.id;
            package.installed_version.assign(fields[1]);
            package.available_version = package.installed_version;
            package.installed_size_bytes = kib_to_bytes(fields[2]);
            package.source = "dpkg";
            package.state = InstallState::installed;
            classify(package);

            if (valid_identity(package)) {
                packages.emplace_back(std::move(package));
            }
        }

        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    std::sort(packages.begin(), packages.end(),
              [](const PackageRecord &left, const PackageRecord &right) {
                  return left.name < right.name;
              });
    return packages;
}

std::vector<PackageRecord> AptBackend::search(
    const std::string_view, std::string &error)
{
    error = "APT catalogue search is not enabled in the 0.1.0 read-only milestone.";
    return {};
}

std::vector<PackageRecord> AptBackend::list_updates(std::string &error)
{
    error = "APT update inventory is not enabled in the 0.1.0 read-only milestone.";
    return {};
}

std::optional<TransactionPlan> AptBackend::plan(
    const TransactionRequest &, std::string &error)
{
    error = "APT transaction planning is not enabled in the 0.1.0 read-only milestone.";
    return std::nullopt;
}

} // namespace infiltrator::software
