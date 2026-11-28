cmake_minimum_required(VERSION 3.12)
project("karta"
    LANGUAGES CXX
    DESCRIPTION "Hyprland layout similar to PaperWM and niri")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(CTest)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(KARTA_ENABLE_SANITIZERS "Enable ASan/UBSan in debug builds" OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(DEPS REQUIRED hyprland pixman-1 libdrm)
pkg_check_modules(HYPRLAND REQUIRED IMPORTED_TARGET hyprland)
pkg_check_modules(LUA REQUIRED IMPORTED_TARGET lua)
find_package(spdlog REQUIRED)

# Compile flags
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED true)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-gnu-unique")
endif()
add_library(karta SHARED
            src/plugin/entry.cpp
            src/plugin/config.h
            src/plugin/dispatch/registration.h
            src/plugin/dispatch/shared.h
            src/plugin/dispatch/registration.cpp
            src/plugin/dispatch/layout_commands.cpp
            src/plugin/dispatch/overview_mark_commands.cpp
            src/core/core.cpp
            src/core/focus_pointer_sync.cpp
            src/core/direction.cpp
            src/core/karta_workspace_snapshot.cpp
            src/core/layout_snapshot.cpp
            src/core/layout_math.cpp
            src/core/workarea_pager.cpp
            src/core/monitor_geometry.cpp
            src/core/monitor_geometry_runtime.cpp
            src/core/layout_profile.cpp
            src/core/workspace_selector.h
            src/core/linked_list.h
            src/overview/navigation/logic.h
            src/overview/navigation/logic.cpp
            src/overview/scene/orientation.h
            src/overview/scene/orientation.cpp
            src/overview/scene/geometry_utils.h
            src/overview/render/animation.h
            src/overview/render/style.h
            src/overview/model/workspace_grid.h
            src/overview/model/workspace_grid.cpp
            src/overview/model/model.h
            src/overview/model/model.cpp
            src/overview/model/layout.h
            src/overview/model/layout.cpp
            src/overview/scene/scene.h
            src/overview/scene/scene.cpp
            src/overview/scene/layout.h
            src/overview/scene/layout.cpp
            src/overview/session/session.h
            src/overview/session/session.cpp
            src/overview/session/effects.h
            src/overview/session/effects.cpp
            src/overview/session/effects_runtime.h
            src/overview/session/effects_logic.cpp
            src/overview/navigation/selection.h
            src/overview/navigation/selection.cpp
            src/overview/render/render.h
            src/overview/minimap/logic.cpp
            src/overview/minimap/runtime.cpp
            src/overview/render/render.cpp
            src/overview/render/state.h
            src/overview/render/state.cpp
            src/overview/render/draw.h
            src/overview/render/draw.cpp
            src/overview/render/hooks.h
            src/overview/render/hooks.cpp
            src/overview/render/pass_element.h
            src/overview/render/pass_element.cpp
            src/model/stack/pointer.cpp
            src/model/stack/label.cpp
            src/model/stack/window.cpp
            src/model/stack/core.cpp
            src/model/stack/membership.cpp
            src/model/stack/geometry.cpp
            src/layout/karta/core.cpp
            src/layout/karta/snapshot.cpp
            src/layout/karta/overview.cpp
            src/layout/karta/dispatch.cpp
            src/layout/karta/dispatch_runtime_logic.cpp
            src/layout/karta/dispatch_logic.cpp
            src/layout/karta/route.cpp
            src/layout/karta/route_logic.cpp
            src/layout/karta/topology_route.cpp
            src/layout/karta/layout_repository.cpp
            src/layout/karta/karta_workspace_repository.cpp
            src/layout/karta/workspace_controller_logic.cpp
            src/layout/karta/workspace_controller.cpp
            src/layout/karta/workspace.cpp
            src/layout/karta/focus.cpp
            src/layout/karta/commands.cpp
            src/layout/karta/internal.h
            src/layout/lane/core.cpp
            src/layout/lane/actions.cpp
            src/layout/lane/geometry.cpp)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(karta PRIVATE
        $<$<CONFIG:Debug>:-Wall;-Wextra;-Wpedantic>)

    if(KARTA_ENABLE_SANITIZERS)
        target_compile_options(karta PRIVATE
            $<$<CONFIG:Debug>:-fsanitize=address,undefined;-fno-omit-frame-pointer>)
        set(CMAKE_SHARED_LINKER_FLAGS_DEBUG
            "${CMAKE_SHARED_LINKER_FLAGS_DEBUG} -fsanitize=address,undefined")
    endif()
endif()

add_compile_definitions(WLR_USE_UNSTABLE)

target_include_directories(karta PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${DEPS_INCLUDE_DIRS}
            /usr/include/hyprland/wlroots-hyprland/hyprland 
            /usr/include/hyprland/src
)
target_link_libraries(karta PRIVATE spdlog::spdlog_header_only PkgConfig::LUA)
set_target_properties(karta PROPERTIES PREFIX "")

if(BUILD_TESTING)
    add_executable(karta_logic_tests
                   tests/test_main.cpp
                   tests/minimap_tests.cpp
                   src/overview/minimap/logic.cpp
                   tests/core_logic_tests.cpp
                   tests/layout_snapshot_tests.cpp
                   tests/layout_logic_tests.cpp
                   tests/topology_route_tests.cpp
                   tests/overview_logic_tests.cpp
                   tests/overview_model_tests.cpp
                   tests/overview_scene_tests.cpp
                   tests/overview_session_logic_tests.cpp
                   tests/overview_session_effects_tests.cpp
                   src/core/direction.cpp
                   src/core/karta_workspace_snapshot.cpp
                   src/core/layout_snapshot.cpp
                   src/core/layout_math.cpp
                   src/core/workarea_pager.cpp
                   src/core/monitor_geometry.cpp
                   src/core/layout_profile.cpp
                   src/overview/navigation/logic.cpp
                   src/overview/scene/orientation.cpp
                   src/overview/model/workspace_grid.cpp
                   src/overview/scene/layout.cpp
                   src/overview/session/effects_logic.cpp
                   src/overview/navigation/selection.cpp
                   src/layout/karta/dispatch_logic.cpp
                   src/layout/karta/dispatch_runtime_logic.cpp
                   src/layout/karta/route_logic.cpp
                   src/layout/karta/topology_route.cpp
                   src/layout/karta/workspace_controller_logic.cpp)
    target_include_directories(karta_logic_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${DEPS_INCLUDE_DIRS}
        /usr/include/hyprland/wlroots-hyprland/hyprland
        /usr/include/hyprland/src)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(karta_logic_tests PRIVATE
            $<$<CONFIG:Debug>:-Wall;-Wextra;-Wpedantic>)
        if(KARTA_ENABLE_SANITIZERS)
            target_compile_options(karta_logic_tests PRIVATE
                $<$<CONFIG:Debug>:-fsanitize=address,undefined;-fno-omit-frame-pointer>)
            target_link_options(karta_logic_tests PRIVATE
                $<$<CONFIG:Debug>:-fsanitize=address,undefined>)
        endif()
    endif()
    target_link_libraries(karta_logic_tests PRIVATE PkgConfig::HYPRLAND ${DEPS_LIBRARIES} spdlog::spdlog_header_only stdc++ m)
    set_target_properties(karta_logic_tests PROPERTIES LINKER_LANGUAGE CXX)
    add_test(NAME karta.logic COMMAND karta_logic_tests)
endif()
