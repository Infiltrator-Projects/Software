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

    const std::filesystem::path armored_repository =
        make_temp_directory();
    const std::filesystem::path armored_cache =
        make_temp_directory();

    const std::string armored_packages =
R"(Package: hello
Version: 2.0-1
Architecture: amd64
Filename: pool/main/h/hello/hello_2.0-1_amd64.deb
Size: 1234
Installed-Size: 9
SHA256: deadbeef

)";

    const std::string armored_inrelease =
R"(-----BEGIN PGP SIGNED MESSAGE-----
Hash: SHA512

Origin: Example
Suite: stable
Codename: stable
Architectures: amd64
Components: main
SHA256:
 3447e3d43ad0e9d58efa7010b1f61fbad8b06369dac2ca3579591cd91bccdc77 147 main/binary-amd64/Packages
-----BEGIN PGP SIGNATURE-----

iQEzBAEBCgAdFiEEcZOoQMEq52Q29R2V0SrrBeZiyaAFAmq0cl8ACgkQ0SrrBeZi
yaBdLwf/SUlmR6WS9fdwVfx8QwPuP/NYxsX1f54PnrULtRYV4m821CUXu8IDO7xR
VCPz+jv/HMYllYg4PseQyKGZZnE1vxWEqaVOZseDNMWtQH9AAKqCqrqDVK2+bdNl
AZ0ji8Krcg+bkztKWgoLzPEYHUH+QP6rARX7SSv7xItu5U6X6VkvIOF++7hxURnM
yT8bsbdE6E34TN2uCZ9l6DJq+1dkTETmAMuzt9h3oEShvw82gea3TZ5qNnoXRGmP
FZfj7pfbdJ4zkx+m/b5AhUloVmAc1Ly1Heut1PZX10KzIZamPfF6hg/9JxFirB0x
Vpm/ot/ci85X7MyF5Uwgq2smYhshjw==
=b3lp
-----END PGP SIGNATURE-----
)";

    const std::string armored_key =
R"(-----BEGIN PGP PUBLIC KEY BLOCK-----

mQENBGq0cl4BCAC/uVId3+OUxnAzvT29O4snYkiHej2lYDccZ+aEDsqR7QDA6Igr
rUEnS9XIVcO/x1VAqGsZoETIIi2lmlJD7sWXELDGJtDHsWMu4FNFSKCU6fNH5BMO
Kidig3OR6vIB7K5W5jcaJXdsW6P+8YcjRWQFBY/kA9fDL7MGN66UfgyBgVDXa4RI
tbST16RT/TfASl5gTT22OimT+Pr7sW+VRtMnVV1nHHZloQgKvYVwFenyca8PCzoW
36JCET4+A+9gTMhS2BPmfcfgqa6ihlLw4ZwxtGzRKE1mwNCZ9xNYNa5m8QNOLvbn
WPMm3rxpuYnFO05Czp76xjUHZ9v5LUyGJ9kdABEBAAG0MkluZmlsdHJhdG9yIFRl
c3QgUmVwb3NpdG9yeSA8dGVzdEBleGFtcGxlLmludmFsaWQ+iQFPBBMBCgA5FiEE
cZOoQMEq52Q29R2V0SrrBeZiyaAFAmq0cl4DGy8EBQsJCAcCBhUKCQgLAgQWAgMB
Ah4BAheAAAoJENEq6wXmYsmgTugIAK6/oy2xT1piKsTPrluP4hfGYYIwTAL4bkFv
BvY3AozpwFcB3AhElZJLxFdIfo1dxBUGDGGNNT5D/TPspDBiEkdssbqsENfd6FT6
VCcWQHGIarZ9YbWZC2iH3cnb+EKw2zCndjRnIbarWB2JCdnVwpgS34c62H1330Ps
vmYTQJlI8jR5knJquOVaCvlRep/pHPyp9MuLbqo/hLkX08vMBhxjzpG+G1ZkoBvc
Z906RAJzWvfBzQht4m6LHopdSEgATr18X7b6619jeEsgnL+Z+mel9wjBhJHtsjUB
I7P1MsIy6V+qEZaKilX9nIcTzAO8m9aw7CpMiV6tbTHRknult08=
=9qj0
-----END PGP PUBLIC KEY BLOCK-----
)";

    write_file(
        armored_repository / "dists/stable/InRelease",
        armored_inrelease);
    write_file(
        armored_repository /
            "dists/stable/main/binary-amd64/Packages",
        armored_packages);
    const std::filesystem::path armored_key_path =
        armored_repository / "docker-style.asc";
    write_file(armored_key_path, armored_key);

    DebianRepositorySource armored_source;
    armored_source.id = "armored signed-by stable/main";
    armored_source.uri =
        "file://" + armored_repository.string();
    armored_source.suite = "stable";
    armored_source.components = {"main"};
    armored_source.keyrings = {armored_key_path.string()};

    error.clear();
    const DebianRepositorySnapshot armored_snapshot =
        DebianRepositoryRefresh::refresh(
            armored_source,
            "amd64",
            armored_cache.string(),
            error);
    assert(error.empty());
    assert(armored_snapshot.packages.size() == 1U);
    assert(armored_snapshot.packages[0].package == "hello");

    std::filesystem::remove_all(repository);
    std::filesystem::remove_all(cache);
    std::filesystem::remove_all(armored_repository);
    std::filesystem::remove_all(armored_cache);
    return 0;
}
