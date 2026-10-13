// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/debian_control.hpp"

#include <cassert>
#include <string>
#include <vector>

int main()
{
    using namespace infiltrator::software;

    std::vector<DebianControlFields> records;
    std::string error;
    const bool parsed = parse_debian_control_records(
        "PaCkAgE: alpha\r\n"
        "Description:\r\n"
        " continued text\r\n"
        "\tsecond line\r\n"
        "   \r\n"
        "Package: beta\n"
        "Version: 2\n",
        [&](const DebianControlFields &fields) {
            records.push_back(fields);
        },
        error);

    assert(parsed);
    assert(error.empty());
    assert(records.size() == 2U);
    assert(records[0].at("package") == "alpha");
    assert(records[0].at("description") == "continued text\nsecond line");
    assert(records[1].at("package") == "beta");
    assert(records[1].at("version") == "2");

    records.clear();
    assert(!parse_debian_control_records(
        "Package: one\nPACKAGE: two\n",
        [&](const DebianControlFields &fields) {
            records.push_back(fields);
        },
        error));
    assert(records.empty());
    assert(!error.empty());

    records.clear();
    assert(!parse_debian_control_records(
        " orphan continuation\n",
        [&](const DebianControlFields &fields) {
            records.push_back(fields);
        },
        error));
    assert(records.empty());
    assert(!error.empty());

    return 0;
}
