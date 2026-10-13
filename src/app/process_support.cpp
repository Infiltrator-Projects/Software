// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/process_support.hpp"

namespace infiltrator::software::app {

GSubprocess *start_subprocess(
    const std::vector<std::string> &arguments,
    const GSubprocessFlags flags,
    std::string &error)
{
    error.clear();
    if (arguments.empty()) {
        error = "Subprocess command is empty.";
        return nullptr;
    }

    std::vector<const gchar *> argv;
    argv.reserve(arguments.size() + 1U);
    for (const std::string &argument : arguments) {
        argv.push_back(argument.c_str());
    }
    argv.push_back(nullptr);

    GError *gerror = nullptr;
    GSubprocess *process =
        g_subprocess_newv(argv.data(), flags, &gerror);
    if (process == nullptr &&
        gerror != nullptr && gerror->message != nullptr) {
        error = gerror->message;
    }
    g_clear_error(&gerror);
    return process;
}

} // namespace infiltrator::software::app
