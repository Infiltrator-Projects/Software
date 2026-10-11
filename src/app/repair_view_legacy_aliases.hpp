// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef INFILTRATOR_SOFTWARE_REPAIR_VIEW_LEGACY_ALIASES_HPP
#define INFILTRATOR_SOFTWARE_REPAIR_VIEW_LEGACY_ALIASES_HPP

/*
 * Source-compatibility spellings for the still-unmigrated Repair view body.
 * This header is included only after WindowState and all owner declarations
 * are complete, under a source-local build marker. New code must use
 * state->repair.<member> / state->discover.<member> directly and must not add
 * aliases.
 */
#define repair_list repair.repair_list
#define repair_status repair.repair_status
#define repair_engine repair.repair_engine
#define repair_sources repair.repair_sources
#define repair_issues repair.repair_issues
#define repair_health_banner repair.repair_health_banner
#define repair_health_icon repair.repair_health_icon
#define repair_health_title repair.repair_health_title
#define repair_health_copy repair.repair_health_copy
#define repair_recheck repair.repair_recheck
#define repair_rebuild repair.repair_rebuild
#define repair_configure repair.repair_configure
#define repair_generation repair.repair_generation
#define repair_busy repair.repair_busy
#define repair_interrupted repair.repair_interrupted
#define repair_loaded repair.repair_loaded

#ifndef discover_health_banner_state
#define discover_health_banner_state discover.discover_health_banner_state
#endif
#ifndef discover_health_summary
#define discover_health_summary discover.discover_health_summary
#endif

#endif
