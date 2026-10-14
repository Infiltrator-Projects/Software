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
    if (process == nullptr) {
        error =
            gerror != nullptr && gerror->message != nullptr
                ? std::string(gerror->message)
                : "Unable to start subprocess.";
    }
    g_clear_error(&gerror);
    return process;
}

bool run_subprocess_sync(
    const std::vector<std::string> &arguments,
    SubprocessResult &result,
    std::string &error)
{
    result = {};
    GSubprocess *process = start_subprocess(
        arguments,
        static_cast<GSubprocessFlags>(
            G_SUBPROCESS_FLAGS_STDOUT_PIPE |
            G_SUBPROCESS_FLAGS_STDERR_PIPE),
        error);
    if (process == nullptr) {
        return false;
    }

    gchar *standard_output = nullptr;
    gchar *standard_error = nullptr;
    GError *gerror = nullptr;
    const gboolean communicated = g_subprocess_communicate_utf8(
        process, nullptr, nullptr, &standard_output, &standard_error, &gerror);
    if (!communicated) {
        error =
            gerror != nullptr && gerror->message != nullptr
                ? std::string(gerror->message)
                : "Unable to communicate with subprocess.";
        g_free(standard_output);
        g_free(standard_error);
        g_clear_error(&gerror);
        g_object_unref(process);
        return false;
    }

    if (standard_output != nullptr) {
        result.standard_output = standard_output;
    }
    if (standard_error != nullptr) {
        result.standard_error = standard_error;
    }
    result.successful = g_subprocess_get_successful(process) != FALSE;

    g_free(standard_output);
    g_free(standard_error);
    g_clear_error(&gerror);
    g_object_unref(process);
    error.clear();
    return true;
}

} // namespace infiltrator::software::app
