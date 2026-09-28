// SPDX-License-Identifier: GPL-3.0-or-later
#include "apt_plan_guard.hpp"
#include "core/exact_transaction_spec.hpp"

#include <glib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

bool write_full(
    const int fd,
    const std::string_view content)
{
    std::size_t offset = 0U;
    while (offset < content.size()) {
        const ssize_t written =
            write(
                fd,
                content.data() + offset,
                content.size() - offset);
        if (written > 0) {
            offset += static_cast<std::size_t>(written);
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

bool sync_directory(const char *directory)
{
    const int fd =
        open(
            directory,
            O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    const bool synced = fsync(fd) == 0;
    const bool closed = close(fd) == 0;
    return synced && closed;
}

bool valid_automation_setting(const std::string_view key, const std::string_view value)
{
    if (value.empty() || value.size() > 512U ||
        value.find('\n') != std::string_view::npos ||
        value.find('\r') != std::string_view::npos) {
        return false;
    }

    const bool boolean_key =
        key == "auto-update-packages" ||
        key == "auto-remove-obsolete" ||
        key == "install-recommends" ||
        key == "keep-configuration" ||
        key == "snapshot-before-system-updates";
    if (boolean_key) {
        return value == "true" || value == "false";
    }

    const bool integer_key =
        key == "first-refresh-minutes" ||
        key == "recurring-refresh-minutes";
    if (integer_key) {
        return std::all_of(
            value.begin(),
            value.end(),
            [](const unsigned char ch) {
                return std::isdigit(ch) != 0;
            });
    }

    return key == "ignore";
}

int configure_automation(const int argc, char **argv)
{
    static constexpr const char *kDirectory =
        "/etc/infiltrator-software";
    static constexpr const char *kPath =
        "/etc/infiltrator-software/automatic-updates.conf";

    std::vector<std::string> values;
    for (int index = 2; index < argc; ++index) {
        const std::string setting(argv[index]);
        const std::size_t equals = setting.find('=');
        if (equals == std::string::npos || equals == 0U ||
            !valid_automation_setting(
                std::string_view(setting).substr(0U, equals),
                std::string_view(setting).substr(equals + 1U))) {
            std::fprintf(stderr, "Invalid automatic-update setting: %s\n", argv[index]);
            return 64;
        }
        values.push_back(setting);
    }

    std::error_code ec;
    std::filesystem::create_directories(kDirectory, ec);
    if (ec) {
        std::fprintf(stderr, "Unable to create %s: %s\n", kDirectory, ec.message().c_str());
        return 1;
    }

    std::string content =
        "# Managed by Infiltrator Software\n";
    for (const std::string &value : values) {
        content += value;
        content += '\n';
    }

    std::string pattern =
        std::string(kDirectory) +
        "/.automatic-updates.conf-XXXXXX";
    std::vector<char> writable(
        pattern.begin(),
        pattern.end());
    writable.push_back('\0');

    const int fd = mkstemp(writable.data());
    if (fd < 0) {
        std::fprintf(
            stderr,
            "Unable to create automatic-update configuration: %s\n",
            std::strerror(errno));
        return 1;
    }

    const std::string temporary(writable.data());
    bool ok =
        fchmod(fd, 0600) == 0 &&
        write_full(fd, content) &&
        fsync(fd) == 0;
    if (close(fd) != 0) {
        ok = false;
    }
    if (!ok) {
        const int saved_errno = errno;
        std::filesystem::remove(temporary, ec);
        std::fprintf(
            stderr,
            "Unable to durably write automatic-update configuration: %s\n",
            std::strerror(saved_errno));
        return 1;
    }

    if (rename(temporary.c_str(), kPath) != 0) {
        const int saved_errno = errno;
        std::filesystem::remove(temporary, ec);
        std::fprintf(
            stderr,
            "Unable to publish automatic-update configuration: %s\n",
            std::strerror(saved_errno));
        return 1;
    }
    if (!sync_directory(kDirectory)) {
        std::fprintf(
            stderr,
            "Automatic-update configuration was renamed, but the directory "
            "could not be durably synchronized.\n");
        return 1;
    }
    return 0;
}

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

    for (const char raw : package) {
        const unsigned char ch =
            static_cast<unsigned char>(raw);
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
        for (const char raw : version) {
            const unsigned char ch =
                static_cast<unsigned char>(raw);
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
    for (const char raw : token) {
        const unsigned char ch =
            static_cast<unsigned char>(raw);
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
    for (const char raw : std::string_view(uid)) {
        const unsigned char ch =
            static_cast<unsigned char>(raw);
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

bool sha256_file(
    const std::filesystem::path &path,
    std::string &digest)
{
    digest.clear();
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;

    GChecksum *checksum =
        g_checksum_new(G_CHECKSUM_SHA256);
    if (checksum == nullptr) return false;

    std::array<char, 64U * 1024U> buffer{};
    while (input) {
        input.read(
            buffer.data(),
            static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        if (count > 0) {
            g_checksum_update(
                checksum,
                reinterpret_cast<const guchar *>(buffer.data()),
                static_cast<gsize>(count));
        }
    }
    if (input.bad()) {
        g_checksum_free(checksum);
        return false;
    }

    const gchar *text = g_checksum_get_string(checksum);
    if (text != nullptr) digest = text;
    g_checksum_free(checksum);
    return digest.size() == 64U;
}

bool verify_downloaded_artifacts(
    const std::filesystem::path &archive_directory,
    const std::vector<infiltrator::software::ExactTransactionSpec> &expected,
    std::string &error)
{
    error.clear();
    std::vector<bool> matched(expected.size(), false);
    std::size_t deb_count = 0U;
    std::error_code ec;

    for (const auto &entry :
         std::filesystem::directory_iterator(archive_directory, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec) || ec) {
            ec.clear();
            continue;
        }
        if (entry.path().extension() != ".deb") continue;
        ++deb_count;

        std::string digest;
        if (!sha256_file(entry.path(), digest)) {
            error =
                "Unable to hash downloaded package artifact " +
                entry.path().filename().string() + ".";
            return false;
        }

        bool found = false;
        for (std::size_t index = 0U; index < expected.size(); ++index) {
            if (!matched[index] &&
                g_ascii_strcasecmp(
                    digest.c_str(),
                    expected[index].sha256.c_str()) == 0) {
                matched[index] = true;
                found = true;
                break;
            }
        }
        if (!found) {
            error =
                "APT downloaded a package artifact that was not part of the "
                "reviewed SHA-256 set: " +
                entry.path().filename().string() + ".";
            return false;
        }
    }

    if (ec) {
        error =
            "Unable to inspect downloaded package artifacts: " +
            ec.message();
        return false;
    }
    if (deb_count != expected.size() ||
        std::find(matched.begin(), matched.end(), false) != matched.end()) {
        error =
            "The downloaded package artifact set does not exactly match the "
            "reviewed transaction.";
        return false;
    }
    return true;
}

bool make_transaction_archive_directory(
    std::filesystem::path &directory,
    std::string &error)
{
    error.clear();
    std::string pattern =
        "/var/cache/apt/archives/infiltrator-software-XXXXXX";
    std::vector<char> writable(pattern.begin(), pattern.end());
    writable.push_back('\0');
    char *created = mkdtemp(writable.data());
    if (created == nullptr) {
        error =
            "Unable to create isolated package archive directory: " +
            std::string(std::strerror(errno));
        return false;
    }
    directory = created;
    std::error_code ec;
    std::filesystem::create_directories(directory / "partial", ec);
    if (ec) {
        std::filesystem::remove_all(directory, ec);
        error =
            "Unable to prepare isolated package archive directory.";
        return false;
    }
    return true;
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


} // namespace

int main(int argc, char **argv)
{
    if (geteuid() != 0) {
        std::fprintf(
            stderr,
            "infiltrator-software-update-helper must run as root.\n");
        return 1;
    }

    if (argc >= 2 &&
        std::strcmp(argv[1], "configure-automation") == 0) {
        return configure_automation(argc, argv);
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
        enum class ConffilePolicy {
            defaults,
            keep_local,
            take_maintainer
        };
        ConffilePolicy conffile_policy =
            ConffilePolicy::defaults;
        bool purge_removals = false;

        while (specification_start < argc) {
            const std::string_view option(
                argv[specification_start]);
            if (option.rfind(
                    "--progress-token=", 0U) == 0U) {
                progress_token =
                    std::string(option.substr(17U));
                if (!safe_progress_token(progress_token)) {
                    std::fprintf(
                        stderr,
                        "Invalid update progress token.\n");
                    return 64;
                }
                ++specification_start;
                continue;
            }
            if (option == "--force-confold") {
                conffile_policy =
                    ConffilePolicy::keep_local;
                ++specification_start;
                continue;
            }
            if (option == "--force-confnew") {
                conffile_policy =
                    ConffilePolicy::take_maintainer;
                ++specification_start;
                continue;
            }
            if (option == "--purge-removals") {
                purge_removals = true;
                ++specification_start;
                continue;
            }
            break;
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
            "--no-install-suggests"};
        if (conffile_policy ==
            ConffilePolicy::keep_local) {
            arguments.emplace_back("-o");
            arguments.emplace_back(
                "Dpkg::Options::=--force-confold");
        } else if (
            conffile_policy ==
            ConffilePolicy::take_maintainer) {
            arguments.emplace_back("-o");
            arguments.emplace_back(
                "Dpkg::Options::=--force-confnew");
        } else {
            arguments.emplace_back("-o");
            arguments.emplace_back(
                "Dpkg::Options::=--force-confdef");
        }
        arguments.emplace_back("install");
        arguments.reserve(static_cast<std::size_t>(argc) + 9U);

        bool has_removal = false;
        std::vector<std::string> approved_specs;
        std::vector<infiltrator::software::ExactTransactionSpec>
            reviewed_specs;
        approved_specs.reserve(
            static_cast<std::size_t>(
                argc - specification_start));
        reviewed_specs.reserve(approved_specs.capacity());

        for (int index = specification_start; index < argc; ++index) {
            const std::string approved(argv[index]);
            infiltrator::software::ExactTransactionSpec reviewed;
            std::string spec;
            bool removal = false;

            if (resolved_plan &&
                approved.rfind("x2|", 0U) == 0U) {
                std::string decode_error;
                if (!infiltrator::software::decode_exact_transaction_spec(
                        approved, reviewed, decode_error)) {
                    std::fprintf(
                        stderr,
                        "Invalid exact transaction specification: %s\n",
                        decode_error.c_str());
                    return 64;
                }
                removal =
                    reviewed.action ==
                    infiltrator::software::TransactionAction::remove;
                spec =
                    reviewed.package_id + "=" +
                    reviewed.version;
            } else {
                removal =
                    approved.rfind("remove:", 0U) == 0U;
                spec =
                    removal ? approved.substr(7U) : approved;

                /*
                 * Old apply-plan removal specs are retained for the automatic
                 * maintenance path because removals have no repository payload
                 * to bind. Install/upgrade plans must use x2 artifact specs.
                 */
                if (resolved_plan && !removal) {
                    std::fprintf(
                        stderr,
                        "Install/upgrade apply-plan specifications must include "
                        "the reviewed repository artifact identity.\n");
                    return 64;
                }
                reviewed.action =
                    removal
                        ? infiltrator::software::TransactionAction::remove
                        : infiltrator::software::TransactionAction::install;
                const std::size_t equals = spec.find('=');
                if (equals != std::string::npos) {
                    reviewed.package_id = spec.substr(0U, equals);
                    reviewed.version = spec.substr(equals + 1U);
                }
            }

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

            if (legacy_upgrade && !installed_package(spec)) {
                std::fprintf(
                    stderr,
                    "Refusing to install a requested package that is not "
                    "already installed through the legacy upgrade path: %s\n",
                    argv[index]);
                return 65;
            }

            approved_specs.push_back(
                removal ? "remove:" + spec : spec);
            reviewed_specs.push_back(std::move(reviewed));
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
        } else if (purge_removals) {
            /*
             * Automatic maintenance reviews an autoremove --purge result.
             * Preserve the purge semantic while still executing the exact,
             * version-pinned removal set through the normal plan guard.
             */
            arguments.insert(arguments.begin() + 1, "--purge");
        }

        /*
         * A package may already have reached the approved final version before
         * this helper runs (for example, the user retried a stale Updates row
         * after an earlier transaction completed). Verify that state directly
         * with dpkg before doing any network work. If every approved final
         * state is already true, the transaction is a successful no-op.
         */
        std::vector<std::string> pending_specs;
        std::vector<infiltrator::software::ExactTransactionSpec>
            pending_artifacts;
        pending_specs.reserve(approved_specs.size());
        pending_artifacts.reserve(approved_specs.size());
        for (std::size_t index = 0U;
             index < approved_specs.size();
             ++index) {
            const std::string &approved = approved_specs[index];
            bool satisfied = false;
            std::string state_error;
            if (!approved_spec_already_satisfied(
                    approved, satisfied, state_error)) {
                std::fprintf(stderr, "%s\n", state_error.c_str());
                return 67;
            }
            if (!satisfied) {
                pending_specs.push_back(approved);
                if (resolved_plan &&
                    reviewed_specs[index].action !=
                        infiltrator::software::TransactionAction::remove) {
                    pending_artifacts.push_back(reviewed_specs[index]);
                }
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

        std::filesystem::path archive_directory;
        if (!pending_artifacts.empty()) {
            std::string archive_error;
            if (!make_transaction_archive_directory(
                    archive_directory, archive_error)) {
                std::fprintf(stderr, "%s\n", archive_error.c_str());
                return 69;
            }

            std::vector<std::string> download_arguments = arguments;
            download_arguments.insert(
                download_arguments.begin(),
                "-o");
            download_arguments.insert(
                download_arguments.begin() + 1,
                "Dir::Cache::archives=" +
                    archive_directory.string());
            download_arguments.insert(
                download_arguments.begin() + 2,
                "--download-only");

            write_progress(
                progress_path,
                progress_token,
                "download",
                "Downloading the exact reviewed package artifacts.");
            const int download_status =
                run_apt(std::move(download_arguments));
            if (download_status != 0) {
                std::error_code cleanup_error;
                std::filesystem::remove_all(
                    archive_directory, cleanup_error);
                return download_status;
            }

            if (!verify_downloaded_artifacts(
                    archive_directory,
                    pending_artifacts,
                    archive_error)) {
                std::fprintf(stderr, "%s\n", archive_error.c_str());
                std::error_code cleanup_error;
                std::filesystem::remove_all(
                    archive_directory, cleanup_error);
                return 70;
            }

            /*
             * The final APT invocation is not allowed to fetch replacement
             * bytes after verification. It must consume only the isolated
             * archive set whose SHA-256 values matched the reviewed plan.
             */
            arguments.insert(arguments.begin(), "-o");
            arguments.insert(
                arguments.begin() + 1,
                "Dir::Cache::archives=" +
                    archive_directory.string());
            arguments.insert(
                arguments.begin() + 2,
                "--no-download");
        }

        int install_status = 0;
        if (progress_token.empty()) {
            install_status = run_apt(std::move(arguments));
        } else {
            write_progress(
                progress_path,
                progress_token,
                pending_artifacts.empty() ? "install" : "verify",
                pending_artifacts.empty()
                    ? "Applying the approved package transaction."
                    : "Installing the SHA-256 verified reviewed artifacts.");
            install_status =
                run_apt_with_progress(
                    std::move(arguments),
                    progress_path,
                    progress_token);
        }

        if (!archive_directory.empty()) {
            std::error_code cleanup_error;
            std::filesystem::remove_all(
                archive_directory, cleanup_error);
        }
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
        "apply-plan [--progress-token=TOKEN] [--force-confold|--force-confnew] "
        "[--purge-removals] [remove:]PACKAGE=VERSION... | repair-configure | "
        "configure-automation key=value...\n");
    return 64;
}
