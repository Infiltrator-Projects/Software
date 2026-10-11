// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_PAGE_REGISTRY_HPP
#define INFILTRATOR_SOFTWARE_PAGE_REGISTRY_HPP

#include "app/app_page.hpp"

#include <gtk/gtk.h>

#include <array>
#include <cstddef>

namespace infiltrator::software::app {

struct WindowState;

struct PageDescriptor {
    AppPage page;
    const char *name;
    const char *label;
    const char *subtitle;
    const char *icon_name;
    GtkWidget *(*construct)(WindowState *state);
    void (*refresh_if_needed)(WindowState *state);
    void (*selection_changed)(WindowState *state, bool selected);
};

constexpr std::size_t kAppPageCount = 7U;

const std::array<PageDescriptor, kAppPageCount> &page_descriptors();
const PageDescriptor *page_descriptor(int index) noexcept;
const PageDescriptor *page_descriptor(AppPage page) noexcept;
void destroy_page_state(WindowState *state);

} // namespace infiltrator::software::app

#endif
