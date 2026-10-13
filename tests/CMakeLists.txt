# SPDX-License-Identifier: GPL-3.0-or-later

# Release builds define NDEBUG by default, but most regression executables use
# assert() as their failure mechanism. Keep those assertions active so Release
# CTest cannot silently compile away checks.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(-UNDEBUG)
elseif(MSVC)
    add_compile_options(/UNDEBUG)
endif()

function(add_software_test target source)
    add_executable(${target} ${source})
    if(ARGN)
        target_link_libraries(${target} PRIVATE ${ARGN})
    endif()
    add_test(NAME ${target} COMMAND ${target})
endfunction()

add_software_test(software-release-metadata-test release_metadata_test.cpp)
target_include_directories(software-release-metadata-test PRIVATE
    "${PROJECT_SOURCE_DIR}/src")

add_software_test(software-text-test text_test.cpp)
target_include_directories(software-text-test PRIVATE "${PROJECT_SOURCE_DIR}/src")
add_software_test(software-model-test model_test.cpp software-core)
add_software_test(software-exact-transaction-spec-test
    exact_transaction_spec_test.cpp software-core)
add_software_test(software-engine-compatibility-test
    engine_compatibility_test.cpp software-client)
add_software_test(software-transaction-history-test
    transaction_history_test.cpp software-history)
add_software_test(software-update-freshness-test
    update_freshness_test.cpp software-core)
add_software_test(software-update-policy-test
    update_policy_test.cpp software-core)
add_software_test(software-update-tracker-test
    update_tracker_test.cpp software-core)
add_software_test(software-debian-control-test
    debian_control_test.cpp software-engine)
add_software_test(software-debian-installed-test
    debian_installed_state_test.cpp software-engine)
add_software_test(software-debian-index-test
    debian_package_index_test.cpp software-engine)
add_software_test(software-debian-version-test
    debian_version_test.cpp software-engine)
add_software_test(software-debian-candidate-test
    debian_candidate_test.cpp software-engine)
add_software_test(software-debian-phased-updates-test
    debian_phased_updates_test.cpp software-engine)
add_software_test(software-debian-preferences-test
    debian_preferences_test.cpp software-engine)
add_software_test(software-package-policy-test
    package_policy_test.cpp software-engine)
add_software_test(software-debian-dependency-test
    debian_dependency_test.cpp software-engine)
add_software_test(software-debian-repository-test
    debian_repository_test.cpp software-engine)
add_software_test(software-debian-source-configuration-test
    debian_source_configuration_test.cpp software-engine)
add_software_test(software-package-state-test
    package_state_store_test.cpp software-engine)
add_software_test(software-debian-transaction-test
    debian_transaction_test.cpp software-engine)
add_software_test(software-kernel-inventory-test
    kernel_inventory_test.cpp software-engine)
add_software_test(software-engine-service-core-test
    engine_service_core_test.cpp software-engine)
add_software_test(software-apt-plan-guard-test
    apt_plan_guard_test.cpp software-apt-plan-guard)
add_software_test(software-catalogue-test
    catalogue_test.cpp software-catalogue)
add_software_test(software-catalogue-store-test
    catalogue_snapshot_store_test.cpp software-catalogue)
add_software_test(software-sources-test
    source_inventory_test.cpp software-source-inventory software-source-mutation)
add_software_test(software-apt-source-format-test
    apt_source_format_test.cpp software-source-inventory software-source-mutation)
add_software_test(software-external-updates-test
    external_updates_test.cpp software-external)
