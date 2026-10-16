// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SOFTWARE_APP_PAGE_HPP
#define SOFTWARE_APP_PAGE_HPP

namespace infiltrator::software::app {

enum class AppPage {
    discover = 0,
    installed = 1,
    updates = 2,
    system = 3,
    repositories = 4,
    history = 5,
    repair = 6
};

} // namespace infiltrator::software::app

#endif
