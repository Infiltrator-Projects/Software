# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" SOFTWARE_VERSION)
string(STRIP "${SOFTWARE_VERSION}" SOFTWARE_VERSION)
if(NOT SOFTWARE_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must contain major.minor.patch")
endif()

project(Software VERSION "${SOFTWARE_VERSION}" LANGUAGES C CXX)
include(CTest)

set(SOFTWARE_BUILD_PROFILE "cmake" CACHE STRING
    "Common build-profile identity for this Software build")
set_property(CACHE SOFTWARE_BUILD_PROFILE PROPERTY STRINGS cmake generic native)
if(NOT SOFTWARE_BUILD_PROFILE MATCHES "^(cmake|generic|native)$")
    message(FATAL_ERROR "Unsupported SOFTWARE_BUILD_PROFILE")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(INFILTRATR_COMMON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/infiltratr-common")

if(NOT EXISTS "${INFILTRATR_COMMON_DIR}/VERSION")
    message(FATAL_ERROR
        "Common is missing. Clone recursively or run: git submodule update --init --recursive")
endif()

# The gitlink is the single authority for the Common revision. Duplicating its
# commit/version here made every Common update a multi-file synchronisation
# exercise and made release validation depend on CMake source spelling.
file(READ "${INFILTRATR_COMMON_DIR}/VERSION" INFILTRATR_COMMON_VERSION)
string(STRIP "${INFILTRATR_COMMON_VERSION}" INFILTRATR_COMMON_VERSION)
set(INFILTRATR_COMMON_EXPECTED_VERSION "${INFILTRATR_COMMON_VERSION}")
if(NOT INFILTRATR_COMMON_VERSION MATCHES "^[0-9]+\.[0-9]+\.[0-9]+$")
    message(FATAL_ERROR
        "Common VERSION must contain major.minor.patch; found ${INFILTRATR_COMMON_VERSION}")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    find_package(Git REQUIRED)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}"
                rev-parse "HEAD:src/infiltratr-common"
        RESULT_VARIABLE COMMON_GITLINK_RESULT
        OUTPUT_VARIABLE COMMON_GITLINK_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${INFILTRATR_COMMON_DIR}" rev-parse HEAD
        RESULT_VARIABLE COMMON_GIT_RESULT
        OUTPUT_VARIABLE COMMON_GIT_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(NOT COMMON_GITLINK_RESULT EQUAL 0 OR NOT COMMON_GIT_RESULT EQUAL 0)
        message(FATAL_ERROR "Unable to verify the Common submodule gitlink")
    endif()
    set(INFILTRATR_COMMON_EXPECTED_COMMIT "${COMMON_GITLINK_COMMIT}")
    if(NOT COMMON_GIT_COMMIT STREQUAL COMMON_GITLINK_COMMIT)
        message(FATAL_ERROR
            "Common checkout ${COMMON_GIT_COMMIT} does not match the superproject "
            "gitlink ${COMMON_GITLINK_COMMIT}")
    endif()
endif()

set(INFILTRATR_COMMON_BUILD_TESTS OFF CACHE BOOL
    "Do not build Common tests as part of Software" FORCE)
set(INFILTRATR_COMMON_WARNINGS_AS_ERRORS ON CACHE BOOL
    "Treat Common warnings as errors" FORCE)
add_subdirectory("${INFILTRATR_COMMON_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/infiltratr-common" EXCLUDE_FROM_ALL)
include(
    "${INFILTRATR_COMMON_DIR}/cmake/InfiltratrTypographyAssets.cmake")

# Apply the project warning contract once, after Common has been added so the
# consumer cannot accidentally alter Common's own compiler policy. New Software
# targets and tests inherit the same warnings without joining a fragile list.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
endif()

set(SOFTWARE_FONT_ARCHIVE_PATH "" CACHE FILEPATH
    "Optional pre-downloaded canonical MB Corpo archive for offline builds")
if(SOFTWARE_FONT_ARCHIVE_PATH)
    if(NOT EXISTS "${SOFTWARE_FONT_ARCHIVE_PATH}")
        message(FATAL_ERROR
            "SOFTWARE_FONT_ARCHIVE_PATH does not exist: "
            "${SOFTWARE_FONT_ARCHIVE_PATH}")
    endif()
    set(SOFTWARE_FONT_ARCHIVE "${SOFTWARE_FONT_ARCHIVE_PATH}")
    file(SHA256 "${SOFTWARE_FONT_ARCHIVE}" SOFTWARE_FONT_ARCHIVE_SHA256)
    if(NOT SOFTWARE_FONT_ARCHIVE_SHA256
       STREQUAL INFILTRATR_MB_CORPO_ARCHIVE_SHA256)
        message(FATAL_ERROR
            "SOFTWARE_FONT_ARCHIVE_PATH does not match the canonical archive hash")
    endif()
else()
    set(SOFTWARE_FONT_ARCHIVE
        "${CMAKE_CURRENT_BINARY_DIR}/software-mb-corpo-fonts.tar.xz")
    set(SOFTWARE_FONT_ARCHIVE_READY OFF)
    if(EXISTS "${SOFTWARE_FONT_ARCHIVE}")
        file(SHA256
            "${SOFTWARE_FONT_ARCHIVE}"
            SOFTWARE_FONT_ARCHIVE_SHA256)
        if(SOFTWARE_FONT_ARCHIVE_SHA256
           STREQUAL INFILTRATR_MB_CORPO_ARCHIVE_SHA256)
            set(SOFTWARE_FONT_ARCHIVE_READY ON)
        endif()
    endif()
    if(NOT SOFTWARE_FONT_ARCHIVE_READY)
        file(DOWNLOAD
            "${INFILTRATR_MB_CORPO_ARCHIVE_URL}"
            "${SOFTWARE_FONT_ARCHIVE}"
            EXPECTED_HASH "SHA256=${INFILTRATR_MB_CORPO_ARCHIVE_SHA256}"
            TLS_VERIFY ON
            STATUS SOFTWARE_FONT_DOWNLOAD_STATUS)
        list(GET SOFTWARE_FONT_DOWNLOAD_STATUS 0 SOFTWARE_FONT_DOWNLOAD_CODE)
        if(NOT SOFTWARE_FONT_DOWNLOAD_CODE EQUAL 0)
            list(GET SOFTWARE_FONT_DOWNLOAD_STATUS 1 SOFTWARE_FONT_DOWNLOAD_MESSAGE)
            message(FATAL_ERROR
                "Unable to obtain canonical Software font archive: "
                "${SOFTWARE_FONT_DOWNLOAD_MESSAGE}. For an offline build, set "
                "SOFTWARE_FONT_ARCHIVE_PATH to the verified canonical archive.")
        endif()
    endif()
endif()

set(SOFTWARE_FONT_DIR "${CMAKE_CURRENT_BINARY_DIR}/software-fonts")
file(REMOVE_RECURSE "${SOFTWARE_FONT_DIR}")
file(MAKE_DIRECTORY "${SOFTWARE_FONT_DIR}")
file(ARCHIVE_EXTRACT
    INPUT "${SOFTWARE_FONT_ARCHIVE}"
    DESTINATION "${SOFTWARE_FONT_DIR}")

set(SOFTWARE_FONT_FILES
    "${SOFTWARE_FONT_DIR}/${INFILTRATR_MB_CORPO_BRAND_REGULAR_FILE}"
    "${SOFTWARE_FONT_DIR}/${INFILTRATR_MB_CORPO_UI_BOLD_FILE}"
    "${SOFTWARE_FONT_DIR}/${INFILTRATR_MB_CORPO_UI_REGULAR_FILE}")
set(SOFTWARE_FONT_EXPECTED_SHA256
    "${INFILTRATR_MB_CORPO_BRAND_REGULAR_SHA256}"
    "${INFILTRATR_MB_CORPO_UI_BOLD_SHA256}"
    "${INFILTRATR_MB_CORPO_UI_REGULAR_SHA256}")
foreach(SOFTWARE_FONT_INDEX RANGE 0 2)
    list(GET SOFTWARE_FONT_FILES
         ${SOFTWARE_FONT_INDEX} SOFTWARE_FONT_FILE)
    list(GET SOFTWARE_FONT_EXPECTED_SHA256
         ${SOFTWARE_FONT_INDEX} SOFTWARE_FONT_SHA256_EXPECTED)
    file(SHA256 "${SOFTWARE_FONT_FILE}" SOFTWARE_FONT_SHA256_ACTUAL)
    if(NOT SOFTWARE_FONT_SHA256_ACTUAL
       STREQUAL SOFTWARE_FONT_SHA256_EXPECTED)
        message(FATAL_ERROR
            "Software font hash mismatch: ${SOFTWARE_FONT_FILE}")
    endif()
endforeach()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4>=4.6)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
pkg_check_modules(XAPP REQUIRED IMPORTED_TARGET xapp)
pkg_check_modules(APPSTREAM REQUIRED IMPORTED_TARGET appstream)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
pkg_check_modules(JSON_GLIB REQUIRED IMPORTED_TARGET json-glib-1.0)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
pkg_check_modules(ZLIB REQUIRED IMPORTED_TARGET zlib)
pkg_check_modules(LZMA REQUIRED IMPORTED_TARGET liblzma)
pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)

add_library(software-core STATIC
    src/core/exact_transaction_spec.cpp
    src/core/model.cpp
    src/core/update_freshness.cpp
    src/core/update_policy.cpp
    src/core/update_tracker.cpp)
target_include_directories(software-core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(software-engine STATIC
    src/engine/debian_control.cpp
    src/engine/debian_installed_state.cpp
    src/engine/debian_package_index.cpp
    src/engine/debian_version.cpp
    src/engine/debian_candidate.cpp
    src/engine/package_policy.cpp
    src/engine/debian_apt_configuration.cpp
    src/engine/debian_phased_updates.cpp
    src/engine/debian_preferences.cpp
    src/engine/debian_dependency.cpp
    src/engine/debian_repository.cpp
    src/engine/debian_source_configuration.cpp
    src/engine/debian_reconcile.cpp
    src/engine/package_state_store.cpp
    src/engine/debian_transaction.cpp
    src/engine/kernel_inventory.cpp
    src/engine/engine_service_core.cpp)
target_include_directories(software-engine PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(software-engine
    PUBLIC software-core
    PRIVATE InfiltratrCommon::Common
            PkgConfig::CURL PkgConfig::GLIB PkgConfig::ZLIB PkgConfig::LZMA
            PkgConfig::SQLITE3)

add_library(software-history STATIC
    src/core/transaction_history.cpp)
target_include_directories(software-history PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(software-history
    PUBLIC software-core
    PRIVATE PkgConfig::SQLITE3)

add_library(software-client STATIC
    src/client/engine_client.cpp
    src/client/engine_compatibility.cpp)
target_include_directories(software-client PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(software-client
    PUBLIC software-core
    PRIVATE PkgConfig::GIO)
target_compile_definitions(software-client PRIVATE
    SOFTWARE_VERSION="${PROJECT_VERSION}")

add_library(software-catalogue STATIC
    src/catalogue/repository_catalogue.cpp
    src/catalogue/catalogue_snapshot_store.cpp
    src/catalogue/system_catalogue.cpp)
target_include_directories(software-catalogue PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(software-catalogue
    PUBLIC software-core
    PRIVATE InfiltratrCommon::Common PkgConfig::APPSTREAM
            PkgConfig::CURL PkgConfig::JSON_GLIB)

# Reading configured sources and mutating them are deliberately separate build
# capabilities. The graphical application receives inventory only; the
# privileged helper is the sole consumer of source-file mutation code.
add_library(software-source-format STATIC
    src/sources/apt_source_format.cpp)
target_include_directories(software-source-format PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src")

# Native reconciliation consumes the same deb822 syntax authority as source
# inventory/mutation. Trust-sensitive fields must not be reparsed by a second
# engine-local implementation.
target_link_libraries(software-engine PRIVATE software-source-format)

add_library(software-source-inventory STATIC
    src/sources/source_inventory.cpp)
target_include_directories(software-source-inventory PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(software-source-inventory
    PUBLIC software-core
    PRIVATE software-source-format)

add_library(software-source-mutation STATIC
    src/sources/source_mutation.cpp)
target_include_directories(software-source-mutation PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(software-source-mutation
    PRIVATE software-source-format)

add_library(software-external STATIC
    src/external/external_updates.cpp
    src/external/cinnamon_spices.cpp)
target_include_directories(software-external PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(software-external
    PRIVATE software-core PkgConfig::GIO PkgConfig::CURL
            PkgConfig::JSON_GLIB PkgConfig::ZLIB)

add_library(software-apt-plan-guard STATIC
    src/helper/apt_plan_guard.cpp)
target_include_directories(software-apt-plan-guard PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_executable(software-helper
    src/helper/source_helper.cpp)
target_link_libraries(software-helper PRIVATE
    software-source-mutation)

add_executable(software-update-helper
    src/helper/update_helper.cpp)
target_link_libraries(software-update-helper PRIVATE
    software-apt-plan-guard software-engine PkgConfig::GLIB)

# The installed D-Bus interface XML is the protocol-schema authority. Embed
# that exact file into the service at configure time so the standalone binary
# remains self-contained without maintaining a second hand-written copy.
file(READ
    "${CMAKE_CURRENT_SOURCE_DIR}/data/net.ssmith.software.Engine.xml"
    SOFTWARE_ENGINE_INTERFACE_XML)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/service/engine_introspection.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/engine_introspection.hpp"
    @ONLY)

add_executable(software-engine
    src/service/main.cpp)
target_include_directories(software-engine PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(software-engine PRIVATE
    software-engine PkgConfig::GIO)
target_compile_definitions(software-engine PRIVATE
    SOFTWARE_VERSION="${PROJECT_VERSION}")

add_executable(software-tray
    src/tray/main.cpp)
target_link_libraries(software-tray PRIVATE
    software-client software-history software-external
    PkgConfig::GTK3 PkgConfig::XAPP)

add_executable(software-session-updater
    src/session/main.cpp)
target_link_libraries(software-session-updater PRIVATE
    software-core software-external PkgConfig::GLIB)

add_executable(software-cli
    src/cli/main.cpp)
target_link_libraries(software-cli PRIVATE
    software-core software-history software-client PkgConfig::GLIB)
target_compile_definitions(software-cli PRIVATE
    SOFTWARE_VERSION="${PROJECT_VERSION}")

add_executable(software-auto-updater
    src/automation/main.cpp)
target_link_libraries(software-auto-updater PRIVATE
    software-core software-history software-engine PkgConfig::GLIB)

add_executable(software-maintenance
    src/automation/maintenance.cpp)
target_link_libraries(software-maintenance PRIVATE
    software-core software-history software-engine PkgConfig::GLIB)

add_executable(software-release-upgrade
    src/release/main.cpp)
target_link_libraries(software-release-upgrade PRIVATE
    software-core software-engine PkgConfig::GLIB)

add_executable(software
    src/app/main.cpp
    src/app/discover_view.cpp
    src/app/updates_view.cpp
    src/app/page_support.cpp
    src/app/process_support.cpp
    src/app/view_lifecycle_bridge.cpp
    src/app/app_coordinator.cpp
    src/app/page_registry.cpp
    src/app/navigation_controller.cpp
    src/app/runtime_state.cpp
    src/app/transaction_review.cpp
    src/app/history_controller.cpp
    src/app/history_view.cpp
    src/app/installed_controller.cpp
    src/app/installed_inventory.cpp
    src/app/updates_controller.cpp
    src/app/repository_controller.cpp
    src/app/repository_view.cpp
    src/app/repair_diagnostics.cpp
    src/app/repair_view.cpp
    src/app/system_data.cpp
    src/app/system_view.cpp
    src/app/preferences_dialog.cpp
    src/app/kernel_manager.cpp
    src/app/theme.cpp
    src/app/ui_components.cpp)

target_link_libraries(software PRIVATE
    software-core software-history software-client
    software-catalogue software-source-inventory software-external
    InfiltratrCommon::Common PkgConfig::GTK4 PkgConfig::CURL)

target_compile_definitions(software PRIVATE
    SOFTWARE_VERSION="${PROJECT_VERSION}"
    SOFTWARE_BUILD_PROFILE="${SOFTWARE_BUILD_PROFILE}")

install(TARGETS
    software
    software-tray
    software-session-updater
    software-cli
    software-release-upgrade
    RUNTIME DESTINATION bin)
install(TARGETS
    software-helper
    software-update-helper
    software-engine
    software-auto-updater
    software-maintenance
    RUNTIME DESTINATION libexec)
install(FILES
    data/net.ssmith.software.Engine.service
    DESTINATION share/dbus-1/services)
install(FILES
    data/net.ssmith.software.Engine.xml
    DESTINATION share/dbus-1/interfaces)
install(FILES
    data/net.ssmith.software.policy
    data/net.ssmith.software.updates.policy
    DESTINATION share/polkit-1/actions)
install(FILES
    data/software-auto-update.service
    data/software-auto-update.timer
    data/software-maintenance.service
    data/software-maintenance.timer
    DESTINATION lib/systemd/system)
install(FILES
    data/status/software-up-to-date-symbolic.svg
    data/status/software-updates-available-symbolic.svg
    data/status/software-checking-symbolic.svg
    data/status/software-installing-symbolic.svg
    data/status/software-error-symbolic.svg
    DESTINATION share/icons/hicolor/scalable/status)
install(FILES ${SOFTWARE_FONT_FILES}
    DESTINATION share/fonts/truetype/software)
install(FILES LICENSE DESTINATION share/doc/software)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
