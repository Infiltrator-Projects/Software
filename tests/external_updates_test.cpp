// SPDX-License-Identifier: GPL-3.0-or-later
#include "external/external_updates.hpp"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

void write_script(
    const std::filesystem::path &path,
    const std::string &body)
{
    std::ofstream output(path);
    assert(output);
    output << "#!/bin/sh\n" << body;
    output.close();
    assert(output);
    assert(chmod(path.c_str(), 0755) == 0);
}

std::string read_all(
    const std::filesystem::path &path)
{
    std::ifstream input(path);
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

} // namespace

int main()
{
    namespace fs = std::filesystem;
    using namespace infiltrator::software;

    const fs::path root =
        fs::temp_directory_path() /
        ("software-external-test-" +
         std::to_string(
             static_cast<unsigned long long>(
                 ::getpid())));
    fs::create_directories(root);
    const fs::path trace = root / "trace";
    (void)setenv("TRACE", trace.c_str(), 1);

    write_script(
        root / "flatpak",
        R"(case "$*" in
  *remote-ls*)
    if [ "$FAIL_SYSTEM_SCAN" = 1 ] && [ "$2" = "--system" ]; then
      echo 'System Flatpak repository is unavailable' >&2
      exit 1
    fi
    printf 'org.example.App\tapp/org.example.App/x86_64/stable\t2.0\t1234\n'
    printf 'org.example.Runtime\truntime/org.example.Runtime/x86_64/24.08\t24.08\t2345\n'
    exit 0
    ;;
  *uninstall*--unused*)
    echo "flatpak-uninstall $*" >> "$TRACE"
    exit 0
    ;;
  *remotes*)
    printf 'flathub\n'
    exit 0
    ;;
  *remote-info*)
    exit 0
    ;;
  *install*)
    echo "flatpak-theme-install $*" >> "$TRACE"
    exit 0
    ;;
  *update*)
    echo "flatpak-update $*" >> "$TRACE"
    exit 0
    ;;
esac
exit 0
)");

    write_script(
        root / "gsettings",
        "printf \"'Mint-Y-Dark'\\\\n\"\n");

    write_script(
        root / "cinnamon-spice-updater",
R"(if [ "$1" = "--list-simple" ]; then
  if [ "$FAIL_CINNAMON_SCAN" = 1 ]; then
    echo 'Cinnamon updates failed: cache unavailable'
    exit 0
  fi
  case "$2" in
    applet) printf 'weather@mock\n' ;;
    desklet) printf 'clock@mock\n' ;;
    extension) printf 'tiling@mock\n' ;;
    theme) printf 'theme@mock\n' ;;
    action) echo 'unsupported Cinnamon Spice type' >&2; exit 2 ;;
  esac
  exit 0
fi
if [ "$1" = "--update-all" ]; then
  if [ "$FAIL_CINNAMON_APPLY" = 1 ]; then
    echo 'Cinnamon updates failed: installation failed'
    exit 0
  fi
  echo "cinnamon-update-all" >> "$TRACE"
  exit 0
fi
exit 1
)");

    const char *old_path = std::getenv("PATH");
    const std::string path =
        root.string() + ":" +
        (old_path == nullptr ? "" : old_path);
    (void)setenv("PATH", path.c_str(), 1);

    std::vector<ExternalUpdate> flatpak;
    std::string error;
    assert(discover_flatpak_updates(flatpak, error));
    assert(error.empty());
    assert(flatpak.size() == 4U);
    bool saw_app = false;
    bool saw_runtime = false;
    for (const ExternalUpdate &update : flatpak) {
        saw_app =
            saw_app ||
            update.kind ==
                ExternalUpdateKind::flatpak_application;
        saw_runtime =
            saw_runtime ||
            update.kind ==
                ExternalUpdateKind::flatpak_runtime;
    }
    assert(saw_app);
    assert(saw_runtime);

    std::vector<ExternalUpdate> selected;
    for (const ExternalUpdate &update : flatpak) {
        if ((!update.user_installation &&
             update.kind == ExternalUpdateKind::flatpak_application) ||
            (update.user_installation &&
             update.kind == ExternalUpdateKind::flatpak_runtime))
            selected.push_back(update);
    }
    assert(selected.size() == 2U);
    assert(selected[0].ref.rfind("app/",0U) == 0U);
    assert(selected[1].ref.rfind("runtime/",0U) == 0U);
    assert(apply_flatpak_updates_selected(selected, error));
    const std::string selected_trace = read_all(trace);
    assert(selected_trace.find("--system -- app/org.example.App/") !=
        std::string::npos);
    assert(selected_trace.find("--user -- runtime/org.example.Runtime/") !=
        std::string::npos);
    assert(selected_trace.find("flatpak-uninstall") == std::string::npos);
    ExternalUpdate invalid = selected.front();
    invalid.ref = "--unsafe";
    selected.push_back(invalid);
    assert(!apply_flatpak_updates_selected(selected, error));
    assert(read_all(trace) == selected_trace);
    selected.pop_back();

    (void)setenv("FAIL_SYSTEM_SCAN", "1", 1);
    flatpak.clear();
    assert(!discover_flatpak_updates(flatpak, error));
    assert(flatpak.empty());
    assert(error.find("System Flatpak installation") !=
        std::string::npos);
    (void)unsetenv("FAIL_SYSTEM_SCAN");

    std::vector<ExternalUpdate> cinnamon;
    assert(discover_cinnamon_updates(cinnamon, error));
    assert(error.empty());
    assert(cinnamon.size() == 4U);
    (void)setenv("FAIL_CINNAMON_SCAN", "1", 1);
    assert(!discover_cinnamon_updates(cinnamon, error));
    assert(cinnamon.empty());
    assert(error.find("cache unavailable") != std::string::npos);
    (void)unsetenv("FAIL_CINNAMON_SCAN");

    std::vector<std::string> flatpak_phases;
    assert(apply_flatpak_updates(true, true, error,
        [&](std::string_view phase) {
            flatpak_phases.emplace_back(phase);
        }));
    assert(error.empty());
    assert(flatpak_phases.size() == 5U);
    assert(flatpak_phases[3].find("system Flatpak updates") !=
        std::string::npos);
    assert(flatpak_phases[4].find("user Flatpak updates") !=
        std::string::npos);
    std::vector<std::string> cinnamon_phases;
    assert(apply_cinnamon_updates(error,
        [&](std::string_view phase) {
            cinnamon_phases.emplace_back(phase);
        }));
    assert(error.empty());
    assert(cinnamon_phases.size() == 1U);
    (void)setenv("FAIL_CINNAMON_APPLY", "1", 1);
    assert(!apply_cinnamon_updates(error));
    assert(error.find("installation failed") != std::string::npos);
    (void)unsetenv("FAIL_CINNAMON_APPLY");

    const std::string logged = read_all(trace);
    assert(logged.find("flatpak-uninstall") != std::string::npos);
    assert(logged.find("flatpak-update") != std::string::npos);
    assert(logged.find("org.gtk.Gtk3theme.Mint-Y-Dark") != std::string::npos);
    assert(logged.find("cinnamon-update-all") != std::string::npos);

    fs::remove_all(root);
    return 0;
}
