// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/engine_service_core.hpp"
#include "engine/debian_candidate.hpp"
#include "release/release_metadata.hpp"

#include <glib.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
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
    const std::string uid=std::to_string(static_cast<unsigned long long>(getuid()));
    const std::filesystem::path root=
        std::filesystem::temp_directory_path()/("infiltrator-software-release-"+uid);
    std::error_code ec;
    std::filesystem::create_directories(root,ec);
    if (ec) { error="Unable to create release-upgrade planning area: "+ec.message(); return false; }

    const std::string db=(root/"packages.db").string();
    const std::string cache=(root/"repositories").string();
    (void)setenv("INFILTRATOR_SOFTWARE_STATE_DB",db.c_str(),1);
    (void)setenv("INFILTRATOR_SOFTWARE_REPOSITORY_CACHE",cache.c_str(),1);
    (void)setenv("INFILTRATOR_SOFTWARE_SOURCES_FILE",release.repositories.c_str(),1);

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

    TransactionRequest install_request;
    install_request.action=TransactionAction::install;
    for (const PackageRecord &p:updates) {
        const std::string id=p.package_name.empty()?p.id:p.package_name;
        if (blocked.find(base(id))==blocked.end())
            install_request.package_ids.push_back(id);
    }
    for (const std::string &name:release.additions)
        if (blocked.find(base(name))==blocked.end())
            install_request.package_ids.push_back(name);

    std::sort(install_request.package_ids.begin(),install_request.package_ids.end());
    install_request.package_ids.erase(std::unique(install_request.package_ids.begin(),install_request.package_ids.end()),install_request.package_ids.end());

    combined=TransactionPlan{};
    const auto status=core.status();
    combined.state_generation=status.generation;
    combined.source_fingerprint=status.source_fingerprint;

    if (!install_request.package_ids.empty()) {
        auto plan=core.plan(install_request,{},error);
        if (!plan.has_value()) return false;
        combined=*plan;
    }

    std::set<std::string> installed_names;
    for (const PackageRecord &p:installed)
        installed_names.insert(base(p.package_name.empty()?p.id:p.package_name));

    TransactionRequest remove_request;
    remove_request.action=TransactionAction::remove;
    for (const std::string &name:release.removals)
        if (installed_names.find(base(name))!=installed_names.end())
            remove_request.package_ids.push_back(name);

    if (!remove_request.package_ids.empty()) {
        auto removal=core.plan(remove_request,{},error);
        if (!removal.has_value()) return false;
        combined.items.insert(combined.items.end(),removal->items.begin(),removal->items.end());
        combined.disk_delta_bytes+=removal->disk_delta_bytes;
        combined.touches_system=combined.touches_system||removal->touches_system;
    }

    if (combined.items.empty()) {
        error="The target release does not require any package changes.";
        return false;
    }
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
    if (publication.destination_existed) {
        std::filesystem::copy_file(
            publication.destination,
            publication.destination_backup,
            std::filesystem::copy_options::overwrite_existing,
            ec);
        if (ec) {
            error =
                "Unable to back up current Mint repositories: " +
                ec.message();
            return false;
        }
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
    if (publication.obsolete_existed) {
        std::filesystem::copy_file(
            publication.obsolete,
            publication.obsolete_backup,
            std::filesystem::copy_options::overwrite_existing,
            ec);
        if (ec) {
            error =
                "Unable to back up legacy Mint source repositories: " +
                ec.message();
            return false;
        }
    }

    const auto temporary =
        publication.destination.string() +
        ".infiltrator-new";
    std::filesystem::copy_file(
        release.repositories,
        temporary,
        std::filesystem::copy_options::overwrite_existing,
        ec);
    if (ec) {
        error =
            "Unable to stage target Mint repositories: " +
            ec.message();
        return false;
    }
    if (chmod(temporary.c_str(), 0644) != 0) {
        std::filesystem::remove(temporary, ec);
        error =
            "Unable to set permissions on staged target repositories.";
        return false;
    }
    std::filesystem::rename(
        temporary,
        publication.destination,
        ec);
    if (ec) {
        std::filesystem::remove(temporary);
        error =
            "Unable to activate target Mint repositories: " +
            ec.message();
        return false;
    }

    std::filesystem::remove(
        publication.obsolete, ec);
    if (ec) {
        error =
            "Unable to retire legacy Mint source repositories: " +
            ec.message();
        return false;
    }
    return true;
}

bool rollback_target_sources(
    const SourcePublication &publication,
    std::string &error)
{
    std::error_code ec;
    if (publication.destination_existed) {
        std::filesystem::copy_file(
            publication.destination_backup,
            publication.destination,
            std::filesystem::copy_options::overwrite_existing,
            ec);
    } else {
        std::filesystem::remove(
            publication.destination, ec);
    }
    if (ec) {
        error =
            "Unable to restore previous Mint repositories: " +
            ec.message();
        return false;
    }

    if (publication.obsolete_existed) {
        std::filesystem::copy_file(
            publication.obsolete_backup,
            publication.obsolete,
            std::filesystem::copy_options::overwrite_existing,
            ec);
    } else {
        std::filesystem::remove(
            publication.obsolete, ec);
    }
    if (ec) {
        error =
            "Unable to restore legacy Mint source repositories: " +
            ec.message();
        return false;
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

int apply_command(int argc,char **argv)
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

    gchar *inhibit=g_find_program_in_path("systemd-inhibit");
    if (inhibit==nullptr) {
        std::cerr<<"Release upgrade refused because systemd-inhibit is unavailable.\n";
        return 1;
    }
    g_free(inhibit);
    SourcePublication publication;
    if (!publish_target_sources(
            release,
            publication,
            error)) {
        std::cerr << error << "\n";
        return 1;
    }

    std::vector<std::string> command={"systemd-inhibit","--what=shutdown:sleep",
        "--who=Infiltrator Software","--why=Upgrading Linux Mint release","--mode=block",
        "/usr/libexec/infiltrator-software-update-helper","apply-plan"};
    command.insert(command.end(),approved.begin(),approved.end());
    if (!run_command(std::move(command),error)) {
        const std::string transaction_error = error;
        std::string rollback_error;
        if (!rollback_target_sources(
                publication,
                rollback_error)) {
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

    if (g_find_program_in_path("update-grub")!=nullptr) {
        std::string ignored;
        (void)run_command({"update-grub"},ignored);
    }
    if (g_find_program_in_path("ubuntu-system-adjustments")!=nullptr) {
        std::string ignored;
        (void)run_command({"ubuntu-system-adjustments","adjust-grub-title"},ignored);
    }

    const auto after=read_assignments("/etc/linuxmint/info");
    const auto codename=after.find("CODENAME");
    if (codename==after.end() || codename->second!=release.target_codename) {
        std::cerr<<"Packages were upgraded, but the target Mint release identity was not yet confirmed. Reboot and recheck Software.\n";
        return 3;
    }
    std::cout<<"Release upgrade complete: "<<release.target_name<<" ("<<release.target_codename<<").\n";
    return 0;
}
}

int main(int argc,char **argv)
{
    if (argc==2 && std::string_view(argv[1])=="plan") return plan_command();
    if (argc>=3 && std::string_view(argv[1])=="apply") return apply_command(argc,argv);
    std::cerr<<"Usage: infiltrator-software-release-upgrade plan | apply SPEC...\n";
    return 64;
}
