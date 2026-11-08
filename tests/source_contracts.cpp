// SPDX-License-Identifier: GPL-3.0-or-later
// C++17 version of the Software source architecture and authorization contracts.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>
#include <initializer_list>
namespace fs = std::filesystem;
struct Rule { std::string path; std::vector<std::string> required, forbidden; };
static void check(bool yes, const std::string& why) {
    if (!yes) throw std::runtime_error(why);
}
static std::string read(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    check(bool(f), "Cannot read " + p.string());
    return {std::istreambuf_iterator<char>(f), {}};
}
static bool has(const std::string& a, const std::string& b) { return a.find(b) != std::string::npos; }
static std::size_t occurrences(const std::string& a, const std::string& b) {
    std::size_t count = 0;
    for (std::size_t n = 0; (n = a.find(b,n)) != std::string::npos; n += b.size()) ++count;
    return count;
}
static bool regex_has(const std::string& src, const std::string& expr) {
    return std::regex_search(src, std::regex(expr));
}
static std::string regex_capture(const std::string& src, const std::string& expr) {
    std::smatch m;
    check(std::regex_search(src, m, std::regex(expr)) && m.size()>1, "Missing contract: " + expr);
    return m[1].str();
}
static bool includes(const std::string& src, const std::string& header) {
    return has(src, "#include \"" + header + "\"") ||
           has(src, "# include \"" + header + "\"");
}
static std::string concat_app() {
    std::string out;
    for (const auto& f: fs::directory_iterator("src/app")) {
        if (f.is_regular_file() && (f.path().extension()==".cpp" || f.path().extension()==".hpp"))
            out += read(f.path()) + "\n";
    }
    return out;
}
static std::vector<unsigned char> decode_base64(const std::string& value) {
    std::vector<unsigned char> output;
    unsigned bits = 0; int buffered = 0; bool padded = false;
    for (unsigned char c: value) {
        if (c == '"' || std::isspace(c)) continue;
        if (c == '=') { padded = true; continue; }
        check(!padded, "Base64 content follows padding");
        int x;
        if (c >= 'A' && c <= 'Z') x=c-'A';
        else if (c >= 'a' && c <= 'z') x=c-'a'+26;
        else if (c >= '0' && c <= '9') x=c-'0'+52;
        else if (c=='+') x=62;
        else if (c=='/') x=63;
        else throw std::runtime_error("Invalid authored base64 asset");
        bits=(bits<<6)|unsigned(x);
        buffered += 6;
        if (buffered>=8) { buffered -= 8; output.push_back((bits>>buffered)&255); }
    }
    check(buffered < 6 && (!buffered || !(bits & ((1u<<buffered)-1))), "Malformed base64 trailing bits");
    return output;
}
int main() {
  try {
    const std::vector<Rule> rules = {
        {"README.md", {"[`VERSION`](VERSION)"}, {}},
        {"CMakeLists.txt", {"src/engine/debian_control.cpp","INFILTRATR_COMMON_EXPECTED_VERSION","INFILTRATR_COMMON_EXPECTED_COMMIT","InfiltratrCommon::Common","SOFTWARE_ENGINE_INTERFACE_XML","engine_introspection.hpp.in","add_library(software-source-format STATIC","add_library(software-source-inventory STATIC","add_library(software-source-mutation STATIC"}, {"add_library(software-apt STATIC","software-sources"}},
        {"src/core/model.hpp", {"struct KernelRecord","struct SourceRecord"}, {}},
        {"src/app/installed_inventory.cpp", {"EngineClient"}, {"DebianInstalledState::read","\"engine/debian_installed_state.hpp\""}},
        {"src/app/installed_controller.cpp", {"destroy_installed_controller"}, {"from_engine","\"Debian state\"","\"Debian + Flatpak\""}},
        {"src/app/repository_controller.hpp", {}, {"\"sources/source_inventory.hpp\""}},
        {"src/client/engine_client.hpp", {}, {"\"engine/kernel_inventory.hpp\""}},
        {"src/app/process_support.cpp", {"g_subprocess_newv("}, {}},
        {"src/engine/engine_service_core.cpp", {"record_refresh_failure(refresh_error)","refresh_error_.empty()"}, {}},
        {"src/service/main.cpp", {"record_refresh_failure(error)","record_refresh_success()","\"PlanMixedTransaction\"","kEngineIntrospectionXml"}, {"R\"XML("}},
        {"src/client/engine_client.cpp", {"engine_version_is_compatible_with_client","ensure_engine_identity","GetConnectionUnixUser","GetConnectionUnixProcessID","kRequiredApiVersion"}, {}},
        {"src/service/engine_introspection.hpp.in", {"@SOFTWARE_ENGINE_INTERFACE_XML@"}, {}},
        {"data/net.ssmith.software.Engine.xml", {"name=\"PlanMixedTransaction\"","<method name=\"Quit\"/>"}, {}},
        {"src/engine/debian_transaction.cpp", {"plan_removal","Refusing to remove Essential/Protected package","protected_package","held","DebianDependencyResolver::resolve"}, {"retained_package_matches","retained_package_provides","retained_group_satisfied"}},
        {"src/engine/debian_dependency.cpp", {}, {}},
        {"src/engine/debian_candidate.cpp", {"candidate.pin_priority"}, {}},
        {"src/engine/package_policy.cpp", {"DebianPolicyStack"}, {}},
        {"src/engine/debian_phased_updates.cpp", {"std::minstd_rand","Always-Include-Phased-Updates","Never-Include-Phased-Updates"}, {}},
        {"src/engine/debian_preferences.cpp", {"/etc/apt/preferences.d"}, {}},
        {"src/engine/debian_reconcile.cpp", {"\"/var/lib/dpkg/arch\"","phased_updates","host_preferences"}, {"g_spawn_","g_subprocess_","std::system(","popen(","fork(","posix_spawn","execv"}},
        {"src/engine/debian_repository.cpp", {"cached_release_current","verify_payload","read_cached"}, {}},
        {"src/engine/debian_source_configuration.cpp", {"parse_apt_deb822_stanzas"}, {}},
        {"src/engine/debian_control.cpp", {"parse_debian_control_records"}, {}},
        {"src/engine/debian_installed_state.cpp", {"parse_debian_control_records"}, {}},
        {"src/engine/debian_package_index.cpp", {"parse_debian_control_records","phased-update-percentage"}, {}},
        {"src/engine/package_state_store.cpp", {"pin_priority INTEGER NOT NULL","policy_provider TEXT NOT NULL","release_origin TEXT NOT NULL"}, {}},
        {"src/core/exact_transaction_spec.cpp", {"\"x3|\"","source_fingerprint"}, {}},
        {"src/sources/source_inventory.cpp", {"parse_apt_deb822_stanzas"}, {"next_stanza_boundary"}},
        {"src/sources/source_mutation.cpp", {"parse_apt_deb822_stanzas","set_apt_list_entry_enabled","set_apt_deb822_entry_enabled"}, {"next_stanza_boundary"}},
        {"src/sources/apt_source_format.cpp", {"parse_apt_deb822_stanzas"}, {}},
        {"src/helper/source_helper.cpp", {"\"set-apt-source-enabled\"","safe_existing_apt_source"}, {}},
        {"src/helper/update_helper.cpp", {"verify_reviewed_repository_state","verify_downloaded_artifacts","validate_apt_simulation","approved_spec_already_satisfied","\"--download-only\"","\"--no-download\"","\"--no-remove\"","\"--purge-removals\"","kSystemSoftwarePreferencesPath"}, {}},
        {"src/helper/apt_plan_guard.cpp", {"parse_remove_line","introduced an unapproved change"}, {}},
        {"src/external/external_updates.cpp", {"discover_flatpak_updates","apply_flatpak_updates_selected"}, {"\"cinnamon-spice-updater\""}},
        {"src/external/cinnamon_spices.cpp", {"discover_native_cinnamon_updates","apply_native_cinnamon_updates_selected","extract_zip_safely","RENAME_EXCHANGE","CURLOPT_PROTOCOLS_STR","CURLOPT_REDIR_PROTOCOLS_STR"}, {}},
        {"src/catalogue/system_catalogue.cpp", {"std::string(\"flatpak:\")","installed_flatpaks"}, {}},
        {"src/catalogue/repository_catalogue.cpp", {"infiltratr_atomic_file_write_bytes"}, {}},
        {"src/app/kernel_manager.cpp", {"user_transaction_history_path()"}, {"history.sqlite3"}},
        {"src/automation/main.cpp", {"system_transaction_history_path","system_on_battery","kSystemSoftwarePreferencesPath"}, {}},
        {"src/automation/maintenance.cpp", {"system_transaction_history_path","\"--purge-removals\"","\"apt-get\", \"-s\", \"autoremove\", \"--purge\"","\"LC_ALL\", \"C\"","\"/usr/libexec/software-update-helper\"","kSystemSoftwarePreferencesPath"}, {}},
        {"src/session/main.cpp", {"system_on_battery"}, {}},
        {"src/release/main.cpp", {"kReleaseJournal","\"packages-applying\"","\"packages-applied\"","recover_pending_release","rollback_target_sources","load_release_info","build_plan"}, {}},
        {"src/tray/main.cpp", {"refresh_installed"}, {}},
        {"src/cli/main.cpp", {"\"--security-only\"","\"--kernel-only\"","\"--dry-run\"","\"--refresh-cache\"","\"--force-confold\"","\"--force-confnew\"","\"--install-recommends\"","\"ignore\"","kSystemSoftwarePreferencesPath"}, {}},
        {"debian/control", {}, {"Provides: mintupdate","Conflicts: mintupdate","Replaces: mintupdate"}},
        {"data/net.ssmith.software.updates.policy", {"Authentication is required to install, update or remove system software."}, {}},
        {"data/net.ssmith.software.policy", {}, {}},
    };
    for (const auto& rule: rules) {
        const auto src=read(rule.path);
        for (const auto& token: rule.required)
            check(has(src,token), rule.path + " missing " + token);
        for (const auto& token: rule.forbidden)
            check(!has(src,token), rule.path + " contains retired " + token);
    }
    const auto app=concat_app();
    const auto cmake=read("CMakeLists.txt");
    const auto workflow=read(".github/workflows/ci.yml");
    const auto readme=read("README.md");
    const auto version=read("VERSION");
    const auto changelog=read("debian/changelog");
    const auto meta=read("debian/software.metainfo.xml");
    std::string plain_version=version;
    plain_version.erase(std::remove_if(plain_version.begin(),plain_version.end(),
        [](unsigned char c){return std::isspace(c);}),plain_version.end());
    check(changelog.rfind("software ("+plain_version+") ",0)==0,"Changelog/version mismatch");
    check(regex_capture(meta, R"rx(<release\s+[^>]*version="([^"]+)")rx")==plain_version,
          "Metainfo/version mismatch");
    check(has(workflow,"cancel-in-progress: true") && has(workflow,"persist-credentials: false") &&
          has(workflow,"dist/release") && has(workflow,"group: verify-"),
          "Missing single-publisher CI release discipline");
    check(occurrences(workflow,"dpkg-buildpackage -us -uc -b")==1 &&
          occurrences(workflow,"publish-github")==1, "CI is building/publishing more than once");
    check(!has(workflow,"actions/upload-artifact@") && !has(workflow,"actions/download-artifact@") &&
          !has(workflow,"\n  publish:") &&
          workflow.find("complete-bundle")<workflow.find("publish-github"),
          "Release artifacts are rebuilt or relayed");
    check(!has(changelog,"Warning: truncated output") && !has(changelog,"tokens truncated") &&
          !has(changelog,"Total output lines:"), "Corrupt changelog");
    check(!has(app,"AptBackend") && !has(app,"from_engine") &&
          !regex_has(app,R"rx((^|\n)\s*#\s*include\s+"engine/)rx"),
          "GUI imported engine implementation");
    const auto model=read("src/core/model.hpp");
    check(has(model,"struct KernelRecord") && has(model,"struct SourceRecord"),"Domain model missing");
    const auto coordinator=read("src/app/app_coordinator.cpp");
    for(const auto* h: {"app/discover_view.hpp","app/updates_view.hpp","app/system_view.hpp",
                       "app/repair_view.hpp","app/repository_view.hpp",
                       "app/installed_controller.hpp","app/history_controller.hpp"})
        check(!includes(coordinator,h),std::string("Coordinator imports page ")+h);
    for(const auto* h: {"app/page_registry.hpp","app/view_intents.hpp","app/window_state.hpp"})
        check(includes(coordinator,h),std::string("Coordinator missing ")+h);
    const auto main_app=read("src/app/main.cpp");
    for(const auto* h: {"app/discover_view.hpp","app/updates_view.hpp","app/updates_controller.hpp"})
        check(!includes(main_app,h),std::string("Main imports page ")+h);
    check(includes(read("src/app/discover_view.cpp"),"app/page_support.hpp") &&
          includes(read("src/app/updates_view.cpp"),"app/page_support.hpp"),
          "Page support boundary bypassed");
    for (const auto& entry: fs::directory_iterator("src/app")) {
        if(!entry.is_regular_file() || entry.path().extension()!=".cpp" ||
            entry.path().filename()=="process_support.cpp") continue;
        const auto src=read(entry.path());
        for(const auto* api: {"g_subprocess_new(","g_subprocess_newv(",
                              "g_subprocess_launcher_new(","g_spawn_"})
            check(!has(src,api),entry.path().string()+" bypasses process support");
    }
    const auto links=[&](const std::string& target) {
        return regex_capture(cmake,"target_link_libraries\\s*\\(\\s*"+target+"\\s+([^)]*)\\)");
    };
    check(has(links("software"),"software-source-inventory") &&
          !has(links("software"),"software-source-mutation") &&
          has(links("software-helper"),"software-source-mutation"),
          "Read-only and privileged mutation must not share link authority");
    check(regex_has(cmake,R"rx(target_link_libraries\s*\(\s*software-engine-core\s+PRIVATE\s+software-source-format\s*\))rx"),
          "Debian parser link ownership changed");
    for(const auto* path: {"src/sources/source_inventory.cpp","src/sources/source_mutation.cpp",
                            "src/engine/debian_source_configuration.cpp"})
        check(includes(read(path),"sources/apt_source_format.hpp"),
              std::string(path)+" bypasses deb822 parser");
    const auto dependency=read("src/engine/debian_dependency.cpp");
    const auto transaction=read("src/engine/debian_transaction.cpp");
    for(const auto* token: {"install_recommends","planned_removals","available_candidates",
                             "conflict_resolution_candidates","installed_identity_held",
                             "candidate_architecture_matches","installed_architecture_matches",
                             "remove_installed"})
        check(has(dependency,token)||has(transaction,token),std::string("Missing transaction policy ")+token);
    const auto cinnamon=read("src/external/cinnamon_spices.cpp");
    check(occurrences(cinnamon,"if (!configure_https_only(curl))") ==
          occurrences(cinnamon,"curl_easy_perform(curl)"),"Cinnamon libcurl HTTPS guard bypassed");
    const auto engine=read("src/service/main.cpp"), client=read("src/client/engine_client.cpp");
    check(regex_capture(engine,R"rx(kApiVersion\s*=\s*([0-9]+)U)rx") ==
          regex_capture(client,R"rx(kRequiredApiVersion\s*=\s*([0-9]+)U)rx"),
          "D-Bus client and engine API versions differ");
    const auto atomic=read("src/catalogue/repository_catalogue.cpp");
    check(regex_has(atomic,
          R"rx(const int ([A-Za-z_][A-Za-z_0-9]*)\s*=\s*infiltratr_atomic_file_write_bytes\([\s\S]*?\);\s*if\s*\(\s*\1\s*!=\s*0\s*\))rx"),
          "Common POSIX writer error convention was reversed");
    for(const auto* policy: {"data/net.ssmith.software.updates.policy",
                             "data/net.ssmith.software.policy"}) {
        const auto defaults=regex_capture(read(policy),R"rx(<defaults>([\s\S]*?)</defaults>)rx");
        for(const auto* field: {"allow_any","allow_inactive","allow_active"})
            check(regex_has(defaults,std::string("<")+field+">\\s*auth_admin\\s*</"+field+">"),
                  std::string(policy)+" grants insecure Polkit defaults");
    }
    const auto parity=read("docs/MINTUPDATE_PARITY.md");
    check(!has(parity,"Removal approved after host validation"),"Unaudited Mint update parity claim");
    int required_rows=0; std::size_t start=0;
    while(start<parity.size()) {
        auto stop=parity.find('\n',start);
        auto row=parity.substr(start,stop==std::string::npos?stop:stop-start);
        while(!row.empty() && std::isspace((unsigned char)row.back())) row.pop_back();
        if(row.rfind("| ",0)==0 && row.size()>=12 &&
           row.compare(row.size()-12,12,"| Required |")==0) {
            ++required_rows;
            check(occurrences(row,"|")==4,"Malformed required parity row");
        }
        if(stop==std::string::npos) break;
        start=stop+1;
    }
    check(required_rows>0,"Mint Update parity lacks required rows");
    std::string source_content;
    for(const auto& entry:fs::recursive_directory_iterator("src")) {
        if(!entry.is_regular_file() ||
           (entry.path().extension()!=".cpp" && entry.path().extension()!=".hpp") ||
           has(entry.path().generic_string(),"src/infiltratr-common/")) continue;
        source_content+=read(entry.path());
    }
    check(occurrences(source_content,"\"/etc/software/automatic-updates.conf\"")==1,
          "Automatic update preferences have multiple owners");
    for(const auto* token: {"InstalledController","RepositoryController","create_installed_page",
                             "make_system_page","make_repair_page","create_history_controller_page",
                             "discover_install_clicked","create_snapshot_before_update"})
        check(has(app,token),std::string("Product capability removed: ")+token);
    std::string encoded;
    for(int i=1;i<=6;i++) {
        const std::string suffix=(i<10?"0":"")+std::to_string(i);
        encoded+=read("src/app/discover_hero_part"+suffix+".inc");
    }
    const auto hero=decode_base64(encoded);
    check(hero.size()>15000 && hero[0]==255 && hero[1]==216 &&
          hero[hero.size()-2]==255 && hero.back()==217,"Corrupt authored JPEG");
    const auto logo=decode_base64(read("src/app/titlebar_logo.inc"));
    const std::vector<unsigned char> signature{137,80,78,71,13,10,26,10};
    check(logo.size()>5000 && std::equal(signature.begin(),signature.end(),logo.begin()),
          "Corrupt authored PNG");
    // A native test must fail closed on future Python additions, not just
    // validate that today's installed executables omit Python dependencies.
    for(const auto* dir: {"src","tests","tools","debian","data",".github"}) {
        if(!fs::exists(dir)) continue;
        for(const auto& entry:fs::recursive_directory_iterator(dir)) {
            // Libraries is an independently versioned pinned Git submodule.
            // Software does not own its upstream development tooling.
            const auto path=entry.path().generic_string();
            if(path.rfind("src/infiltratr-common/",0)==0) continue;
            check(!entry.is_regular_file() || entry.path().extension()!=".py",
                  "Python source reintroduced: "+path);
        }
    }
    std::cout<<"PASS: native Software C++17 architecture, authorization and release contracts\n";
    return 0;
  } catch(const std::exception& error) {
    std::cerr<<"Software source contract failure: "<<error.what()<<'\n';
    return 1;
  }
}
