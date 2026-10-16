// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SOFTWARE_PAGE_REGISTRY_HPP
#define SOFTWARE_PAGE_REGISTRY_HPP

#include "app/app_page.hpp"

#include <gtk/gtk.h>

#include <array>
#include <cstddef>

namespace infiltrator::software::app {

struct WindowState;

enum class AppStateChange {
    kernel,
    repository,
    repository_snapshot,
    history_snapshot,
    transaction_history,
    repair,
    updates_snapshot,
    catalogue_transaction,
    update_transaction
};

struct PageDescriptor {
    AppPage page;
    const char *name;
    const char *label;
    const char *subtitle;
    const char *icon_name;
    GtkWidget *(*construct)(WindowState *state);
    void (*refresh_if_needed)(WindowState *state);
    void (*selection_changed)(WindowState *state, bool selected);
    void (*invalidate)(WindowState *state, AppStateChange change);
};

constexpr std::size_t kAppPageCount = 7U;

const std::array<PageDescriptor, kAppPageCount> &page_descriptors();
const PageDescriptor *page_descriptor(int index) noexcept;
const PageDescriptor *page_descriptor(AppPage page) noexcept;
void destroy_page_state(WindowState *state);
void invalidate_pages(WindowState *state, AppStateChange change);

} // namespace infiltrator::software::app

#endif
