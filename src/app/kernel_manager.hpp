// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_KERNEL_MANAGER_HPP
#define INFILTRATOR_SOFTWARE_KERNEL_MANAGER_HPP

#include <gtk/gtk.h>

namespace infiltrator::software {

using KernelManagerChangedCallback = void (*)(gpointer user_data);

void present_kernel_manager(
    GtkWindow *parent,
    KernelManagerChangedCallback changed_callback,
    gpointer changed_user_data);

} // namespace infiltrator::software

#endif
