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

struct SubprocessResult {
    std::string standard_output;
    std::string standard_error;
    bool successful{false};
};

/*
 * Run one subprocess to completion while keeping construction, pipe and GError
 * ownership inside the GUI process boundary. The return value describes the
 * ability to start and communicate with the process; child exit success is
 * deliberately separate so each caller retains its product-specific policy.
 */
[[nodiscard]] bool run_subprocess_sync(
    const std::vector<std::string> &arguments,
    SubprocessResult &result,
    std::string &error);

} // namespace infiltrator::software::app

#endif
