// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/debian_repository.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <unistd.h>

namespace {

std::filesystem::path make_temp_directory()
{
    std::string pattern =
        (std::filesystem::temp_directory_path() /
         "infiltrator-software-repository-XXXXXX").string();
    std::vector<char> writable(pattern.begin(), pattern.end());
    writable.push_back('\0');
    char *created = mkdtemp(writable.data());
    assert(created != nullptr);
    return std::filesystem::path(created);
}

void write_file(
    const std::filesystem::path &path,
    const std::string &content)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    assert(output);
    output << content;
    assert(output.good());
}

} // namespace

int main()
{
    using namespace infiltrator::software;

    const std::string packages =
        "Package: hello\n"
        "Version: 2.0-1\n"
        "Architecture: amd64\n"
        "Filename: pool/main/h/hello/hello_2.0-1_amd64.deb\n"
        "Size: 1234\n"
        "Installed-Size: 9\n"
        "SHA256: deadbeef\n"
        "Depends: libc6 (>= 2.38)\n"
        "\n"
        "Package: hello-data\n"
        "Version: 2.0-1\n"
        "Architecture: all\n"
        "Filename: pool/main/h/hello/hello-data_2.0-1_all.deb\n"
        "Size: 321\n"
        "Installed-Size: 4\n"
        "SHA256: feedface\n";

    const std::string release =
        "Origin: Example\n"
        "Suite: stable\n"
        "Codename: stable\n"
        "Architectures: amd64 all\n"
        "Components: main\n"
        "SHA256:\n"
        " 2b1cd1965de07ffaf23ac2fbf5af2a7277ce114f53110a7dd46c40f1f53c8e91 323 main/binary-amd64/Packages\n";

    std::string error;
    const DebianReleaseMetadata parsed =
        DebianReleaseMetadata::parse(release, error);
    assert(error.empty());
    assert(parsed.suite == "stable");
    assert(parsed.codename == "stable");
    assert(parsed.architectures.size() == 2U);
    assert(parsed.components.size() == 1U);
    assert(parsed.sha256_entries.size() == 1U);

    const std::filesystem::path repository =
        make_temp_directory();
    const std::filesystem::path cache =
        make_temp_directory();

    write_file(repository / "dists/stable/Release", release);
    write_file(
        repository / "dists/stable/main/binary-amd64/Packages",
        packages);

    DebianRepositorySource source;
    source.id = "fixture stable/main";
    source.uri = "file://" + repository.string();
    source.suite = "stable";
    source.components = {"main"};
    source.verify_signatures = false;

    const DebianRepositorySnapshot snapshot =
        DebianRepositoryRefresh::refresh(
            source, "amd64", cache.string(), error);
    assert(error.empty());
    assert(snapshot.source_id == source.id);
    assert(snapshot.packages.size() == 2U);
    assert(snapshot.verified_indexes.size() == 1U);
    assert(snapshot.packages[0].package == "hello");
    assert(snapshot.packages[1].package == "hello-data");
    assert(std::filesystem::exists(
        cache / "fixture_stable_main/Release"));

    const std::string bad_release =
        "Suite: stable\n"
        "Architectures: amd64\n"
        "Components: main\n"
        "SHA256:\n"
        " 0000000000000000000000000000000000000000000000000000000000000000 323 main/binary-amd64/Packages\n";
    write_file(repository / "dists/stable/Release", bad_release);

    error.clear();
    const DebianRepositorySnapshot rejected =
        DebianRepositoryRefresh::refresh(
            source, "amd64", "", error);
    assert(rejected.packages.empty());
    assert(error.find("SHA-256 mismatch") != std::string::npos);

    std::filesystem::remove_all(repository);
    std::filesystem::remove_all(cache);
    return 0;
}
