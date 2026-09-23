// SPDX-License-Identifier: GPL-3.0-or-later
#include "helper/apt_plan_guard.hpp"

#include <cassert>
#include <string>

int main()
{
    using infiltrator::software::helper::validate_apt_simulation;

    std::string error;
    assert(validate_apt_simulation(
        {"app:amd64=2.0", "libcore:amd64=2.0"},
        "Inst app [1.0] (2.0 stable [amd64])\n"
        "Inst libcore [1.0] (2.0 stable [amd64])\n",
        error));
    assert(error.empty());

    assert(validate_apt_simulation(
        {"architecture-all=1.0"},
        "Inst architecture-all (1.0 stable [all])\n",
        error));

    assert(!validate_apt_simulation(
        {"app:amd64=2.0"},
        "Inst app [1.0] (2.0 stable [amd64])\n"
        "Inst surprise (1.0 stable [amd64])\n",
        error));
    assert(error.find("unapproved") != std::string::npos);

    assert(!validate_apt_simulation(
        {"app:amd64=2.0", "libcore:amd64=2.0"},
        "Inst app [1.0] (2.0 stable [amd64])\n",
        error));
    assert(error.find("libcore") != std::string::npos);

    assert(!validate_apt_simulation(
        {"app:amd64=2.0"},
        "Remv old [1.0]\n"
        "Inst app [1.0] (2.0 stable [amd64])\n",
        error));
    assert(error.find("remove") != std::string::npos);

    assert(validate_apt_simulation(
        {"remove:old=1.0"},
        "Remv old [1.0]\n",
        error));
    assert(error.empty());

    assert(validate_apt_simulation(
        {"remove:old=1.0", "app:amd64=2.0"},
        "Remv old [1.0]\n"
        "Inst app [1.0] (2.0 stable [amd64])\n",
        error));
    assert(error.empty());

    assert(!validate_apt_simulation(
        {"remove:old=1.0"},
        "Remv old [1.1]\n",
        error));

    assert(!validate_apt_simulation(
        {"app:arm64=2.0"},
        "Inst app [1.0] (2.0 stable [amd64])\n",
        error));

    assert(!validate_apt_simulation(
        {"app:amd64=2.0", "app:amd64=2.1"},
        "Inst app [1.0] (2.0 stable [amd64])\n",
        error));

    assert(!validate_apt_simulation(
        {"app:amd64=2.0"},
        "Reading package lists... Done\n",
        error));
    assert(error.find("stale") != std::string::npos);

    return 0;
}
