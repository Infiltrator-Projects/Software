# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" SOFTWARE_VERSION)
string(STRIP "${SOFTWARE_VERSION}" SOFTWARE_VERSION)
if(NOT SOFTWARE_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must contain major.minor.patch")
endif()

project(InfiltratorSoftware VERSION "${SOFTWARE_VERSION}" LANGUAGES C CXX)
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
set(INFILTRATR_COMMON_EXPECTED_VERSION "1.19.24")
set(INFILTRATR_COMMON_EXPECTED_COMMIT "748e089ae175329471d4cf375522c44081371bd5")

if(NOT EXISTS "${INFILTRATR_COMMON_DIR}/VERSION")
    message(FATAL_ERROR
        "Common is missing. Clone recursively or run: git submodule update --init --recursive")
endif()

file(READ "${INFILTRATR_COMMON_DIR}/VERSION" INFILTRATR_COMMON_VERSION)
string(STRIP "${INFILTRATR_COMMON_VERSION}" INFILTRATR_COMMON_VERSION)
if(NOT INFILTRATR_COMMON_VERSION STREQUAL INFILTRATR_COMMON_EXPECTED_VERSION)
    message(FATAL_ERROR
        "Infiltrator Software requires Common ${INFILTRATR_COMMON_EXPECTED_VERSION}; "
        "found ${INFILTRATR_COMMON_VERSION}")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    find_package(Git REQUIRED)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${INFILTRATR_COMMON_DIR}" rev-parse HEAD
        RESULT_VARIABLE COMMON_GIT_RESULT
        OUTPUT_VARIABLE COMMON_GIT_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(NOT COMMON_GIT_RESULT EQUAL 0 OR
       NOT COMMON_GIT_COMMIT STREQUAL INFILTRATR_COMMON_EXPECTED_COMMIT)
        message(FATAL_ERROR
            "Common must be pinned to ${INFILTRATR_COMMON_EXPECTED_COMMIT}; "
            "found ${COMMON_GIT_COMMIT}")
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

set(SOFTWARE_FONT_ARCHIVE
    "${CMAKE_CURRENT_BINARY_DIR}/software-mb-corpo-fonts.tar.xz")
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
        "${SOFTWARE_FONT_DOWNLOAD_MESSAGE}")
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
    src/core/model.cpp
    src/core/update_freshness.cpp)
target_include_directories(software-core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(software-engine STATIC
    src/engine/debian_installed_state.cpp
    src/engine/debian_package_index.cpp
    src/engine/debian_version.cpp
    src/engine/debian_candidate.cpp
    src/engine/package_policy.cpp
    src/engine/debian_phased_updates.cpp
    src/engine/debian_preferences.cpp
    src/engine/debian_dependency.cpp
    src/engine/debian_repository.cpp
    src/engine/debian_source_configuration.cpp
    src/engine/debian_reconcile.cpp
    src/engine/package_state_store.cpp
    src/engine/debian_transaction.cpp
    src/engine/engine_service_core.cpp)
target_include_directories(software-engine PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(software-engine PUBLIC software-core
    PRIVATE InfiltratrCommon::Common
            PkgConfig::CURL PkgConfig::GLIB PkgConfig::ZLIB PkgConfig::LZMA
            PkgConfig::SQLITE3)

add_library(software-history STATIC
    src/core/transaction_history.cpp)
target_include_directories(software-history PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(software-history PUBLIC
    software-core
    PRIVATE PkgConfig::SQLITE3)

add_library(software-client STATIC
    src/client/engine_client.cpp)
target_include_directories(software-client PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(software-client PUBLIC
    software-core PkgConfig::GIO)
target_compile_definitions(software-client PRIVATE
    INFILTRATOR_SOFTWARE_VERSION="${PROJECT_VERSION}")

add_library(software-apt STATIC src/backends/apt/apt_backend.cpp)
target_include_directories(software-apt PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(software-apt PUBLIC software-engine)

add_library(software-catalogue STATIC
    src/catalogue/repository_catalogue.cpp
    src/catalogue/catalogue_snapshot_store.cpp
    src/catalogue/system_catalogue.cpp)
target_include_directories(software-catalogue PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(software-catalogue PUBLIC
    software-core InfiltratrCommon::Common
    PkgConfig::APPSTREAM PkgConfig::CURL PkgConfig::JSON_GLIB)

add_library(software-sources STATIC
    src/sources/source_inventory.cpp
    src/sources/source_mutation.cpp)
target_include_directories(software-sources PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(software-apt-plan-guard STATIC
    src/helper/apt_plan_guard.cpp)
target_include_directories(software-apt-plan-guard PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_executable(infiltrator-software-helper
    src/helper/source_helper.cpp)
target_link_libraries(infiltrator-software-helper PRIVATE
    software-sources)

add_executable(infiltrator-software-update-helper
    src/helper/update_helper.cpp)
target_link_libraries(infiltrator-software-update-helper PRIVATE
    software-apt-plan-guard)

add_executable(infiltrator-software-engine
    src/service/main.cpp)
target_link_libraries(infiltrator-software-engine PRIVATE
    software-engine PkgConfig::GIO)
target_compile_definitions(infiltrator-software-engine PRIVATE
    INFILTRATOR_SOFTWARE_VERSION="${PROJECT_VERSION}")

add_executable(infiltrator-software-tray
    src/tray/main.cpp)
target_link_libraries(infiltrator-software-tray PRIVATE
    software-client PkgConfig::GTK3 PkgConfig::XAPP)

find_program(GLIB_COMPILE_RESOURCES
    NAMES glib-compile-resources
    REQUIRED)

set(SOFTWARE_RESOURCE_XML
    "${CMAKE_CURRENT_SOURCE_DIR}/data/infiltrator-software.gresource.xml")
set(SOFTWARE_RESOURCE_C
    "${CMAKE_CURRENT_BINARY_DIR}/infiltrator-software-resources.c")

add_custom_command(
    OUTPUT "${SOFTWARE_RESOURCE_C}"
    COMMAND "${GLIB_COMPILE_RESOURCES}"
            "${SOFTWARE_RESOURCE_XML}"
            --target="${SOFTWARE_RESOURCE_C}"
            --generate-source
            --sourcedir="${CMAKE_CURRENT_SOURCE_DIR}/data"
    DEPENDS
        "${SOFTWARE_RESOURCE_XML}"
        "${CMAKE_CURRENT_SOURCE_DIR}/data/artwork/discover-hero.jpg"
    VERBATIM)
set_source_files_properties(
    "${SOFTWARE_RESOURCE_C}"
    PROPERTIES GENERATED TRUE)

add_executable(infiltrator-software
    src/app/main.cpp
    src/app/theme.cpp
    "${SOFTWARE_RESOURCE_C}")
target_link_libraries(infiltrator-software PRIVATE
    software-core software-history software-client software-apt
    software-catalogue software-sources
    InfiltratrCommon::Common PkgConfig::GTK4)

target_compile_definitions(infiltrator-software PRIVATE
    INFILTRATOR_SOFTWARE_VERSION="${PROJECT_VERSION}"
    INFILTRATOR_SOFTWARE_BUILD_PROFILE="${SOFTWARE_BUILD_PROFILE}")

install(TARGETS infiltrator-software infiltrator-software-tray
    RUNTIME DESTINATION bin)
install(TARGETS
    infiltrator-software-helper
    infiltrator-software-update-helper
    infiltrator-software-engine
    RUNTIME DESTINATION libexec)
install(FILES
    data/net.ssmith.infiltrator.software.Engine.service
    DESTINATION share/dbus-1/services)
install(FILES
    data/net.ssmith.infiltrator.software.Engine.xml
    DESTINATION share/dbus-1/interfaces)
install(FILES
    data/net.ssmith.infiltrator.software.policy
    data/net.ssmith.infiltrator.software.updates.policy
    DESTINATION share/polkit-1/actions)
install(FILES
    data/status/infiltrator-software-up-to-date-symbolic.svg
    data/status/infiltrator-software-updates-available-symbolic.svg
    data/status/infiltrator-software-checking-symbolic.svg
    data/status/infiltrator-software-installing-symbolic.svg
    data/status/infiltrator-software-error-symbolic.svg
    DESTINATION share/icons/hicolor/scalable/status)
install(FILES ${SOFTWARE_FONT_FILES}
    DESTINATION share/fonts/truetype/infiltrator-software)
install(FILES
    data/artwork/discover-hero.jpg
    DESTINATION share/infiltrator-software/artwork)
install(FILES LICENSE DESTINATION share/doc/infiltrator-software)

foreach(target IN ITEMS
        software-core software-history software-engine software-client software-apt software-catalogue software-sources
        software-apt-plan-guard
        infiltrator-software-helper infiltrator-software-update-helper
        infiltrator-software-engine infiltrator-software-tray
        infiltrator-software)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
    endif()
endforeach()

if(BUILD_TESTING)
    # Release builds define NDEBUG by default, but the regression executables
    # intentionally use assert() as their failure mechanism. Keep those
    # assertions active so Release CTest cannot silently compile away checks.
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        add_compile_options(-UNDEBUG)
    elseif(MSVC)
        add_compile_options(/UNDEBUG)
    endif()
    add_executable(software-model-test tests/model_test.cpp)
    target_link_libraries(software-model-test PRIVATE software-core)
    add_test(NAME software-model-test COMMAND software-model-test)

    add_executable(software-transaction-history-test
        tests/transaction_history_test.cpp)
    target_link_libraries(software-transaction-history-test PRIVATE
        software-history)
    add_test(
        NAME software-transaction-history-test
        COMMAND software-transaction-history-test)

    add_executable(software-update-freshness-test
        tests/update_freshness_test.cpp)
    target_link_libraries(software-update-freshness-test PRIVATE software-core)
    add_test(NAME software-update-freshness-test
        COMMAND software-update-freshness-test)

    add_executable(software-debian-installed-test
        tests/debian_installed_state_test.cpp)
    target_link_libraries(software-debian-installed-test PRIVATE software-engine)
    add_test(
        NAME software-debian-installed-test
        COMMAND software-debian-installed-test)

    add_executable(software-debian-index-test
        tests/debian_package_index_test.cpp)
    target_link_libraries(software-debian-index-test PRIVATE software-engine)
    add_test(
        NAME software-debian-index-test
        COMMAND software-debian-index-test)

    add_executable(software-debian-version-test
        tests/debian_version_test.cpp)
    target_link_libraries(software-debian-version-test PRIVATE software-engine)
    add_test(
        NAME software-debian-version-test
        COMMAND software-debian-version-test)

    add_executable(software-debian-candidate-test
        tests/debian_candidate_test.cpp)
    target_link_libraries(software-debian-candidate-test PRIVATE software-engine)
    add_test(
        NAME software-debian-candidate-test
        COMMAND software-debian-candidate-test)

    add_executable(software-debian-phased-updates-test
        tests/debian_phased_updates_test.cpp)
    target_link_libraries(software-debian-phased-updates-test PRIVATE software-engine)
    add_test(
        NAME software-debian-phased-updates-test
        COMMAND software-debian-phased-updates-test)

    add_executable(software-debian-preferences-test
        tests/debian_preferences_test.cpp)
    target_link_libraries(software-debian-preferences-test PRIVATE software-engine)
    add_test(
        NAME software-debian-preferences-test
        COMMAND software-debian-preferences-test)

    add_executable(software-package-policy-test
        tests/package_policy_test.cpp)
    target_link_libraries(software-package-policy-test PRIVATE software-engine)
    add_test(
        NAME software-package-policy-test
        COMMAND software-package-policy-test)

    add_executable(software-debian-dependency-test
        tests/debian_dependency_test.cpp)
    target_link_libraries(software-debian-dependency-test PRIVATE software-engine)
    add_test(
        NAME software-debian-dependency-test
        COMMAND software-debian-dependency-test)

    add_executable(software-debian-repository-test
        tests/debian_repository_test.cpp)
    target_link_libraries(software-debian-repository-test PRIVATE software-engine)
    add_test(
        NAME software-debian-repository-test
        COMMAND software-debian-repository-test)

    add_executable(software-debian-source-configuration-test
        tests/debian_source_configuration_test.cpp)
    target_link_libraries(software-debian-source-configuration-test PRIVATE software-engine)
    add_test(
        NAME software-debian-source-configuration-test
        COMMAND software-debian-source-configuration-test)

    add_executable(software-package-state-test
        tests/package_state_store_test.cpp)
    target_link_libraries(software-package-state-test PRIVATE software-engine)
    add_test(
        NAME software-package-state-test
        COMMAND software-package-state-test)

    add_executable(software-debian-transaction-test
        tests/debian_transaction_test.cpp)
    target_link_libraries(software-debian-transaction-test PRIVATE software-engine)
    add_test(
        NAME software-debian-transaction-test
        COMMAND software-debian-transaction-test)

    add_executable(software-engine-service-core-test
        tests/engine_service_core_test.cpp)
    target_link_libraries(software-engine-service-core-test PRIVATE software-engine)
    add_test(
        NAME software-engine-service-core-test
        COMMAND software-engine-service-core-test)

    add_executable(software-apt-test tests/apt_backend_test.cpp)
    target_link_libraries(software-apt-test PRIVATE software-apt)
    add_test(NAME software-apt-test COMMAND software-apt-test)

    add_executable(software-apt-plan-guard-test
        tests/apt_plan_guard_test.cpp)
    target_link_libraries(software-apt-plan-guard-test PRIVATE
        software-apt-plan-guard)
    add_test(
        NAME software-apt-plan-guard-test
        COMMAND software-apt-plan-guard-test)

    add_executable(software-catalogue-test tests/catalogue_test.cpp)
    target_link_libraries(software-catalogue-test PRIVATE software-catalogue)
    add_test(NAME software-catalogue-test COMMAND software-catalogue-test)

    add_executable(software-catalogue-store-test
        tests/catalogue_snapshot_store_test.cpp)
    target_link_libraries(software-catalogue-store-test PRIVATE software-catalogue)
    add_test(
        NAME software-catalogue-store-test
        COMMAND software-catalogue-store-test)

    add_executable(software-sources-test tests/source_inventory_test.cpp)
    target_link_libraries(software-sources-test PRIVATE software-sources)
    add_test(NAME software-sources-test COMMAND software-sources-test)
endif()
