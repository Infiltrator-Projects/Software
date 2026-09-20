// SPDX-License-Identifier: GPL-3.0-or-later
#include <cctype>
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

int execute_apt(std::vector<std::string> arguments)
{
    const char *path = apt_get_path();
    if (path == nullptr) {
        std::fprintf(stderr, "apt-get is not available.\n");
        return 127;
    }

    std::vector<char *> argv;
    argv.reserve(arguments.size() + 2U);
    argv.push_back(const_cast<char *>("apt-get"));
    for (std::string &argument : arguments) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);

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

    if (argc == 2 && std::strcmp(argv[1], "refresh") == 0) {
        return execute_apt({"update"});
    }

    if (argc >= 3 && std::strcmp(argv[1], "apply") == 0) {
        std::vector<std::string> arguments{
            "-y", "--no-remove", "install"};
        arguments.reserve(static_cast<std::size_t>(argc) + 3U);

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
            if (!installed_package(spec)) {
                std::fprintf(
                    stderr,
                    "Refusing to install a requested package that is not "
                    "already installed: %s\n",
                    argv[index]);
                return 65;
            }
            arguments.push_back(spec);
        }

        return execute_apt(std::move(arguments));
    }

    std::fprintf(
        stderr,
        "Usage: infiltrator-software-update-helper refresh\n"
        "   or: infiltrator-software-update-helper apply PACKAGE[=VERSION]...\n");
    return 64;
}
