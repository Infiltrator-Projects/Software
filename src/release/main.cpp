// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/engine_service_core.hpp"
#include "engine/debian_candidate.hpp"
#include "release/release_metadata.hpp"

#include <glib.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {
using namespace infiltrator::software;

struct ReleaseInfo {
    std::string edition;
    std::string current_codename;
    std::string target_name;
    std::string target_codename;
    std::filesystem::path directory;
    std::filesystem::path repositories;
    std::vector<std::string> additions;
    std::vector<std::string> removals;
    std::vector<std::string> blacklist;
};

std::string trim(std::string value)
{
    const auto first=value.find_first_not_of(" \t\r\n");
    if (first==std::string::npos) return {};
    const auto last=value.find_last_not_of(" \t\r\n");
    value=value.substr(first,last-first+1U);
    if (value.size()>=2U &&
        ((value.front()=='"' && value.back()=='"') ||
         (value.front()=='\'' && value.back()=='\'')))
        value=value.substr(1U,value.size()-2U);
    return value;
}

class TemporaryDirectory final {
public:
    TemporaryDirectory() = default;
    TemporaryDirectory(const TemporaryDirectory &) = delete;
    TemporaryDirectory &operator=(const TemporaryDirectory &) = delete;

    ~TemporaryDirectory()
    {
        if (!path_.empty()) {
            std::error_code ignored;
            std::filesystem::remove_all(path_, ignored);
        }
    }

    bool create(std::string &error)
    {
        std::error_code ec;
        const std::filesystem::path parent =
            std::filesystem::temp_directory_path(ec);
        if (ec) {
            error =
                "Unable to locate a private release-upgrade planning area: " +
                ec.message();
            return false;
        }

        std::string pattern =
            (parent / "infiltrator-software-release-XXXXXX").string();
        std::vector<char> writable(pattern.begin(), pattern.end());
        writable.push_back('\0');
        char *created = mkdtemp(writable.data());
        if (created == nullptr) {
            error =
                "Unable to create a private release-upgrade planning area: " +
                std::string(std::strerror(errno));
            return false;
        }

        path_ = created;
        if (chmod(path_.c_str(), 0700) != 0) {
            const std::string detail = std::strerror(errno);
            std::filesystem::remove_all(path_, ec);
            path_.clear();
            error =
                "Unable to secure the release-upgrade planning area: " +
                detail;
            return false;
        }
        return true;
    }

    const std::filesystem::path &path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

class ScopedEnvironment final {
public:
    ScopedEnvironment() = default;
    ScopedEnvironment(const ScopedEnvironment &) = delete;
    ScopedEnvironment &operator=(const ScopedEnvironment &) = delete;

    ~ScopedEnvironment()
    {
        for (auto iterator = saved_.rbegin();
             iterator != saved_.rend();
             ++iterator) {
            if (iterator->second.has_value()) {
                (void)setenv(
                    iterator->first.c_str(),
                    iterator->second->c_str(),
                    1);
            } else {
                (void)unsetenv(iterator->first.c_str());
            }
        }
    }

    bool set(
        const char *name,
        const std::string &value,
        std::string &error)
    {
        const char *current = std::getenv(name);
        saved_.emplace_back(
            name,
            current == nullptr
                ? std::optional<std::string>{}
                : std::optional<std::string>{current});
        if (setenv(name, value.c_str(), 1) != 0) {
            error =
                "Unable to isolate release-upgrade planning state: " +
                std::string(std::strerror(errno));
            saved_.pop_back();
            return false;
        }
        return true;
    }

private:
    std::vector<
        std::pair<std::string, std::optional<std::string>>> saved_;
};

bool write_all(const int fd, const std::string_view content)
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

bool sync_directory(
    const std::filesystem::path &directory,
    std::string &error)
{
    const int fd =
        open(
            directory.c_str(),
            O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) {
        error =
            "Unable to open " + directory.string() +
            " for durability verification: " +
            std::string(std::strerror(errno));
        return false;
    }
    const bool synced = fsync(fd) == 0;
    const int close_status = close(fd);
    if (!synced || close_status != 0) {
        error =
            "Unable to durably publish changes in " +
            directory.string() + ".";
        return false;
    }
    return true;
}

bool durable_write_text(
    const std::filesystem::path &destination,
    const std::string_view content,
    const mode_t mode,
    std::string &error)
{
    std::error_code ec;
    std::filesystem::create_directories(
        destination.parent_path(), ec);
    if (ec) {
        error =
            "Unable to create " +
            destination.parent_path().string() + ": " +
            ec.message();
        return false;
    }

    std::string pattern =
        (destination.parent_path() /
         (".infiltrator-release-state-XXXXXX")).string();
    std::vector<char> writable(
        pattern.begin(), pattern.end());
    writable.push_back('\0');

    const int fd = mkstemp(writable.data());
    if (fd < 0) {
        error =
            "Unable to stage " + destination.string() + ": " +
            std::string(std::strerror(errno));
        return false;
    }

    const std::filesystem::path temporary(
        writable.data());
    bool ok =
        write_all(fd, content) &&
        fchmod(fd, mode) == 0 &&
        fsync(fd) == 0;
    if (close(fd) != 0) {
        ok = false;
    }
    if (!ok) {
        const std::string detail =
            std::strerror(errno);
        std::filesystem::remove(
            temporary, ec);
        error =
            "Unable to durably stage " +
            destination.string() + ": " +
            detail;
        return false;
    }

    if (rename(
            temporary.c_str(),
            destination.c_str()) != 0) {
        const std::string detail =
            std::strerror(errno);
        std::filesystem::remove(
            temporary, ec);
        error =
            "Unable to atomically publish " +
            destination.string() + ": " +
            detail;
        return false;
    }
    return sync_directory(
        destination.parent_path(),
        error);
}

bool durable_copy_file(
    const std::filesystem::path &source,
    const std::filesystem::path &destination,
    const mode_t mode,
    std::string &error)
{
    std::ifstream input(source, std::ios::binary);
    if (!input) {
        error =
            "Unable to read " + source.string() +
            " for durable publication.";
        return false;
    }
    const std::string content{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    if (input.bad()) {
        error =
            "Unable to finish reading " + source.string() + ".";
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(
        destination.parent_path(), ec);
    if (ec) {
        error =
            "Unable to create " +
            destination.parent_path().string() + ": " +
            ec.message();
        return false;
    }

    std::string pattern =
        (destination.parent_path() /
         (".infiltrator-release-" +
          destination.filename().string() +
          "-XXXXXX")).string();
    std::vector<char> writable(pattern.begin(), pattern.end());
    writable.push_back('\0');
    const int fd = mkstemp(writable.data());
    if (fd < 0) {
        error =
            "Unable to stage " + destination.string() + ": " +
            std::string(std::strerror(errno));
        return false;
    }

    const std::filesystem::path temporary(writable.data());
    bool ok =
        write_all(fd, content) &&
        fchmod(fd, mode) == 0 &&
        fsync(fd) == 0;
    if (close(fd) != 0) {
        ok = false;
    }
    if (!ok) {
        const std::string detail = std::strerror(errno);
        std::filesystem::remove(temporary, ec);
        error =
            "Unable to durably stage " + destination.string() +
            ": " + detail;
        return false;
    }

    if (rename(
            temporary.c_str(),
            destination.c_str()) != 0) {
        const std::string detail = std::strerror(errno);
        std::filesystem::remove(temporary, ec);
        error =
            "Unable to atomically publish " +
            destination.string() + ": " + detail;
        return false;
    }
    return sync_directory(
        destination.parent_path(), error);
}

bool durable_remove(
    const std::filesystem::path &path,
    std::string &error)
{
    if (unlink(path.c_str()) != 0) {
        if (errno == ENOENT) {
            return true;
        }
        error =
            "Unable to remove " + path.string() + ": " +
            std::string(std::strerror(errno));
        return false;
    }
    return sync_directory(path.parent_path(), error);
}

std::map<std::string,std::string> read_assignments(const std::filesystem::path &path)
{
    std::map<std::string,std::string> result;
    std::ifstream input(path);
    std::string line;
    while (std::getline(input,line)) {
        line=trim(line);
        if (line.empty() || line.front()=='#') continue;
        const auto eq=line.find('=');
        if (eq==std::string::npos) continue;
        result[trim(line.substr(0U,eq))]=trim(line.substr(eq+1U));
    }
    return result;
}

std::vector<std::string> read_package_list(const std::filesystem::path &path)
{
    std::vector<std::string> result;
    std::ifstream input(path);
    std::string line;
    while (std::getline(input,line)) {
        line=trim(line);
        if (line.empty() || line.front()=='#') continue;
        std::istringstream words(line);
        std::string name;
        while (words>>name) result.push_back(name);
    }
    return result;
}

bool load_release_info(ReleaseInfo &info,std::string &error)
{
    const auto current=read_assignments("/etc/linuxmint/info");
    const auto codename=current.find("CODENAME");
    const auto edition=current.find("EDITION");
    if (codename==current.end() || codename->second.empty()) {
        error="This system does not expose Linux Mint release metadata.";
        return false;
    }
    info.current_codename=codename->second;
    info.edition=edition==current.end()?std::string{}:
        release::normalize_edition(edition->second);
    if (release::meta_package(info.edition).empty()) {
        error="The Linux Mint edition is missing or invalid; release upgrade cannot continue.";
        return false;
    }
    info.directory=std::filesystem::path("/usr/share/mint-upgrade-info")/info.current_codename;

    const auto target=read_assignments(info.directory/"info");
    const auto target_name=target.find("target_name");
    const auto target_codename=target.find("target_codename");
    if (target_name==target.end() || target_codename==target.end() ||
        target_name->second.empty() || target_codename->second.empty()) {
        error="No supported Linux Mint point-release upgrade is available.";
        return false;
    }

    const auto editions=target.find("editions");
    if (editions!=target.end()) {
        if (!release::supported_edition(editions->second,info.edition)) {
            error="The available release upgrade does not support this Linux Mint edition.";
            return false;
        }
    }

    info.target_name=target_name->second;
    info.target_codename=target_codename->second;
    info.repositories=info.directory/"official-package-repositories.list";
    if (!std::filesystem::is_regular_file(info.repositories)) {
        error="The release-upgrade repository definition is missing.";
        return false;
    }
    info.additions=read_package_list(info.directory/"additions");
    info.removals=read_package_list(info.directory/"removals");
    info.blacklist=read_package_list(info.directory/"blacklist");
    return true;
}

std::string base(std::string value)
{
    const auto colon=value.find(':');
    if (colon!=std::string::npos) value.erase(colon);
    return value;
}

std::vector<std::string> specs_for(const TransactionPlan &plan)
{
    std::vector<std::string> result;
    result.reserve(plan.items.size());
    for (const TransactionItem &item:plan.items) {
        if (item.action==TransactionAction::remove)
            result.push_back("remove:"+item.package_id+"="+item.from_version);
        else
            result.push_back(item.package_id+"="+item.to_version);
    }
    std::sort(result.begin(),result.end());
    return result;
}

bool build_plan(const ReleaseInfo &release,TransactionPlan &combined,std::string &error)
{
    TemporaryDirectory workspace;
    if (!workspace.create(error)) {
        return false;
    }
    const std::filesystem::path &root = workspace.path();

    const std::string db=(root/"packages.db").string();
    const std::string cache=(root/"repositories").string();
    ScopedEnvironment environment;
    if (!environment.set(
            "INFILTRATOR_SOFTWARE_STATE_DB", db, error) ||
        !environment.set(
            "INFILTRATOR_SOFTWARE_REPOSITORY_CACHE", cache, error) ||
        !environment.set(
            "INFILTRATOR_SOFTWARE_SOURCES_FILE",
            release.repositories.string(),
            error)) {
        return false;
    }

    DebianCandidatePolicy policy;
    for (const std::string &name:release.blacklist) policy.held_packages.insert(base(name));
    policy.held_packages.insert("linux-kernel-generic");

    EngineServiceCore core(default_package_state_path(),policy);
    if (!core.refresh(error)) return false;

    const auto installed=core.installed();
    const auto updates=core.updates();
    const std::string meta=release::meta_package(release.edition);
    const bool meta_installed=std::any_of(installed.begin(),installed.end(),
        [&meta](const PackageRecord &p) {
            return (p.package_name.empty()?p.id:p.package_name)==meta;
        });
    if (!meta_installed) {
        error="Install "+meta+" before upgrading Linux Mint. This edition's "
            "meta package is required to keep its desktop components installed.";
        return false;
    }
    std::set<std::string> blocked;
    for (const std::string &name:release.blacklist) blocked.insert(base(name));

    TransactionRequest request;
    request.action=TransactionAction::install;
    for (const PackageRecord &p:updates) {
        const std::string id=p.package_name.empty()?p.id:p.package_name;
        if (blocked.find(base(id))==blocked.end())
            request.package_ids.push_back(id);
    }
    for (const std::string &name:release.additions)
        if (blocked.find(base(name))==blocked.end())
            request.package_ids.push_back(name);

    std::sort(request.package_ids.begin(),request.package_ids.end());
    request.package_ids.erase(
        std::unique(
            request.package_ids.begin(),
            request.package_ids.end()),
        request.package_ids.end());

    std::set<std::string> installed_names;
    for (const PackageRecord &p:installed)
        installed_names.insert(
            base(p.package_name.empty()?p.id:p.package_name));

    for (const std::string &name:release.removals) {
        if (installed_names.find(base(name))!=installed_names.end())
            request.remove_package_ids.push_back(name);
    }
    std::sort(
        request.remove_package_ids.begin(),
        request.remove_package_ids.end());
    request.remove_package_ids.erase(
        std::unique(
            request.remove_package_ids.begin(),
            request.remove_package_ids.end()),
        request.remove_package_ids.end());

    if (request.package_ids.empty() &&
        request.remove_package_ids.empty()) {
        error="The target release does not require any package changes.";
        return false;
    }

    /*
     * Resolve the release additions/upgrades and required removals in one
     * projected final state. A release plan shown to the user is therefore
     * exactly one dependency solution, not two independently valid plans
     * concatenated after the fact.
     */
    if (request.package_ids.empty()) {
        request.action=TransactionAction::remove;
        request.package_ids=
            std::move(request.remove_package_ids);
        request.remove_package_ids.clear();
    }

    const auto plan=core.plan(request,{},error);
    if (!plan.has_value()) return false;
    combined=*plan;

    return true;
}

bool run_command(std::vector<std::string> args,std::string &error)
{
    std::vector<gchar*> argv;
    for (std::string &arg:args) argv.push_back(arg.data());
    argv.push_back(nullptr);
    gchar *out=nullptr,*err=nullptr;
    gint status=0;
    GError *gerror=nullptr;
    const gboolean spawned=g_spawn_sync(nullptr,argv.data(),nullptr,G_SPAWN_SEARCH_PATH,
        nullptr,nullptr,&out,&err,&status,&gerror);
    bool ok=spawned!=FALSE;
    if (ok) ok=g_spawn_check_wait_status(status,&gerror)!=FALSE;
    if (out!=nullptr && *out!='\0') std::cout<<out;
    if (!ok) {
        if (err!=nullptr && *err!='\0') error=err;
        else if (gerror!=nullptr && gerror->message!=nullptr) error=gerror->message;
        else error="Release-upgrade command failed.";
    }
    g_free(out); g_free(err); g_clear_error(&gerror);
    return ok;
}

struct SourcePublication {
    std::filesystem::path destination;
    std::filesystem::path destination_backup;
    bool destination_existed{false};
    std::filesystem::path obsolete;
    std::filesystem::path obsolete_backup;
    bool obsolete_existed{false};
};

bool rollback_target_sources(
    const SourcePublication &publication,
    std::string &error,
    bool refresh_metadata = false);

bool publish_target_sources(
    const ReleaseInfo &release,
    SourcePublication &publication,
    std::string &error)
{
    publication = SourcePublication{};
    publication.destination =
        "/etc/apt/sources.list.d/official-package-repositories.list";
    publication.obsolete =
        "/etc/apt/sources.list.d/official-source-repositories.list";
    publication.destination_backup =
        publication.destination.string() +
        ".infiltrator-" + release.current_codename + ".bak";
    publication.obsolete_backup =
        publication.obsolete.string() +
        ".infiltrator-" + release.current_codename + ".bak";

    std::error_code ec;
    std::filesystem::create_directories(
        publication.destination.parent_path(), ec);
    if (ec) {
        error =
            "Unable to create APT source directory: " +
            ec.message();
        return false;
    }

    publication.destination_existed =
        std::filesystem::exists(
            publication.destination, ec);
    if (ec) {
        error =
            "Unable to inspect current Mint repositories: " +
            ec.message();
        return false;
    }
    if (publication.destination_existed &&
        !durable_copy_file(
            publication.destination,
            publication.destination_backup,
            0644,
            error)) {
        error =
            "Unable to back up current Mint repositories: " +
            error;
        return false;
    }

    publication.obsolete_existed =
        std::filesystem::exists(
            publication.obsolete, ec);
    if (ec) {
        error =
            "Unable to inspect legacy Mint source repositories: " +
            ec.message();
        return false;
    }
    if (publication.obsolete_existed &&
        !durable_copy_file(
            publication.obsolete,
            publication.obsolete_backup,
            0644,
            error)) {
        error =
            "Unable to back up legacy Mint source repositories: " +
            error;
        return false;
    }

    if (!durable_copy_file(
            release.repositories,
            publication.destination,
            0644,
            error)) {
        error =
            "Unable to activate target Mint repositories: " +
            error;
        return false;
    }

    std::string remove_error;
    if (!durable_remove(
            publication.obsolete,
            remove_error)) {
        std::string rollback_error;
        if (!rollback_target_sources(
                publication,
                rollback_error)) {
            error =
                "Unable to retire legacy Mint source repositories: " +
                remove_error +
                "; rollback also failed: " +
                rollback_error;
        } else {
            error =
                "Unable to retire legacy Mint source repositories: " +
                remove_error +
                "; previous repository configuration was restored.";
        }
        return false;
    }
    return true;
}

bool rollback_target_sources(
    const SourcePublication &publication,
    std::string &error,
    const bool refresh_metadata)
{
    if (publication.destination_existed) {
        if (!durable_copy_file(
                publication.destination_backup,
                publication.destination,
                0644,
                error)) {
            error =
                "Unable to restore previous Mint repositories: " +
                error;
            return false;
        }
    } else if (!durable_remove(
                   publication.destination,
                   error)) {
        error =
            "Unable to remove target Mint repositories during rollback: " +
            error;
        return false;
    }

    if (publication.obsolete_existed) {
        if (!durable_copy_file(
                publication.obsolete_backup,
                publication.obsolete,
                0644,
                error)) {
            error =
                "Unable to restore legacy Mint source repositories: " +
                error;
            return false;
        }
    } else if (!durable_remove(
                   publication.obsolete,
                   error)) {
        error =
            "Unable to remove legacy target repositories during rollback: " +
            error;
        return false;
    }

    if (refresh_metadata) {
        std::string refresh_error;
        if (!run_command(
                {"apt-get", "update"},
                refresh_error)) {
            error =
                "Previous repository files were restored, but their APT metadata "
                "could not be refreshed: " + refresh_error;
            return false;
        }
    }
    return true;
}

int plan_command()
{
    ReleaseInfo release;
    std::string error;
    if (!load_release_info(release,error)) {
        std::cerr<<error<<"\n";
        return 1;
    }
    TransactionPlan plan;
    if (!build_plan(release,plan,error)) {
        std::cerr<<error<<"\n";
        return 1;
    }

    std::cout<<"TARGET\t"<<release.target_name<<"\t"<<release.target_codename<<"\n";
    for (const TransactionItem &item : plan.items) {
        std::cout
            <<"ITEM\t"
            <<transaction_action_name(item.action)
            <<"\t"<<item.package_id
            <<"\t"<<item.from_version
            <<"\t"<<item.to_version
            <<"\t"<<item.download_bytes
            <<"\t"<<(item.system_critical ? "1" : "0")
            <<"\n";
    }
    for (const std::string &spec:specs_for(plan)) std::cout<<"SPEC\t"<<spec<<"\n";
    std::cout<<"SUMMARY\t"<<plan.items.size()<<"\t"<<plan.download_bytes<<"\n";
    return 0;
}

int apply_inhibited_command(int argc,char **argv)
{
    if (geteuid()!=0) {
        std::cerr<<"Release upgrade apply must run as root.\n";
        return 1;
    }
    ReleaseInfo release;
    std::string error;
    if (!load_release_info(release,error)) { std::cerr<<error<<"\n"; return 1; }

    TransactionPlan plan;
    if (!build_plan(release,plan,error)) { std::cerr<<error<<"\n"; return 1; }

    std::vector<std::string> approved;
    for (int i=2;i<argc;++i) approved.emplace_back(argv[i]);
    std::sort(approved.begin(),approved.end());
    const auto current=specs_for(plan);
    if (approved!=current) {
        std::cerr<<"The target release package plan changed after review; nothing was modified.\n";
        return 2;
    }

    SourcePublication publication;
    if (!publish_target_sources(
            release,
            publication,
            error)) {
        std::cerr << error << "\n";
        return 1;
    }

    std::vector<std::string> command={
        "/usr/libexec/infiltrator-software-update-helper",
        "apply-plan"};
    command.insert(command.end(),approved.begin(),approved.end());
    if (!run_command(std::move(command),error)) {
        const std::string transaction_error = error;
        std::string rollback_error;
        if (!rollback_target_sources(
                publication,
                rollback_error,
                true)) {
            std::cerr
                << transaction_error
                << "\nRelease upgrade also failed to restore the previous "
                   "repository configuration: "
                << rollback_error
                << "\n";
        } else {
            std::cerr
                << transaction_error
                << "\nPrevious repository configuration was restored.\n";
        }
        return 1;
    }

    std::vector<std::string> finalization_errors;
    if (gchar *program = g_find_program_in_path("update-grub");
        program != nullptr) {
        g_free(program);
        std::string command_error;
        if (!run_command({"update-grub"}, command_error)) {
            finalization_errors.emplace_back(
                "update-grub failed: " + command_error);
        }
    }
    if (gchar *program =
            g_find_program_in_path("ubuntu-system-adjustments");
        program != nullptr) {
        g_free(program);
        std::string command_error;
        if (!run_command(
                {"ubuntu-system-adjustments", "adjust-grub-title"},
                command_error)) {
            finalization_errors.emplace_back(
                "ubuntu-system-adjustments failed: " +
                command_error);
        }
    }

    const auto after=read_assignments("/etc/linuxmint/info");
    const auto codename=after.find("CODENAME");
    if (codename==after.end() || codename->second!=release.target_codename) {
        std::cerr<<"Packages were upgraded, but the target Mint release identity was not yet confirmed. Reboot and recheck Software.\n";
        return 3;
    }
    if (!finalization_errors.empty()) {
        std::cerr
            << "Release packages were applied, but finalization was incomplete:\n";
        for (const std::string &detail : finalization_errors) {
            std::cerr << " - " << detail << "\n";
        }
        return 3;
    }
    std::cout<<"Release upgrade complete: "<<release.target_name<<" ("<<release.target_codename<<").\n";
    return 0;
}

int apply_command(int argc, char **argv)
{
    if (geteuid() != 0) {
        std::cerr << "Release upgrade apply must run as root.\n";
        return 1;
    }

    gchar *inhibit = g_find_program_in_path("systemd-inhibit");
    if (inhibit == nullptr) {
        std::cerr
            << "Release upgrade refused because systemd-inhibit is unavailable.\n";
        return 1;
    }
    g_free(inhibit);

    std::vector<std::string> command{
        "systemd-inhibit",
        "--what=shutdown:sleep",
        "--who=Infiltrator Software",
        "--why=Upgrading Linux Mint release",
        "--mode=block",
        "/usr/bin/infiltrator-software-release-upgrade",
        "apply-inhibited"};
    for (int index = 2; index < argc; ++index) {
        command.emplace_back(argv[index]);
    }

    std::string error;
    if (!run_command(std::move(command), error)) {
        std::cerr << error << "\n";
        return 1;
    }
    return 0;
}
}

int main(int argc,char **argv)
{
    if (argc==2 && std::string_view(argv[1])=="plan") return plan_command();
    if (argc>=3 && std::string_view(argv[1])=="apply") return apply_command(argc,argv);
    if (argc>=3 && std::string_view(argv[1])=="apply-inhibited")
        return apply_inhibited_command(argc,argv);
    std::cerr<<"Usage: infiltrator-software-release-upgrade plan | apply SPEC...\n";
    return 64;
}
