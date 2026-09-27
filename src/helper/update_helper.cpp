// SPDX-License-Identifier: GPL-3.0-or-later
#include "apt_plan_guard.hpp"

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

bool safe_package_spec(const std::string_view value)
{
    if (value.empty() || value.size() > 512U) {
        return false;
    }

    const std::size_t equals = value.find('=');
    const std::string_view package =
        equals == std::string_view::npos ? value : value.substr(0U, equals);
    const std::string_view version =
        equals == std::string_view::npos
            ? std::string_view{}
            : value.substr(equals + 1U);

    if (package.empty() ||
        std::isalnum(static_cast<unsigned char>(package.front())) == 0) {
        return false;
    }

    for (const unsigned char ch : package) {
        if (std::isalnum(ch) != 0 ||
            ch == '+' || ch == '-' || ch == '.' || ch == ':') {
            continue;
        }
        return false;
    }

    if (equals != std::string_view::npos) {
        if (version.empty()) {
            return false;
        }
        for (const unsigned char ch : version) {
            if (std::isalnum(ch) != 0 ||
                ch == '+' || ch == '-' || ch == '.' ||
                ch == ':' || ch == '~') {
                continue;
            }
            return false;
        }
    }

    return true;
}

enum class InstalledQueryState {
    installed,
    absent,
    error
};

InstalledQueryState installed_version(
    const std::string &spec,
    std::string &version)
{
    version.clear();
    const std::size_t equals = spec.find('=');
    const std::string package =
        equals == std::string::npos ? spec : spec.substr(0U, equals);

    const char *dpkg_query =
        access("/usr/bin/dpkg-query", X_OK) == 0
            ? "/usr/bin/dpkg-query"
            : (access("/bin/dpkg-query", X_OK) == 0
                   ? "/bin/dpkg-query"
                   : nullptr);
    if (dpkg_query == nullptr) {
        return InstalledQueryState::error;
    }

    int pipe_fd[2]{};
    if (pipe(pipe_fd) != 0) {
        return InstalledQueryState::error;
    }

    const pid_t child = fork();
    if (child < 0) {
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return InstalledQueryState::error;
    }

    if (child == 0) {
        close(pipe_fd[0]);
        if (dup2(pipe_fd[1], STDOUT_FILENO) < 0) {
            _exit(127);
        }
        const int null_fd = open("/dev/null", O_WRONLY);
        if (null_fd >= 0) {
            (void)dup2(null_fd, STDERR_FILENO);
            close(null_fd);
        }
        close(pipe_fd[1]);
        execl(
            dpkg_query,
            "dpkg-query",
            "-W",
            "--showformat=${db:Status-Abbrev}\t${Version}",
            package.c_str(),
            static_cast<char *>(nullptr));
        _exit(127);
    }

    close(pipe_fd[1]);
    std::string output;
    char buffer[1024]{};
    bool read_failed = false;
    for (;;) {
        const ssize_t count =
            read(pipe_fd[0], buffer, sizeof(buffer));
        if (count > 0) {
            output.append(buffer, static_cast<std::size_t>(count));
            continue;
        }
        if (count == 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        read_failed = true;
        break;
    }
    close(pipe_fd[0]);

    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) {
            continue;
        }
        return InstalledQueryState::error;
    }

    if (read_failed || !WIFEXITED(status)) {
        return InstalledQueryState::error;
    }
    if (WEXITSTATUS(status) == 1) {
        return InstalledQueryState::absent;
    }
    if (WEXITSTATUS(status) != 0) {
        return InstalledQueryState::error;
    }

    const std::size_t tab = output.find('\t');
    if (tab == std::string::npos || tab < 2U) {
        return InstalledQueryState::error;
    }
    if (output[0] != 'i' || output[1] != 'i') {
        return InstalledQueryState::absent;
    }

    version = output.substr(tab + 1U);
    while (!version.empty() &&
           (version.back() == '\n' || version.back() == '\r')) {
        version.pop_back();
    }
    return version.empty()
        ? InstalledQueryState::error
        : InstalledQueryState::installed;
}

bool installed_package(const std::string &spec)
{
    std::string version;
    return installed_version(spec, version) ==
           InstalledQueryState::installed;
}

bool approved_spec_already_satisfied(
    const std::string &approved,
    bool &satisfied,
    std::string &error)
{
    satisfied = false;
    error.clear();

    const bool removal =
        approved.rfind("remove:", 0U) == 0U;
    const std::string spec =
        removal ? approved.substr(7U) : approved;
    const std::size_t equals = spec.find('=');
    if (equals == std::string::npos) {
        error = "Approved package specification has no exact version.";
        return false;
    }

    const std::string target_version = spec.substr(equals + 1U);
    std::string current_version;
    const InstalledQueryState state =
        installed_version(spec, current_version);
    if (state == InstalledQueryState::error) {
        error =
            "Unable to verify the current installed state for " +
            spec.substr(0U, equals) + ".";
        return false;
    }

    if (removal) {
        satisfied = state == InstalledQueryState::absent;
    } else {
        satisfied =
            state == InstalledQueryState::installed &&
            current_version == target_version;
    }
    return true;
}

const char *apt_get_path()
{
    if (access("/usr/bin/apt-get", X_OK) == 0) {
        return "/usr/bin/apt-get";
    }
    if (access("/bin/apt-get", X_OK) == 0) {
        return "/bin/apt-get";
    }
    return nullptr;
}

std::vector<char *> apt_argv(std::vector<std::string> &arguments);

bool safe_progress_token(const std::string_view token)
{
    if (token.empty() || token.size() > 96U) {
        return false;
    }
    for (const unsigned char ch : token) {
        if (std::isalnum(ch) != 0 || ch == '-' || ch == '_') {
            continue;
        }
        return false;
    }
    return true;
}

std::string privileged_progress_path()
{
    const char *uid = std::getenv("PKEXEC_UID");
    if (uid == nullptr || *uid == '\0') {
        return {};
    }
    for (const unsigned char ch : std::string_view(uid)) {
        if (std::isdigit(ch) == 0) {
            return {};
        }
    }
    return std::string("/run/infiltrator-software/update-") +
           uid + ".state";
}

std::string progress_detail(std::string_view detail)
{
    std::string clean(detail);
    for (char &ch : clean) {
        if (ch == '\n' || ch == '\r' || ch == '\t') {
            ch = ' ';
        }
    }
    if (clean.size() > 360U) {
        clean.resize(357U);
        clean += "...";
    }
    return clean;
}

void write_progress(
    const std::string &path,
    const std::string_view token,
    const std::string_view phase,
    const std::string_view detail)
{
    if (path.empty() || token.empty() || phase.empty()) {
        return;
    }

    if (mkdir("/run/infiltrator-software", 0755) != 0 &&
        errno != EEXIST) {
        return;
    }

    const int fd = open(
        path.c_str(),
        O_WRONLY | O_CREAT | O_TRUNC |
        O_CLOEXEC | O_NOFOLLOW,
        0644);
    if (fd < 0) {
        return;
    }

    const std::string clean = progress_detail(detail);
    (void)dprintf(
        fd,
        "%.*s\t%.*s\t%s\n",
        static_cast<int>(token.size()), token.data(),
        static_cast<int>(phase.size()), phase.data(),
        clean.c_str());
    close(fd);
}

void update_progress_from_apt_line(
    const std::string &path,
    const std::string_view token,
    const std::string_view line)
{
    if (line.rfind("Need to get ", 0U) == 0U ||
        line.rfind("Get:", 0U) == 0U ||
        line.rfind("Ign:", 0U) == 0U) {
        write_progress(path, token, "download", line);
    } else if (line.rfind("Fetched ", 0U) == 0U) {
        write_progress(
            path, token, "install",
            "Download complete. Installing approved packages.");
    } else if (line.rfind("Preparing to unpack ", 0U) == 0U ||
               line.rfind("Unpacking ", 0U) == 0U) {
        write_progress(path, token, "install", line);
    } else if (line.rfind("Setting up ", 0U) == 0U) {
        write_progress(path, token, "configure", line);
    } else if (line.rfind("Processing triggers for ", 0U) == 0U) {
        write_progress(path, token, "finalize", line);
    }
}

bool apt_dpkg_locked()
{
    static constexpr const char *paths[] = {
        "/var/lib/dpkg/lock-frontend",
        "/var/lib/dpkg/lock",
        "/var/lib/apt/lists/lock",
        "/var/cache/apt/archives/lock"
    };

    for (const char *path : paths) {
        const int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            continue;
        }

        struct flock lock {};
        lock.l_type = F_WRLCK;
        lock.l_whence = SEEK_SET;
        const int status = fcntl(fd, F_GETLK, &lock);
        close(fd);

        if (status == 0 && lock.l_type != F_UNLCK) {
            return true;
        }
    }
    return false;
}

bool wait_for_package_manager(
    const std::string &progress_path,
    const std::string_view token)
{
    for (unsigned second = 0U; second < 300U; ++second) {
        if (!apt_dpkg_locked()) {
            return true;
        }

        write_progress(
            progress_path,
            token,
            "wait-lock",
            "Another package-management transaction is active. Waiting for it to finish.");
        sleep(1U);
    }
    return !apt_dpkg_locked();
}

int run_apt_with_progress(
    std::vector<std::string> arguments,
    const std::string &progress_path,
    const std::string_view token)
{
    const char *path = apt_get_path();
    if (path == nullptr) {
        std::fprintf(stderr, "apt-get is not available.\n");
        return 127;
    }

    int pipe_fd[2]{};
    if (pipe(pipe_fd) != 0) {
        std::perror("Unable to create apt-get progress pipe");
        return 127;
    }

    const pid_t child = fork();
    if (child < 0) {
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        std::perror("Unable to start apt-get");
        return 127;
    }

    if (child == 0) {
        close(pipe_fd[0]);
        if (dup2(pipe_fd[1], STDOUT_FILENO) < 0) {
            _exit(127);
        }
        close(pipe_fd[1]);
        std::vector<char *> argv = apt_argv(arguments);
        (void)setenv("DEBIAN_FRONTEND", "noninteractive", 1);
        (void)setenv("LC_ALL", "C", 1);
        execv(path, argv.data());
        std::perror("Unable to execute apt-get");
        _exit(127);
    }

    close(pipe_fd[1]);
    std::string pending;
    char buffer[4096]{};
    bool read_failed = false;
    for (;;) {
        const ssize_t count =
            read(pipe_fd[0], buffer, sizeof(buffer));
        if (count > 0) {
            std::size_t forwarded = 0U;
            while (forwarded < static_cast<std::size_t>(count)) {
                const ssize_t written = write(
                    STDOUT_FILENO,
                    buffer + forwarded,
                    static_cast<std::size_t>(count) - forwarded);
                if (written > 0) {
                    forwarded += static_cast<std::size_t>(written);
                    continue;
                }
                if (written < 0 && errno == EINTR) {
                    continue;
                }
                break;
            }
            pending.append(
                buffer,
                static_cast<std::size_t>(count));
            for (;;) {
                const std::size_t newline =
                    pending.find('\n');
                if (newline == std::string::npos) {
                    break;
                }
                const std::string line =
                    pending.substr(0U, newline);
                pending.erase(0U, newline + 1U);
                update_progress_from_apt_line(
                    progress_path, token, line);
            }
            continue;
        }
        if (count == 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        read_failed = true;
        break;
    }
    close(pipe_fd[0]);
    if (!pending.empty()) {
        update_progress_from_apt_line(
            progress_path, token, pending);
    }

    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) {
            continue;
        }
        std::perror("Unable to collect apt-get status");
        return 127;
    }
    if (read_failed) {
        std::perror("Unable to read apt-get progress output");
        return 127;
    }
    if (!WIFEXITED(status)) {
        return 1;
    }
    return WEXITSTATUS(status);
}

std::vector<char *> apt_argv(std::vector<std::string> &arguments)
{
    std::vector<char *> argv;
    argv.reserve(arguments.size() + 2U);
    argv.push_back(const_cast<char *>("apt-get"));
    for (std::string &argument : arguments) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);
    return argv;
}

int run_apt(std::vector<std::string> arguments)
{
    const char *path = apt_get_path();
    if (path == nullptr) {
        std::fprintf(stderr, "apt-get is not available.\n");
        return 127;
    }

    const pid_t child = fork();
    if (child < 0) {
        std::perror("Unable to start apt-get");
        return 127;
    }
    if (child == 0) {
        std::vector<char *> argv = apt_argv(arguments);
        (void)setenv("DEBIAN_FRONTEND", "noninteractive", 1);
        (void)setenv("LC_ALL", "C", 1);
        execv(path, argv.data());
        std::perror("Unable to execute apt-get");
        _exit(127);
    }

    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) {
            continue;
        }
        std::perror("Unable to collect apt-get status");
        return 127;
    }
    if (!WIFEXITED(status)) {
        return 1;
    }
    return WEXITSTATUS(status);
}


int run_apt_capture(
    std::vector<std::string> arguments,
    std::string &output)
{
    output.clear();
    const char *path = apt_get_path();
    if (path == nullptr) {
        std::fprintf(stderr, "apt-get is not available.\n");
        return 127;
    }
    int pipe_fd[2]{};
    if (pipe(pipe_fd) != 0) {
        std::perror("Unable to create apt-get capture pipe");
        return 127;
    }
    const pid_t child = fork();
    if (child < 0) {
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        std::perror("Unable to start apt-get simulation");
        return 127;
    }
    if (child == 0) {
        close(pipe_fd[0]);
        if (dup2(pipe_fd[1], STDOUT_FILENO) < 0) _exit(127);
        close(pipe_fd[1]);
        std::vector<char *> argv = apt_argv(arguments);
        (void)setenv("DEBIAN_FRONTEND", "noninteractive", 1);
        (void)setenv("LC_ALL", "C", 1);
        execv(path, argv.data());
        _exit(127);
    }
    close(pipe_fd[1]);
    bool read_failed = false;
    char buffer[4096]{};
    for (;;) {
        const ssize_t count = read(pipe_fd[0], buffer, sizeof(buffer));
        if (count > 0) {
            output.append(buffer, static_cast<std::size_t>(count));
            continue;
        }
        if (count == 0) break;
        if (errno == EINTR) continue;
        read_failed = true;
        break;
    }
    close(pipe_fd[0]);
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) continue;
        std::perror("Unable to collect apt-get simulation status");
        return 127;
    }
    if (read_failed) {
        std::perror("Unable to read apt-get simulation output");
        return 127;
    }
    if (!WIFEXITED(status)) return 1;
    return WEXITSTATUS(status);
}

int execute_dpkg_configure()
{
    const char *path =
        access("/usr/bin/dpkg", X_OK) == 0
            ? "/usr/bin/dpkg"
            : (access("/bin/dpkg", X_OK) == 0
                   ? "/bin/dpkg"
                   : nullptr);
    if (path == nullptr) {
        std::fprintf(stderr, "dpkg is not available.\n");
        return 127;
    }

    char *const argv[] = {
        const_cast<char *>("dpkg"),
        const_cast<char *>("--configure"),
        const_cast<char *>("-a"),
        nullptr
    };
    (void)setenv("DEBIAN_FRONTEND", "noninteractive", 1);
    (void)setenv("LC_ALL", "C", 1);
    execv(path, argv);
    std::perror("Unable to execute dpkg --configure -a");
    return 127;
}

int execute_apt(std::vector<std::string> arguments)
{
    const char *path = apt_get_path();
    if (path == nullptr) {
        std::fprintf(stderr, "apt-get is not available.\n");
        return 127;
    }

    std::vector<char *> argv = apt_argv(arguments);
    (void)setenv("DEBIAN_FRONTEND", "noninteractive", 1);
    (void)setenv("LC_ALL", "C", 1);
    execv(path, argv.data());

    std::perror("Unable to execute apt-get");
    return 127;
}

} // namespace

int main(int argc, char **argv)
{
    if (geteuid() != 0) {
        std::fprintf(
            stderr,
            "infiltrator-software-update-helper must run as root.\n");
        return 1;
    }

    if (argc == 2 &&
        std::strcmp(argv[1], "repair-configure") == 0) {
        /*
         * Narrow repair action: finish configuration for packages already
         * unpacked on this machine. It does not select, install, upgrade or
         * remove repository packages.
         */
        return execute_dpkg_configure();
    }

    const bool legacy_upgrade =
        argc >= 3 && std::strcmp(argv[1], "apply") == 0;
    const bool resolved_plan =
        argc >= 3 && std::strcmp(argv[1], "apply-plan") == 0;

    if (legacy_upgrade || resolved_plan) {
        int specification_start = 2;
        std::string progress_token;
        if (argc > specification_start &&
            std::string_view(argv[specification_start]).rfind(
                "--progress-token=", 0U) == 0U) {
            progress_token =
                std::string(argv[specification_start]).substr(17U);
            if (!safe_progress_token(progress_token)) {
                std::fprintf(
                    stderr,
                    "Invalid update progress token.\n");
                return 64;
            }
            ++specification_start;
        }
        if (argc <= specification_start) {
            std::fprintf(
                stderr,
                "At least one exact package specification is required.\n");
            return 64;
        }
        const std::string progress_path =
            progress_token.empty()
                ? std::string{}
                : privileged_progress_path();
        std::vector<std::string> arguments{
            "-y",
            "--no-install-recommends",
            "--no-install-suggests",
            "install"};
        arguments.reserve(static_cast<std::size_t>(argc) + 5U);

        bool has_removal = false;
        std::vector<std::string> approved_specs;
        approved_specs.reserve(
            static_cast<std::size_t>(
                argc - specification_start));

        for (int index = specification_start; index < argc; ++index) {
            std::string approved(argv[index]);
            const bool removal =
                approved.rfind("remove:", 0U) == 0U;
            const std::string spec =
                removal ? approved.substr(7U) : approved;

            if (!safe_package_spec(spec) ||
                spec.find('=') == std::string::npos) {
                std::fprintf(
                    stderr,
                    "Invalid exact package specification: %s\n",
                    argv[index]);
                return 64;
            }
            if (legacy_upgrade && removal) {
                std::fprintf(
                    stderr,
                    "The legacy upgrade entry point cannot remove packages.\n");
                return 64;
            }

            /*
             * The legacy entry point remains upgrade-only for compatibility
             * with older Software clients. apply-plan accepts the exact
             * install/upgrade/removal set already resolved by the native
             * planner.
             */
            if (legacy_upgrade && !installed_package(spec)) {
                std::fprintf(
                    stderr,
                    "Refusing to install a requested package that is not "
                    "already installed through the legacy upgrade path: %s\n",
                    argv[index]);
                return 65;
            }

            approved_specs.push_back(approved);
            if (removal) {
                has_removal = true;
                const std::size_t equals = spec.find('=');
                arguments.push_back(spec.substr(0U, equals) + "-");
            } else {
                arguments.push_back(spec);
            }
        }

        if (!has_removal) {
            arguments.insert(arguments.begin() + 1, "--no-remove");
        }

        /*
         * A package may already have reached the approved final version before
         * this helper runs (for example, the user retried a stale Updates row
         * after an earlier transaction completed). Verify that state directly
         * with dpkg before doing any network work. If every approved final
         * state is already true, the transaction is a successful no-op.
         */
        std::vector<std::string> pending_specs;
        pending_specs.reserve(approved_specs.size());
        for (const std::string &approved : approved_specs) {
            bool satisfied = false;
            std::string state_error;
            if (!approved_spec_already_satisfied(
                    approved, satisfied, state_error)) {
                std::fprintf(stderr, "%s\n", state_error.c_str());
                return 67;
            }
            if (!satisfied) {
                pending_specs.push_back(approved);
            }
        }

        if (pending_specs.empty()) {
            write_progress(
                progress_path,
                progress_token,
                "complete",
                "Approved package versions are already installed.");
            std::puts("INFILTRATOR_NO_CHANGES_REQUIRED");
            return 0;
        }

        /*
         * Refresh root-owned metadata only after the user has reviewed the
         * complete plan and PolicyKit has authorized this exact execution.
         * Every planned package mutation is explicit. Installs/upgrades are
         * pinned to the reviewed version; approved removals are named
         * explicitly. Recommends/Suggests are disabled so APT cannot silently
         * broaden the native plan.
         */
        if (!wait_for_package_manager(
                progress_path,
                progress_token)) {
            std::fprintf(
                stderr,
                "Another package-management transaction remained active for five minutes.\n");
            return 68;
        }

        write_progress(
            progress_path,
            progress_token,
            "refresh",
            "Refreshing trusted repository metadata.");
        const int refresh_status = run_apt({"update"});
        if (refresh_status != 0) {
            std::fprintf(
                stderr,
                "Unable to refresh system package metadata before install.\n");
            return refresh_status;
        }

        /*
         * Repository metadata may have changed after the user approved the
         * plan. Re-simulate the exact privileged command against the refreshed
         * metadata and require a one-for-one match with the approved package
         * identities and versions. Any added dependency, missing change,
         * architecture drift or removal aborts before system mutation.
         */
        write_progress(
            progress_path,
            progress_token,
            "validate",
            "Re-validating the reviewed package versions against refreshed metadata.");
        std::vector<std::string> simulation_arguments = arguments;
        simulation_arguments.insert(simulation_arguments.begin(), "-s");

        std::string simulation_output;
        const int simulation_status =
            run_apt_capture(simulation_arguments, simulation_output);
        if (simulation_status != 0) {
            std::fprintf(
                stderr,
                "Unable to validate the approved transaction against refreshed package metadata.\n");
            return simulation_status;
        }

        std::string validation_error;
        if (!infiltrator::software::helper::validate_apt_simulation(
                pending_specs, simulation_output, validation_error)) {
            std::fprintf(stderr, "%s\n", validation_error.c_str());
            return 66;
        }

        if (progress_token.empty()) {
            return execute_apt(std::move(arguments));
        }

        write_progress(
            progress_path,
            progress_token,
            "download",
            "Downloading approved package payloads.");
        const int install_status =
            run_apt_with_progress(
                std::move(arguments),
                progress_path,
                progress_token);
        if (install_status == 0) {
            write_progress(
                progress_path,
                progress_token,
                "finalize",
                "Package application finished; returning to Software for final verification.");
        } else {
            write_progress(
                progress_path,
                progress_token,
                "error",
                "The privileged package transaction failed.");
        }
        return install_status;
    }

    std::fprintf(
        stderr,
        "Usage: infiltrator-software-update-helper "
        "apply-plan [remove:]PACKAGE=VERSION... | repair-configure\n");
    return 64;
}
