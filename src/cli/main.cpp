// SPDX-License-Identifier: GPL-3.0-or-later
#include "client/engine_client.hpp"
#include "core/update_policy.hpp"
#include "core/model.hpp"
#include "core/transaction_history.hpp"

#include <glib.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace infiltrator::software;

struct Options {
    bool security_only{false};
    bool kernel_only{false};
    bool dry_run{false};
    bool refresh_cache{false};
    bool assume_yes{false};
    bool install_recommends_requested{false};
    bool keep_configuration{false};
    bool replace_configuration{false};
    std::vector<std::string> ignores;
};

void usage()
{
    std::cout
        << "Usage:\n"
        << "  infiltrator-software-cli list [--security-only] [--kernel-only] [--ignore PATTERN]\n"
        << "  infiltrator-software-cli refresh\n"
        << "  infiltrator-software-cli upgrade [--security-only] [--kernel-only] [--dry-run]\n"
        << "      [--ignore PATTERN] [--refresh-cache] [--yes] [--keep-configuration|--replace-configuration]\n"
        << "      [--install-recommends]\n"
        << "  infiltrator-software-cli ignore list\n"
        << "  infiltrator-software-cli ignore add PATTERN\n"
        << "  infiltrator-software-cli ignore remove PATTERN\n";
}

bool parse_options(int argc, char **argv, int start, Options &options, std::string &error)
{
    for (int i=start; i<argc; ++i) {
        const std::string_view v(argv[i]);
        if (v=="--security-only" || v=="-s") options.security_only=true;
        else if (v=="--kernel-only" || v=="--only-kernel" || v=="-k") options.kernel_only=true;
        else if (v=="--dry-run" || v=="-d") options.dry_run=true;
        else if (v=="--refresh-cache" || v=="-r") options.refresh_cache=true;
        else if (v=="--yes" || v=="-y") options.assume_yes=true;
        else if (v=="--install-recommends") options.install_recommends_requested=true;
        else if (v=="--keep-configuration") options.keep_configuration=true;
        else if (v=="--replace-configuration") options.replace_configuration=true;
        else if (v=="--ignore" || v=="-i") {
            if (i+1>=argc) { error="--ignore requires a pattern."; return false; }
            std::string values(argv[++i]);
            std::size_t cursor=0U;
            for (;;) {
                const std::size_t comma=values.find(',',cursor);
                const std::string rule=values.substr(
                    cursor,
                    comma==std::string::npos
                        ? std::string::npos
                        : comma-cursor);
                if (!rule.empty()) options.ignores.push_back(rule);
                if (comma==std::string::npos) break;
                cursor=comma+1U;
            }
        } else { error="Unknown option: "+std::string(v); return false; }
    }
    if (options.keep_configuration && options.replace_configuration) {
        error="Configuration-file policies are mutually exclusive.";
        return false;
    }
    return true;
}

void append_system_ignores(
    SoftwarePreferences &preferences)
{
    std::ifstream input(
        "/etc/infiltrator-software/automatic-updates.conf");
    std::string line;
    while (std::getline(input, line)) {
        static constexpr std::string_view prefix{
            "ignore="};
        if (line.rfind(
                prefix.data(), 0U) == 0U &&
            line.size() > prefix.size()) {
            const std::string rule =
                line.substr(prefix.size());
            if (std::find(
                    preferences.ignored_packages.begin(),
                    preferences.ignored_packages.end(),
                    rule) ==
                preferences.ignored_packages.end()) {
                preferences.ignored_packages.push_back(
                    rule);
            }
        }
    }
}

bool matches(const PackageRecord &p, const Options &o, const SoftwarePreferences &prefs)
{
    if (o.security_only && !p.security_update) return false;
    if (o.kernel_only && p.kind != PackageKind::kernel) return false;
    return !update_is_ignored(p, prefs);
}

std::vector<std::string> exact_specs(const TransactionPlan &plan)
{
    std::vector<std::string> result;
    result.reserve(plan.items.size());
    for (const TransactionItem &item : plan.items) {
        if (item.action==TransactionAction::remove)
            result.push_back("remove:"+item.package_id+"="+item.from_version);
        else
            result.push_back(item.package_id+"="+item.to_version);
    }
    return result;
}

void record_cli_history(
    const TransactionPlan &plan,
    const bool success,
    const std::string_view message)
{
    const std::string path =
        user_transaction_history_path();
    if (path.empty() || plan.items.empty()) {
        return;
    }

    TransactionHistoryStore store(path);
    std::string history_error;
    if (!store.append(
            plan,
            success,
            message,
            history_error)) {
        std::cerr
            << "Warning: unable to record transaction history: "
            << history_error << "\n";
    }
}

bool execute_plan(const TransactionPlan &plan, const Options &o, std::string &error)
{
    std::vector<std::string> args{
        "pkexec",
        "/usr/libexec/infiltrator-software-update-helper",
        "apply-plan"
    };
    if (o.keep_configuration) args.emplace_back("--force-confold");
    if (o.replace_configuration) args.emplace_back("--force-confnew");
    const auto specs=exact_specs(plan);
    args.insert(args.end(), specs.begin(), specs.end());

    std::vector<gchar*> argv;
    argv.reserve(args.size()+1U);
    for (std::string &arg: args) argv.push_back(arg.data());
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
        else error="Update transaction failed.";
    }
    g_free(out); g_free(err); g_clear_error(&gerror);
    return ok;
}

int ignore_command(int argc,char **argv)
{
    if (argc<3) { usage(); return 64; }
    SoftwarePreferences prefs;
    std::string error;
    if (!load_software_preferences(prefs,error)) { std::cerr<<error<<"\n"; return 1; }
    const std::string action(argv[2]);
    if (action=="list") {
        for (const std::string &rule:prefs.ignored_packages) std::cout<<rule<<"\n";
        return 0;
    }
    if (argc!=4 || (action!="add" && action!="remove")) { usage(); return 64; }
    const std::string rule(argv[3]);
    if (action=="add") {
        if (std::find(prefs.ignored_packages.begin(),prefs.ignored_packages.end(),rule)==prefs.ignored_packages.end())
            prefs.ignored_packages.push_back(rule);
    } else {
        prefs.ignored_packages.erase(std::remove(prefs.ignored_packages.begin(),prefs.ignored_packages.end(),rule),prefs.ignored_packages.end());
    }
    if (!save_software_preferences(prefs,error)) { std::cerr<<error<<"\n"; return 1; }
    return 0;
}
}

int main(int argc,char **argv)
{
    using namespace infiltrator::software;
    if (argc<2) { usage(); return 64; }
    if (std::string_view(argv[1])=="--version" ||
        std::string_view(argv[1])=="-v") {
        std::cout << INFILTRATOR_SOFTWARE_VERSION << "\n";
        return 0;
    }
    if (std::string_view(argv[1])=="--help" ||
        std::string_view(argv[1])=="-h") {
        usage();
        return 0;
    }
    const std::string command(argv[1]);
    if (command=="ignore") return ignore_command(argc,argv);

    Options options;
    std::string error;
    if (!parse_options(argc,argv,2,options,error)) { std::cerr<<error<<"\n"; usage(); return 64; }

    EngineClient engine;
    if (command=="refresh") {
        if (argc!=2) { usage(); return 64; }
        if (!engine.refresh(error)) { std::cerr<<error<<"\n"; return 1; }
        std::vector<PackageRecord> updates;
        if (!engine.list_updates(updates,error)) { std::cerr<<error<<"\n"; return 1; }
        std::cout<<"Repository metadata refreshed; "<<updates.size()<<" update(s) available.\n";
        return 0;
    }
    if (command!="list" && command!="upgrade") { usage(); return 64; }

    SoftwarePreferences prefs;
    if (!load_software_preferences(prefs,error)) { std::cerr<<error<<"\n"; return 1; }
    append_system_ignores(prefs);
    for (const std::string &rule:options.ignores) prefs.ignored_packages.push_back(rule);

    if ((command=="upgrade" || options.refresh_cache) &&
        !engine.refresh(error)) { std::cerr<<error<<"\n"; return 1; }
    std::vector<PackageRecord> updates;
    if (!engine.list_updates(updates,error)) { std::cerr<<error<<"\n"; return 1; }

    std::vector<PackageRecord> chosen;
    for (const PackageRecord &p:updates) if (matches(p,options,prefs)) chosen.push_back(p);

    if (command=="list") {
        for (const PackageRecord &p:chosen) {
            std::cout<<p.package_name<<"\t"<<p.installed_version<<"\t"<<p.available_version<<"\t"
                     <<(p.security_update?"security":std::string(package_kind_name(p.kind)))<<"\t"
                     <<p.repository_origin<<"\n";
        }
        return 0;
    }
    if (chosen.empty()) { std::cout<<"No matching updates are available.\n"; return 0; }

    TransactionRequest request;
    request.action=TransactionAction::upgrade;
    request.install_recommends =
        options.install_recommends_requested;
    for (const PackageRecord &p:chosen) request.package_ids.push_back(p.package_name.empty()?p.id:p.package_name);
    const auto plan=engine.plan(request,error);
    if (!plan.has_value()) { std::cerr<<error<<"\n"; return 1; }

    std::cout<<"Planned package changes: "<<plan->items.size()
             <<"\nPlanned download bytes: "<<plan->download_bytes<<"\n";
    for (const TransactionItem &item:plan->items)
        std::cout<<transaction_action_name(item.action)<<"\t"<<item.package_id<<"\t"
                 <<item.from_version<<" -> "<<item.to_version<<"\n";

    if (options.dry_run) return 0;

    if (!options.assume_yes) {
        std::cout << "Proceed with this exact transaction? [y/N] " << std::flush;
        std::string answer;
        std::getline(std::cin,answer);
        if (answer!="y" && answer!="Y" &&
            answer!="yes" && answer!="YES") {
            std::cout << "Cancelled.\n";
            return 0;
        }
    } else if (!options.keep_configuration) {
        options.replace_configuration=true;
    }

    if (!execute_plan(*plan,options,error)) {
        record_cli_history(
            *plan,
            false,
            error.empty()
                ? "CLI update transaction failed."
                : error);
        std::cerr<<error<<"\n";
        return 1;
    }
    record_cli_history(
        *plan,
        true,
        "CLI update transaction completed successfully.");
    std::cout<<"Update transaction completed.\n";
    return 0;
}
