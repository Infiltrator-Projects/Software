// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/exact_transaction_spec.hpp"

#include <cassert>
#include <string>
#include <vector>

int main()
{
    using namespace infiltrator::software;

    TransactionItem install;
    install.package_id = "example:amd64";
    install.action = TransactionAction::upgrade;
    install.from_version = "1.0";
    install.to_version = "2.0";
    install.architecture = "amd64";
    install.source = "/etc/apt/sources.list.d/example.sources:stanza-1:1";
    install.filename = "pool/main/e/example/example_2.0_amd64.deb";
    install.sha256 =
        "0123456789abcdef0123456789abcdef"
        "0123456789abcdef0123456789abcdef";

    TransactionItem removal;
    removal.package_id = "obsolete";
    removal.action = TransactionAction::remove;
    removal.from_version = "1.5";
    removal.architecture = "amd64";

    TransactionPlan plan;
    plan.items = {install, removal};

    std::vector<std::string> specs;
    std::string error;
    assert(exact_transaction_specs(plan, specs, error));
    assert(error.empty());
    assert(specs.size() == 2U);
    assert(specs[0].rfind("x2|I|", 0U) == 0U);
    assert(specs[1].rfind("x2|R|", 0U) == 0U);

    ExactTransactionSpec decoded;
    assert(decode_exact_transaction_spec(specs[0], decoded, error));
    assert(decoded.action == TransactionAction::install);
    assert(decoded.package_id == install.package_id);
    assert(decoded.version == install.to_version);
    assert(decoded.architecture == install.architecture);
    assert(decoded.source == install.source);
    assert(decoded.filename == install.filename);
    assert(decoded.sha256 == install.sha256);

    assert(decode_exact_transaction_spec(specs[1], decoded, error));
    assert(decoded.action == TransactionAction::remove);
    assert(decoded.package_id == removal.package_id);
    assert(decoded.version == removal.from_version);
    assert(decoded.source.empty());
    assert(decoded.filename.empty());
    assert(decoded.sha256.empty());

    TransactionItem incomplete = install;
    incomplete.sha256.clear();
    std::string rejected;
    assert(!encode_exact_transaction_spec(
        incomplete, rejected, error));
    assert(error.find("SHA-256") != std::string::npos);

    assert(!decode_exact_transaction_spec(
        "x2|I|pkg|1.0|amd64|source|file|bad",
        decoded,
        error));

    return 0;
}
