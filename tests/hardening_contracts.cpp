// SPDX-License-Identifier: GPL-3.0-or-later
// Native C++17 forensic hardening contracts; formerly a Python CI script.
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static std::string read(const char* path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error(std::string("cannot read ") + path);
    return {std::istreambuf_iterator<char>(file), {}};
}
static void require(bool condition, const std::string& failure) {
    if (!condition) throw std::runtime_error(failure);
}
static bool contains(const std::string& data, const std::string& text) {
    return data.find(text) != std::string::npos;
}
int main() {
    try {
        const auto apt_configuration = read("src/engine/debian_apt_configuration.cpp");
        const auto preferences = read("src/engine/debian_preferences.cpp");
        const auto phased = read("src/engine/debian_phased_updates.cpp");
        const auto release_script = read("tools/release.sh");
        const auto cmake = read("CMakeLists.txt");

        const std::array<std::pair<const char*, const std::string*>, 3> native{{
            {"APT configuration", &apt_configuration},
            {"APT preferences", &preferences},
            {"phased updates", &phased}
        }};
        constexpr std::array<const char*, 7> prohibited{
            "g_spawn_", "g_subprocess_", "std::system(", "popen(",
            "fork(", "posix_spawn", "execv"
        };
        for (const auto& [name, content] : native)
            for (const char* process_api : prohibited)
                require(!contains(*content, process_api),
                        std::string("native ") + name + " regained process execution via " + process_api);

        require(!contains(preferences, "apt-config"), "APT preferences regained apt-config dependency");
        require(!contains(phased, "apt-config"), "phased updates regained apt-config dependency");
        require(contains(cmake, "src/engine/debian_apt_configuration.cpp"),
                "native APT configuration source missing from CMake");
        require(contains(preferences, "\"Dir::Etc::Preferences\""),
                "APT preferences directory policy missing");
        require(contains(preferences, "\"Dir::Etc::PreferencesParts\""),
                "APT preferences parts policy missing");
        require(contains(apt_configuration, "extension == \".conf\""),
                "APT configuration .conf filter missing");
        require(contains(preferences, "extension == \".pref\""),
                "APT preferences .pref filter missing");

        require(contains(release_script, "(.digest // \"\")"),
                "release must verify published asset digest");
        require(contains(release_script, "sha256sum"),
                "release checksum verification missing");
        require(!contains(release_script, "--clobber"),
                "release must never clobber existing published bytes");
        std::cout << "PASS: native APT policy boundaries and immutable release recovery\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Software hardening contracts failed: " << error.what() << '\n';
        return 1;
    }
}
