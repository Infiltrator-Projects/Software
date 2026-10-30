// SPDX-License-Identifier: GPL-3.0-or-later
// Native C++17 documentation and release-ownership contracts.
#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>
namespace fs = std::filesystem;
static std::string read(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot read " + path.string());
    return {std::istreambuf_iterator<char>(file), {}};
}
static void check(bool success, const std::string& error) {
    if (!success) throw std::runtime_error(error);
}
static std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return value;
}
int main() {
    try {
        const std::string index = read("docs/README.md");
        const std::string readme = read("README.md");
        for (const auto& entry : fs::directory_iterator("docs")) {
            const fs::path file = entry.path();
            if (!entry.is_regular_file() || file.extension() != ".md" ||
                file.filename() == "README.md") continue;
            check(index.find("(" + file.filename().string() + ")") != std::string::npos,
                  "docs/README.md fails to index " + file.filename().string());
        }
        std::vector<std::string> workflows;
        for (const auto& entry : fs::directory_iterator(".github/workflows"))
            if (entry.is_regular_file()) workflows.push_back(entry.path().filename().string());
        check(workflows.size() == 1 && workflows.front() == "ci.yml",
              "CI publication must have exactly one permanent workflow: ci.yml");

        check(!std::regex_search(readme,
                std::regex(R"(\bCommon\s+[0-9]+\.[0-9]+\.[0-9]+\b)")),
              "README duplicates the authoritative Common version");

        const std::array<std::pair<const char*, const char*>, 5> maintained{{
            {"architecture", "docs/ARCHITECTURE.md"},
            {"package_engine", "docs/PACKAGE_ENGINE.md"},
            {"performance", "docs/PERFORMANCE.md"},
            {"roadmap", "docs/ROADMAP.md"},
            {"state", "docs/STATE.md"},
        }};
        const std::regex installed_fallback(
            R"(\bInstalled\b.*\b(uses|retains|falls\s+back)\b.*\bfallback\b)",
            std::regex::icase);
        const std::regex installed_optional(
            R"(\bInstalled\b.*\bprefer(s|red)?\b.*\b(engine|shared\s+state)\b)",
            std::regex::icase);
        for (const auto& [label, file] : maintained) {
            const std::string source = read(file);
            check(lowercase(source).find("compatibility fallback") == std::string::npos,
                  std::string(label) + " reintroduces the retired compatibility fallback");
            size_t start = 0;
            while (start <= source.size()) {
                const size_t end = source.find('\n', start);
                const std::string line = source.substr(start,
                    end == std::string::npos ? std::string::npos : end - start);
                check(!std::regex_search(line, installed_fallback),
                      std::string(label) + " reintroduces Installed fallback: " + line);
                check(!std::regex_search(line, installed_optional),
                      std::string(label) + " makes the Installed engine authority optional: " + line);
                if (end == std::string::npos) break;
                start = end + 1;
            }
            if (std::string(label) == "state")
                check(!std::regex_search(source,
                    std::regex(R"(schema version\s+[0-9]+)", std::regex::icase)),
                    "State prose duplicates source-owned schema number");
        }
        std::cout << "PASS: maintained documentation and exclusive release ownership\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Software documentation contracts failed: " << error.what() << '\n';
        return 1;
    }
}
