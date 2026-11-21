#include <vector>

#include "core/layout_math.h"
#include "overview/navigation/logic.h"
#include "overview/scene/orientation.h"
#include "overview/scene/layout.h"

#include "test_suite.h"
#include "test_support.h"

namespace {

void test_overview_projection() {
    const ScrollerCore::Box visible(0.0, 0.0, 200.0, 100.0);
    const std::vector<ScrollerCore::OverviewRect> items = {
        {.x0 = 10.0, .x1 = 60.0, .y0 = 20.0, .y1 = 70.0},
        {.x0 = 60.0, .x1 = 110.0, .y0 = 10.0, .y1 = 90.0},
    };

    const auto projection = ScrollerCore::compute_overview_projection(items, visible);
    expect_near(projection.min.x, 10.0, 1e-9, "overview projection tracks minimum x");
    expect_near(projection.min.y, 10.0, 1e-9, "overview projection tracks minimum y");
    expect_near(projection.max.x, 110.0, 1e-9, "overview projection tracks maximum x");
    expect_near(projection.max.y, 90.0, 1e-9, "overview projection tracks maximum y");
    expect_near(projection.width, 100.0, 1e-9, "overview projection width is derived from bounds");
    expect_near(projection.height, 80.0, 1e-9, "overview projection height is derived from bounds");
    expect_near(projection.scale, 1.25, 1e-9, "overview projection chooses the limiting scale");
    expect_near(projection.offset.x, 37.5, 1e-9, "overview projection centers on x");
    expect_near(projection.offset.y, 0.0, 1e-9, "overview projection centers on y");

    const std::vector<ScrollerCore::OverviewRect> degenerate = {
        {.x0 = 50.0, .x1 = 50.0, .y0 = 10.0, .y1 = 40.0},
    };
    const auto degenerateProjection = ScrollerCore::compute_overview_projection(degenerate, visible);
    expect_near(degenerateProjection.scale, 1.0, 1e-9, "degenerate projection keeps scale at 1");
    expect_near(degenerateProjection.offset.x, 50.0, 1e-9, "degenerate projection preserves raw x offset");
    expect_near(degenerateProjection.offset.y, 10.0, 1e-9, "degenerate projection preserves raw y offset");
}

void test_monitor_space_orientation() {
    using Overview::MonitorOrientation;

    expect_eq(Overview::orientation_for_transform(WL_OUTPUT_TRANSFORM_NORMAL),
              MonitorOrientation::Landscape,
              "normal transform is landscape");
    expect_eq(Overview::orientation_for_transform(WL_OUTPUT_TRANSFORM_180),
              MonitorOrientation::Landscape,
              "180 transform stays landscape");
    expect_eq(Overview::orientation_for_transform(WL_OUTPUT_TRANSFORM_90),
              MonitorOrientation::Portrait,
              "90 transform is portrait");
    expect_eq(Overview::orientation_for_transform(WL_OUTPUT_TRANSFORM_270),
              MonitorOrientation::Portrait,
              "270 transform is portrait");

    const auto portraitRenderBox = Overview::transform_box_to_render_space({10.0, 20.0, 100.0, 200.0},
                                                                           WL_OUTPUT_TRANSFORM_270,
                                                                           1080.0,
                                                                           1920.0);
    expect_near(portraitRenderBox.x, 860.0, 1e-9, "portrait transform remaps x into render space");
    expect_near(portraitRenderBox.y, 10.0, 1e-9, "portrait transform remaps y into render space");
    expect_near(portraitRenderBox.w, 200.0, 1e-9, "portrait transform swaps width");
    expect_near(portraitRenderBox.h, 100.0, 1e-9, "portrait transform swaps height");

    const auto landscapeRenderBox = Overview::transform_box_to_render_space({10.0, 20.0, 100.0, 200.0},
                                                                            WL_OUTPUT_TRANSFORM_NORMAL,
                                                                            1920.0,
                                                                            1080.0);
    expect_near(landscapeRenderBox.x, 10.0, 1e-9, "landscape transform keeps x stable");
    expect_near(landscapeRenderBox.y, 20.0, 1e-9, "landscape transform keeps y stable");
    expect_near(landscapeRenderBox.w, 100.0, 1e-9, "landscape transform keeps width stable");
    expect_near(landscapeRenderBox.h, 200.0, 1e-9, "landscape transform keeps height stable");
}

void test_overview_target_selection_across_monitors() {
    const std::vector<OverviewLogic::TargetCandidate> targets = {
        {.monitorId = 1, .box = {0.0, 0.0, 100.0, 100.0}},
        {.monitorId = 1, .box = {120.0, 0.0, 100.0, 100.0}},
        {.monitorId = 2, .box = {400.0, 0.0, 100.0, 100.0}},
    };

    const auto next = OverviewLogic::pickTargetIndex(targets, 1, Direction::Right);
    expect_true(next.has_value() && *next == 2,
                "overview target selection crosses to the next monitor when the nearest target is there");
}

void test_overview_target_selection_prefers_rendered_neighbor() {
    const std::vector<ScrollerCore::Box> workspaceOneBoxes = {
        {0.0, 0.0, 100.0, 80.0},
        {0.0, 120.0, 100.0, 80.0},
    };
    const std::vector<ScrollerCore::Box> workspaceTwoBoxes = {
        {0.0, 0.0, 100.0, 80.0},
        {0.0, 120.0, 100.0, 80.0},
        {0.0, 240.0, 100.0, 80.0},
    };

    const auto leftContent = Overview::buildWorkspaceContentBox({0.0, 0.0, 220.0, 220.0});
    const auto rightContent = Overview::buildWorkspaceContentBox({240.0, 0.0, 220.0, 220.0});
    const auto leftProjected = Overview::projectGlobalBoxesToContent(workspaceOneBoxes, leftContent, 0.0, 0.0);
    const auto rightProjected = Overview::projectGlobalBoxesToContent(workspaceTwoBoxes, rightContent, 0.0, 0.0);

    const std::vector<OverviewLogic::TargetCandidate> targets = {
        {.monitorId = 1, .box = leftProjected[0]},
        {.monitorId = 1, .box = rightProjected[1]},
        {.monitorId = 1, .box = leftProjected[1]},
        {.monitorId = 1, .box = rightProjected[2]},
    };

    const auto next = OverviewLogic::pickTargetIndex(targets, 2, Direction::Up);
    expect_true(next.has_value() && *next == 0,
                "overview movefocus prefers the aligned upward neighbor over a closer diagonal workspace target");
}

void test_overview_target_selection_stops_at_preview_edge() {
    const std::vector<OverviewLogic::TargetCandidate> targets = {
        {.monitorId = 1, .box = {0.0, 0.0, 100.0, 100.0}},
        {.monitorId = 1, .box = {120.0, 0.0, 100.0, 100.0}},
    };

    expect_true(!OverviewLogic::pickTargetIndex(targets, 0, Direction::Left).has_value(),
                "overview movefocus stops when there is no preview to the left");
    expect_true(!OverviewLogic::pickTargetIndex(targets, 1, Direction::Right).has_value(),
                "overview movefocus stops when there is no preview to the right");
}

void test_overview_target_selection_keeps_empty_targets_reachable_in_canvas() {
    const std::vector<OverviewLogic::CanvasTargetCandidate> targets = {
        {.canvasId = 10, .monitorId = 1, .box = {0.0, 0.0, 100.0, 100.0}},
        {.canvasId = 10, .monitorId = 1, .box = {140.0, 0.0, 100.0, 100.0}},
        {.canvasId = 20, .monitorId = 1, .box = {120.0, 0.0, 100.0, 100.0}},
    };

    const auto next = OverviewLogic::pickTargetIndexInCanvas(targets, 0, Direction::Right);
    expect_true(next.has_value() && *next == 1,
                "overview movefocus can select same-canvas empty workspace targets before accept");
}

void test_overview_stack_navigation() {
    for (bool column : {false, true}) {
        std::vector<OverviewLogic::CanvasTargetCandidate> targets = {
            {.canvasId=10, .monitorId=1, .box={0, 0, 100, 100}, .stackId=1},
            {.canvasId=10, .monitorId=1, .box={0, 120, 100, 100}, .stackId=1, .stackActive=true},
            {.canvasId=10, .monitorId=1, .box={140, 0, 100, 100}, .stackId=2},
            {.canvasId=10, .monitorId=1, .box={140, 120, 100, 100}, .stackId=2, .stackActive=true},
            {.canvasId=10, .monitorId=2, .box={280, 0, 100, 220}}, // Empty workspace.
            {.canvasId=20, .monitorId=1, .box={110, 0, 20, 220}, .stackId=3},
        };
        if (column) {
            for (auto& target : targets) {
                std::swap(target.box.x, target.box.y);
                std::swap(target.box.w, target.box.h);
            }
        }
        const auto forward = column ? Direction::Down : Direction::Right;
        const auto backward = column ? Direction::Up : Direction::Left;
        const auto within = column ? Direction::Right : Direction::Down;
        for (size_t index : {0u, 1u}) {
            expect_true(OverviewLogic::pickTargetIndexInCanvas(targets, index, forward) == 3,
                        "moving to a stack selects its remembered active window");
            expect_true(!OverviewLogic::pickTargetIndexInCanvas(targets, index, within),
                        "overview never moves between windows in the same stack");
            expect_true(!OverviewLogic::pickTargetIndexInCanvas(targets, index, backward),
                        "overview stops at the stack boundary");
        }
        expect_true(OverviewLogic::pickTargetIndexInCanvas(targets, 3, backward) == 1,
                    "returning to a stack preserves its active window");
        expect_true(OverviewLogic::pickTargetIndexInCanvas(targets, 3, forward) == 4,
                    "stack navigation still reaches an empty workspace on another monitor");
    }
}

void test_overview_axis_alignment_tolerance() {
    // Projection and symmetric deck insets can leave a few ulps of center
    // drift. A sibling above/below must not steal the first horizontal move.
    for (const double drift : {-1e-10, 1e-10}) {
        const std::vector<OverviewLogic::CanvasTargetCandidate> targets = {
            {.canvasId=10, .monitorId=1, .box={0, 100, 200, 200}},
            {.canvasId=10, .monitorId=1, .box={10+drift, 120, 180, 200}},
            {.canvasId=10, .monitorId=1, .box={-240, 100, 200, 200}},
            {.canvasId=10, .monitorId=1, .box={240, 100, 200, 200}},
        };
        for (size_t index : {0u, 1u}) {
            expect_true(OverviewLogic::pickTargetIndexInCanvas(targets, index, Direction::Left) == 2,
                        "left skips same-stack center drift on first press");
            expect_true(OverviewLogic::pickTargetIndexInCanvas(targets, index, Direction::Right) == 3,
                        "right skips same-stack center drift on first press");
        }
        const std::vector<OverviewLogic::TargetCandidate> siblings = {
            {.monitorId=1, .box=targets[0].box}, {.monitorId=1, .box=targets[1].box},
        };
        expect_true(!OverviewLogic::pickTargetIndex(siblings, 0, Direction::Left) &&
                    !OverviewLogic::pickTargetIndex(siblings, 0, Direction::Right),
                    "horizontal edge stays put instead of moving vertically");
        expect_true(OverviewLogic::pickTargetIndex(siblings, 0, Direction::Down) == 1,
                    "down still selects the stacked sibling");
        auto transposed = siblings;
        for (auto& t : transposed) {
            std::swap(t.box.x,t.box.y);
            std::swap(t.box.w,t.box.h);
        }
        expect_true(!OverviewLogic::pickTargetIndex(transposed, 0, Direction::Up) &&
                    !OverviewLogic::pickTargetIndex(transposed, 0, Direction::Down),
                    "column stack ignores vertical center drift");
        expect_true(OverviewLogic::pickTargetIndex(transposed, 0, Direction::Right) == 1,
                    "right still selects horizontal sibling");
    }
}

void test_overview_empty_accept_plan() {
    const auto plan = OverviewLogic::buildEmptyAcceptPlan(7, 42);
    expect_eq(plan.size(), static_cast<size_t>(2), "overview empty accept plan emits two steps");
    expect_eq(plan[0].type, OverviewLogic::AcceptActionType::FocusMonitor,
              "overview empty accept plan focuses the monitor first");
    expect_eq(plan[0].monitorId, 7, "overview empty accept plan keeps the requested monitor id");
    expect_eq(plan[1].type, OverviewLogic::AcceptActionType::Workspace,
              "overview empty accept plan switches workspace second");
    expect_eq(plan[1].workspaceId, static_cast<OverviewLogic::WorkspaceId>(42),
              "overview empty accept plan keeps the requested workspace id");
}

void test_overview_window_accept_plan() {
    const auto plan = OverviewLogic::buildWorkspaceAcceptPlan(5, 17, false);
    expect_eq(plan.size(), static_cast<size_t>(2), "overview window accept plan emits two steps");
    expect_eq(plan[0].type, OverviewLogic::AcceptActionType::FocusMonitor,
              "overview window accept plan focuses the monitor first");
    expect_eq(plan[0].monitorId, 5, "overview window accept plan keeps the requested monitor id");
    expect_eq(plan[1].type, OverviewLogic::AcceptActionType::Workspace,
              "overview window accept plan switches normal workspace second");
    expect_eq(plan[1].workspaceId, static_cast<OverviewLogic::WorkspaceId>(17),
              "overview window accept plan keeps the requested workspace id");
}

void test_overview_special_workspace_accept_plan() {
    const auto plan = OverviewLogic::buildWorkspaceAcceptPlan(3, 88, true);
    expect_eq(plan.size(), static_cast<size_t>(2), "overview special accept plan emits two steps");
    expect_eq(plan[0].type, OverviewLogic::AcceptActionType::FocusMonitor,
              "overview special accept plan focuses the monitor first");
    expect_eq(plan[1].type, OverviewLogic::AcceptActionType::ToggleSpecialWorkspace,
              "overview special accept plan toggles special workspace second");
    expect_eq(plan[1].workspaceId, static_cast<OverviewLogic::WorkspaceId>(88),
              "overview special accept plan keeps the requested workspace id");
}

} // namespace

void run_overview_logic_tests() {
    test_overview_projection();
    test_monitor_space_orientation();
    test_overview_target_selection_across_monitors();
    test_overview_target_selection_prefers_rendered_neighbor();
    test_overview_target_selection_stops_at_preview_edge();
    test_overview_target_selection_keeps_empty_targets_reachable_in_canvas();
    test_overview_axis_alignment_tolerance();
    test_overview_stack_navigation();
    test_overview_empty_accept_plan();
    test_overview_window_accept_plan();
    test_overview_special_workspace_accept_plan();
}
