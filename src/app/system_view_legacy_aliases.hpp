// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_SYSTEM_VIEW_LEGACY_ALIASES_HPP
#define INFILTRATOR_SOFTWARE_SYSTEM_VIEW_LEGACY_ALIASES_HPP

/*
 * Source-compatibility spellings for the still-unmigrated System view body.
 * This header is included only after WindowState and all owner declarations
 * are complete, under a source-local build marker. New code must use
 * state->system.<member> or SystemPageState directly and must not add aliases.
 */
#define system_list system.system_list
#define system_status system.system_status
#define system_count system.system_count
#define system_updates system.system_updates
#define system_refresh system.system_refresh
#define system_review_updates system.system_review_updates
#define system_records system.system_records
#define system_update_records system.system_update_records
#define system_generation system.system_generation
#define system_busy system.system_busy
#define system_loaded system.system_loaded

#endif
