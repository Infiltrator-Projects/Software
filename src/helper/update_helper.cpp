// SPDX-License-Identifier: GPL-3.0-or-later
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <unistd.h>
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
            "-y", "--no-remove", "--only-upgrade", "install"};
        arguments.reserve(static_cast<std::size_t>(argc) + 3U);

        for (int index = 2; index < argc; ++index) {
            const std::string spec(argv[index]);
            if (!safe_package_spec(spec)) {
                std::fprintf(
                    stderr,
                    "Invalid package specification: %s\n",
                    argv[index]);
                return 64;
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
