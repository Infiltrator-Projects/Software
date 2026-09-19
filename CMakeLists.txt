# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" SOFTWARE_VERSION)
string(STRIP "${SOFTWARE_VERSION}" SOFTWARE_VERSION)
if(NOT SOFTWARE_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must contain major.minor.patch")
endif()

project(InfiltratorSoftware VERSION "${SOFTWARE_VERSION}" LANGUAGES C CXX)
include(CTest)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(INFILTRATR_COMMON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/infiltratr-common")
set(INFILTRATR_COMMON_EXPECTED_VERSION "1.19.8")
set(INFILTRATR_COMMON_EXPECTED_COMMIT "3bfcb6f76ca44ac33bc2fee54fb114caa0eca5f9")

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

add_library(software-core STATIC src/core/model.cpp)
target_include_directories(software-core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(software-apt STATIC src/backends/apt/apt_backend.cpp)
target_include_directories(software-apt PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(software-apt PUBLIC software-core)

add_executable(infiltrator-software
    src/app/main.cpp
    src/app/theme.cpp)
target_link_libraries(infiltrator-software PRIVATE
    software-core software-apt InfiltratrCommon::Common PkgConfig::GTK4)

target_compile_definitions(infiltrator-software PRIVATE
    INFILTRATOR_SOFTWARE_VERSION="${PROJECT_VERSION}")

install(TARGETS infiltrator-software RUNTIME DESTINATION bin)
install(FILES ${SOFTWARE_FONT_FILES}
    DESTINATION share/fonts/truetype/infiltrator-software)
install(FILES LICENSE DESTINATION share/doc/infiltrator-software)

foreach(target IN ITEMS software-core software-apt infiltrator-software)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
    endif()
endforeach()

if(BUILD_TESTING)
    add_executable(software-model-test tests/model_test.cpp)
    target_link_libraries(software-model-test PRIVATE software-core)
    add_test(NAME software-model-test COMMAND software-model-test)

    add_executable(software-apt-test tests/apt_backend_test.cpp)
    target_link_libraries(software-apt-test PRIVATE software-apt)
    add_test(NAME software-apt-test COMMAND software-apt-test)
endif()
