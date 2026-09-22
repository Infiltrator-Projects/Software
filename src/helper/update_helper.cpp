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

bool installed_package(const std::string &spec)
{
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
        return false;
    }

    int pipe_fd[2]{};
    if (pipe(pipe_fd) != 0) {
        return false;
    }

    const pid_t child = fork();
    if (child < 0) {
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return false;
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
            "--showformat=${db:Status-Abbrev}",
            package.c_str(),
            static_cast<char *>(nullptr));
        _exit(127);
    }

    close(pipe_fd[1]);
    char status_text[8]{};
    const ssize_t count =
        read(pipe_fd[0], status_text, sizeof(status_text) - 1U);
    close(pipe_fd[0]);

    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) {
            continue;
        }
        return false;
    }

    return count >= 2 &&
           status_text[0] == 'i' &&
           status_text[1] == 'i' &&
           WIFEXITED(status) &&
           WEXITSTATUS(status) == 0;
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

    const bool legacy_upgrade =
        argc >= 3 && std::strcmp(argv[1], "apply") == 0;
    const bool resolved_plan =
        argc >= 3 && std::strcmp(argv[1], "apply-plan") == 0;

    if (legacy_upgrade || resolved_plan) {
        std::vector<std::string> arguments{
            "-y",
            "--no-remove",
            "--no-install-recommends",
            "--no-install-suggests",
            "install"};
        arguments.reserve(static_cast<std::size_t>(argc) + 5U);

        std::vector<std::string> approved_specs;
        approved_specs.reserve(static_cast<std::size_t>(argc - 2));

        for (int index = 2; index < argc; ++index) {
            const std::string spec(argv[index]);
            if (!safe_package_spec(spec) ||
                spec.find('=') == std::string::npos) {
                std::fprintf(
                    stderr,
                    "Invalid exact package specification: %s\n",
                    argv[index]);
                return 64;
            }

            /*
             * The legacy entry point remains upgrade-only for compatibility
             * with older Software clients. apply-plan is different: the GUI
             * has already resolved the complete install/upgrade dependency
             * graph, so new packages are expected and every package arrives
             * here with an exact approved version.
             */
            if (legacy_upgrade && !installed_package(spec)) {
                std::fprintf(
                    stderr,
                    "Refusing to install a requested package that is not "
                    "already installed through the legacy upgrade path: %s\n",
                    argv[index]);
                return 65;
            }
            approved_specs.push_back(spec);
            arguments.push_back(spec);
        }

        /*
         * Refresh root-owned metadata only after the user has reviewed the
         * complete plan and PolicyKit has authorized this exact execution.
         * Every planned package is pinned to the reviewed version. Removal is
         * prohibited and implicit Recommends/Suggests are disabled so APT
         * cannot silently broaden the approved native dependency plan.
         */
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
                approved_specs, simulation_output, validation_error)) {
            std::fprintf(stderr, "%s\n", validation_error.c_str());
            return 66;
        }

        return execute_apt(std::move(arguments));
    }

    std::fprintf(
        stderr,
        "Usage: infiltrator-software-update-helper "
        "apply-plan PACKAGE=VERSION...\n");
    return 64;
}
