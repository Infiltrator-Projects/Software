# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(software-release-metadata-test
    release_metadata_test.cpp)
target_include_directories(software-release-metadata-test PRIVATE "${PROJECT_SOURCE_DIR}/src")
add_test(NAME software-release-metadata-test
    COMMAND software-release-metadata-test)

# Release builds define NDEBUG by default, but the regression executables
# intentionally use assert() as their failure mechanism. Keep those
# assertions active so Release CTest cannot silently compile away checks.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(-UNDEBUG)
elseif(MSVC)
    add_compile_options(/UNDEBUG)
endif()
add_executable(software-model-test model_test.cpp)
target_link_libraries(software-model-test PRIVATE software-core)
add_test(NAME software-model-test COMMAND software-model-test)

add_executable(software-exact-transaction-spec-test
    exact_transaction_spec_test.cpp)
target_link_libraries(software-exact-transaction-spec-test PRIVATE
    software-core)
add_test(
    NAME software-exact-transaction-spec-test
    COMMAND software-exact-transaction-spec-test)

add_executable(software-engine-compatibility-test
    engine_compatibility_test.cpp)
target_link_libraries(software-engine-compatibility-test PRIVATE
    software-client)
add_test(
    NAME software-engine-compatibility-test
    COMMAND software-engine-compatibility-test)

add_executable(software-transaction-history-test
    transaction_history_test.cpp)
target_link_libraries(software-transaction-history-test PRIVATE
    software-history)
add_test(
    NAME software-transaction-history-test
    COMMAND software-transaction-history-test)

add_executable(software-update-freshness-test
    update_freshness_test.cpp)
target_link_libraries(software-update-freshness-test PRIVATE software-core)
add_test(NAME software-update-freshness-test
    COMMAND software-update-freshness-test)

add_executable(software-update-policy-test
    update_policy_test.cpp)
target_link_libraries(software-update-policy-test PRIVATE software-core)
add_test(NAME software-update-policy-test
    COMMAND software-update-policy-test)

add_executable(software-update-tracker-test
    update_tracker_test.cpp)
target_link_libraries(software-update-tracker-test PRIVATE software-core)
add_test(NAME software-update-tracker-test
    COMMAND software-update-tracker-test)

add_executable(software-debian-installed-test
    debian_installed_state_test.cpp)
target_link_libraries(software-debian-installed-test PRIVATE software-engine)
add_test(
    NAME software-debian-installed-test
    COMMAND software-debian-installed-test)

add_executable(software-debian-index-test
    debian_package_index_test.cpp)
target_link_libraries(software-debian-index-test PRIVATE software-engine)
add_test(
    NAME software-debian-index-test
    COMMAND software-debian-index-test)

add_executable(software-debian-version-test
    debian_version_test.cpp)
target_link_libraries(software-debian-version-test PRIVATE software-engine)
add_test(
    NAME software-debian-version-test
    COMMAND software-debian-version-test)

add_executable(software-debian-candidate-test
    debian_candidate_test.cpp)
target_link_libraries(software-debian-candidate-test PRIVATE software-engine)
add_test(
    NAME software-debian-candidate-test
    COMMAND software-debian-candidate-test)

add_executable(software-debian-phased-updates-test
    debian_phased_updates_test.cpp)
target_link_libraries(software-debian-phased-updates-test PRIVATE software-engine)
add_test(
    NAME software-debian-phased-updates-test
    COMMAND software-debian-phased-updates-test)

add_executable(software-debian-preferences-test
    debian_preferences_test.cpp)
target_link_libraries(software-debian-preferences-test PRIVATE software-engine)
add_test(
    NAME software-debian-preferences-test
    COMMAND software-debian-preferences-test)

add_executable(software-package-policy-test
    package_policy_test.cpp)
target_link_libraries(software-package-policy-test PRIVATE software-engine)
add_test(
    NAME software-package-policy-test
    COMMAND software-package-policy-test)

add_executable(software-debian-dependency-test
    debian_dependency_test.cpp)
target_link_libraries(software-debian-dependency-test PRIVATE software-engine)
add_test(
    NAME software-debian-dependency-test
    COMMAND software-debian-dependency-test)

add_executable(software-debian-repository-test
    debian_repository_test.cpp)
target_link_libraries(software-debian-repository-test PRIVATE software-engine)
add_test(
    NAME software-debian-repository-test
    COMMAND software-debian-repository-test)

add_executable(software-debian-source-configuration-test
    debian_source_configuration_test.cpp)
target_link_libraries(software-debian-source-configuration-test PRIVATE software-engine)
add_test(
    NAME software-debian-source-configuration-test
    COMMAND software-debian-source-configuration-test)

add_executable(software-package-state-test
    package_state_store_test.cpp)
target_link_libraries(software-package-state-test PRIVATE software-engine)
add_test(
    NAME software-package-state-test
    COMMAND software-package-state-test)

add_executable(software-debian-transaction-test
    debian_transaction_test.cpp)
target_link_libraries(software-debian-transaction-test PRIVATE software-engine)
add_test(
    NAME software-debian-transaction-test
    COMMAND software-debian-transaction-test)

add_executable(software-kernel-inventory-test
    kernel_inventory_test.cpp)
target_link_libraries(software-kernel-inventory-test PRIVATE software-engine)
add_test(
    NAME software-kernel-inventory-test
    COMMAND software-kernel-inventory-test)

add_executable(software-engine-service-core-test
    engine_service_core_test.cpp)
target_link_libraries(software-engine-service-core-test PRIVATE software-engine)
add_test(
    NAME software-engine-service-core-test
    COMMAND software-engine-service-core-test)

add_executable(software-apt-plan-guard-test
    apt_plan_guard_test.cpp)
target_link_libraries(software-apt-plan-guard-test PRIVATE
    software-apt-plan-guard)
add_test(
    NAME software-apt-plan-guard-test
    COMMAND software-apt-plan-guard-test)

add_executable(software-catalogue-test catalogue_test.cpp)
target_link_libraries(software-catalogue-test PRIVATE software-catalogue)
add_test(NAME software-catalogue-test COMMAND software-catalogue-test)

add_executable(software-catalogue-store-test
    catalogue_snapshot_store_test.cpp)
target_link_libraries(software-catalogue-store-test PRIVATE software-catalogue)
add_test(
    NAME software-catalogue-store-test
    COMMAND software-catalogue-store-test)

add_executable(software-sources-test source_inventory_test.cpp)
target_link_libraries(software-sources-test PRIVATE software-sources)
add_test(NAME software-sources-test COMMAND software-sources-test)

add_executable(software-external-updates-test
    external_updates_test.cpp)
target_link_libraries(software-external-updates-test PRIVATE
    software-external)
add_test(
    NAME software-external-updates-test
    COMMAND software-external-updates-test)
