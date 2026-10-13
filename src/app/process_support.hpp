// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_PROCESS_SUPPORT_HPP
#define INFILTRATOR_SOFTWARE_PROCESS_SUPPORT_HPP

#include <gio/gio.h>

#include <string>
#include <vector>

namespace infiltrator::software::app {

/*
 * Start one argv-style subprocess without exposing GLib argv conversion and
 * GError lifetime handling to every view/controller that needs it. The caller
 * owns the returned GSubprocess reference and must unref it after scheduling
 * or completing communication.
 */
[[nodiscard]] GSubprocess *start_subprocess(
    const std::vector<std::string> &arguments,
    GSubprocessFlags flags,
    std::string &error);

} // namespace infiltrator::software::app

#endif
