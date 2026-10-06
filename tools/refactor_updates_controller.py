#!/usr/bin/env python3
from pathlib import Path


def once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


main_path = Path("src/app/main.cpp")
main = main_path.read_text()
main = once(
    main,
    '#include "app/ui_components.hpp"\n',
    '#include "app/ui_components.hpp"\n#include "app/updates_controller.hpp"\n',
    "controller include",
)

start = main.index("struct UpdatesResult {")
end = main.index("struct UpdateProcessRun {", start)
main = main[:start] + main[end:]

start = main.index("void updates_worker(")
end = main.index("\ngboolean auto_refresh_updates_idle", start)
main = main[:start] + '''void updates_worker(
    GTask *task,
    gpointer,
    gpointer task_data,
    GCancellable *)
{
    auto *data =
        static_cast<UpdatesRefreshRequest *>(task_data);
    auto *result = new UpdatesRefreshResult{};
    if (data == nullptr) {
        result->error =
            "Update task state is unavailable.";
    } else {
        *result = refresh_updates_data(*data);
    }

    g_task_return_pointer(
        task,
        result,
        [](gpointer pointer) {
            delete static_cast<UpdatesRefreshResult *>(pointer);
        });
}
''' + main[end:]

main = once(
    main,
    "auto *result = static_cast<UpdatesResult *>(\n        g_task_propagate_pointer(G_TASK(async_result), nullptr));",
    "auto *result = static_cast<UpdatesRefreshResult *>(\n        g_task_propagate_pointer(G_TASK(async_result), nullptr));",
    "refresh result type",
)
main = once(main, "new UpdatesTaskData{", "new UpdatesRefreshRequest{", "refresh task type")
main = once(
    main,
    "delete static_cast<UpdatesTaskData *>(pointer);",
    "delete static_cast<UpdatesRefreshRequest *>(pointer);",
    "refresh task deleter",
)

start = main.index("void update_plan_worker(")
end = main.index("\nvoid update_plan_complete", start)
main = main[:start] + '''void update_plan_worker(
    GTask *task,
    gpointer,
    gpointer task_data,
    GCancellable *)
{
    auto *data =
        static_cast<UpdatePlanRequest *>(task_data);
    auto *result = new UpdatePlanResult{};
    if (data == nullptr) {
        result->error =
            "No updates are available to plan.";
    } else {
        *result = plan_updates(*data);
    }

    g_task_return_pointer(
        task,
        result,
        [](gpointer pointer) {
            delete static_cast<UpdatePlanResult *>(pointer);
        });
}
''' + main[end:]
main = once(main, "new UpdatePlanTaskData{}", "new UpdatePlanRequest{}", "plan task type")
main = once(
    main,
    "delete static_cast<UpdatePlanTaskData *>(pointer);",
    "delete static_cast<UpdatePlanRequest *>(pointer);",
    "plan task deleter",
)
main_path.write_text(main)

cmake_path = Path("CMakeLists.txt")
cmake = cmake_path.read_text()
cmake = once(
    cmake,
    "    src/app/installed_inventory.cpp\n    src/app/repository_controller.cpp",
    "    src/app/installed_inventory.cpp\n    src/app/updates_controller.cpp\n    src/app/repository_controller.cpp",
    "CMake controller source",
)
cmake_path.write_text(cmake)

contracts_path = Path("tests/source_contracts.py")
contracts = contracts_path.read_text()
contracts = once(
    contracts,
    'installed_inventory = text("src/app/installed_inventory.cpp")\n',
    'installed_inventory = text("src/app/installed_inventory.cpp")\nmain_cpp = text("src/app/main.cpp")\nupdates_controller = text("src/app/updates_controller.cpp")\nupdates_controller_hpp = text("src/app/updates_controller.hpp")\n',
    "contract inputs",
)
contracts = once(
    contracts,
    'assert "engine.refresh_installed(result->error)" in app\nassert "engine.refresh(result->error)" in app\n',
    'assert "engine.refresh_installed(result.error)" in updates_controller\nassert "engine.refresh(result.error)" in updates_controller\n',
    "engine ownership contracts",
)
anchor = 'assert "struct WindowState final" in window_state\n'
addition = '''assert "struct WindowState final" in window_state

# Native update reconciliation, external update discovery and update planning
# belong to the Updates controller. main.cpp owns GTK task lifetime and
# presentation, not package-engine orchestration.
assert '"app/updates_controller.hpp"' in main_cpp
assert "src/app/updates_controller.cpp" in cmake
assert "refresh_updates_data" in updates_controller
assert "plan_updates" in updates_controller
assert "engine.list_updates(" in updates_controller
assert "discover_flatpak_updates(" in updates_controller
assert "discover_cinnamon_updates(" in updates_controller
assert "engine.list_updates(" not in main_cpp
assert "discover_flatpak_updates(" not in main_cpp
assert "discover_cinnamon_updates(" not in main_cpp
'''
contracts = once(contracts, anchor, addition, "controller boundary contracts")
contracts_path.write_text(contracts)
