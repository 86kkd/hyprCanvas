/**
 * @file layout/canvas/workspace_controller.cpp
 * @brief Runtime implementation of multi-monitor canvas switching and transfer.
 */
#include "layout/canvas/workspace_controller.h"

#include <algorithm>
#include <optional>
#include <vector>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/animation/WorkspaceAnimationController.hpp>
#include <hyprland/src/desktop/state/GlobalWindowController.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/workspace/HLWorkspace.hpp>
#include <spdlog/spdlog.h>

#include "core/canvas_transition.h"
#include "core/core.h"
#include "core/hyprland_runtime.h"
#include "layout/canvas/internal.h"
#include "layout/canvas/layout.h"
#include "layout/canvas/workspace_controller_logic.h"
#include "overview/minimap/runtime.h"

namespace CanvasLayoutState {
namespace {

std::vector<CanvasTileNode> to_tile_nodes(const std::vector<CanvasWorkspaceRecord>& records) {
    std::vector<CanvasTileNode> nodes;
    nodes.reserve(records.size());
    for (const auto& record : records) {
        nodes.push_back({
            .canvasId = record.canvasId,
            .tileX = record.tileX,
            .tileY = record.tileY,
            .ephemeral = record.ephemeral,
            .synthetic = false,
        });
    }
    return nodes;
}

const CanvasWorkspaceRecord* find_canvas(const std::vector<CanvasWorkspaceRecord>& canvases, int canvasId) {
    const auto it = std::find_if(canvases.begin(), canvases.end(), [&](const auto& canvas) {
        return canvas.canvasId == canvasId;
    });
    return it == canvases.end() ? nullptr : &*it;
}

const CanvasWorkspaceMember* find_member(const CanvasWorkspaceRecord& canvas, int monitorId) {
    const auto it = std::find_if(canvas.members.begin(), canvas.members.end(), [&](const auto& member) {
        return member.monitorId == monitorId;
    });
    return it == canvas.members.end() ? nullptr : &*it;
}

PHLWINDOW preferred_window_for_workspace(WORKSPACEID workspaceId) {
    const auto workspace = CanvasCore::HyprlandRuntime::workspaceById(workspaceId);
    if (!workspace)
        return nullptr;

    auto remembered = workspace->getLastFocusedWindow();
    if (remembered && remembered->mapped() && !remembered->isFloating() &&
        CanvasCore::HyprlandRuntime::windowWorkspaceId(remembered) == workspaceId)
        return remembered;

    if (auto* layout = CanvasLayoutInternal::get_canvas_for_workspace(workspaceId)) {
        if (const auto candidate = layout->getNextCandidate(nullptr)) {
            auto window = candidate->window();
            if (window && window->mapped() && !window->isFloating())
                return window;
        }
    }
    return nullptr;
}

} // namespace

CanvasWorkspaceController::CanvasWorkspaceController(CanvasWorkspaceRepository* repository)
    : repository_(repository) {}

CanvasWorkspaceRepository& CanvasWorkspaceController::repo() const {
    return repository_ ? *repository_ : canvasRepository();
}

bool CanvasWorkspaceController::activateCanvas(int canvasId,
                                               int selectedMonitorId,
                                               bool requireSelectedMonitorFocus,
                                               const std::vector<SyntheticCanvasWorkspace>& pending,
                                               const char* context) const {
    auto& canvasRepo = repo();
    const auto previewCanvases = canvasRepo.previewCanvases(pending);
    const auto* targetCanvas = find_canvas(previewCanvases, canvasId);
    if (!targetCanvas)
        return false;

    // Capture the origin before activating any member: native workspace change
    // events can update repository state while the remaining monitors switch.
    const auto sourceCanvas = canvasRepo.find(canvasRepo.activeCanvasId());

    std::vector<CanvasWorkspaceMember> members;
    for (const auto& monitor : CanvasCore::HyprlandRuntime::monitors()) {
        if (!monitor)
            continue;

        if (const auto* member = find_member(*targetCanvas, static_cast<int>(monitor->m_id)))
            members.push_back(*member);
    }
    if (members.empty())
        return false;

    std::stable_sort(members.begin(), members.end(), [&](const auto& lhs, const auto& rhs) {
        const auto lhsSelected = lhs.monitorId == selectedMonitorId;
        const auto rhsSelected = rhs.monitorId == selectedMonitorId;
        if (lhsSelected != rhsSelected)
            return !lhsSelected && rhsSelected;
        return lhs.monitorId < rhs.monitorId;
    });

    auto visitedSelectedMonitor = false;
    for (const auto& member : members) {
        const auto monitor = CanvasCore::HyprlandRuntime::monitorById(member.monitorId);
        if (!monitor)
            continue;

        const auto workspace = CanvasCore::HyprlandRuntime::workspaceById(member.workspaceId);
        const auto outgoingWorkspace = monitor->m_activeWorkspace;
        const auto outgoingOffset = outgoingWorkspace && outgoingWorkspace->m_renderOffset
            ? outgoingWorkspace->m_renderOffset->value() : Vector2D{};
        const auto selectedMember = member.monitorId == selectedMonitorId;
        const auto requireMonitorFocus = selectedMember && requireSelectedMonitorFocus;
        visitedSelectedMonitor = visitedSelectedMonitor || selectedMember;
        if (!CanvasLayoutInternal::focus_monitor_workspace(monitor,
                                                           workspace,
                                                           member.workspaceId,
                                                           requireMonitorFocus,
                                                           context))
            return false;

        const auto incomingWorkspace = monitor->m_activeWorkspace;
        if (sourceCanvas && sourceCanvas->canvasId != canvasId && incomingWorkspace &&
            outgoingWorkspace != incomingWorkspace) {
            const auto offset = CanvasCore::canvas_transition_offset(sourceCanvas->tileX, sourceCanvas->tileY,
                                                                       targetCanvas->tileX, targetCanvas->tileY, monitor->m_size);
            if (offset.x == 0.0 && offset.y == 0.0)
                continue;

            // Keep native timing, damage callbacks and disabled-animation behavior.
            // Override only this transition, without changing the user's config.
            using namespace Animation::Workspace;
            if (outgoingWorkspace) {
                startAnimation(outgoingWorkspace, ANIMATION_TYPE_OUT, true, false, "slide");
                if (outgoingWorkspace->m_renderOffset) {
                    outgoingWorkspace->m_renderOffset->setValue(outgoingOffset);
                    *outgoingWorkspace->m_renderOffset = -offset;
                }
            }
            startAnimation(incomingWorkspace, ANIMATION_TYPE_IN, true, false, "slide");
            if (incomingWorkspace->m_renderOffset) {
                incomingWorkspace->m_renderOffset->setValue(offset);
                *incomingWorkspace->m_renderOffset = Vector2D{};
            }
            spdlog::debug("canvas_transition: source={} target={} monitor={} offset=({}, {})",
                          sourceCanvas->canvasId, canvasId, member.monitorId, offset.x, offset.y);
        }
    }

    if (visitedSelectedMonitor)
        return true;

    const auto fallbackMember = members.front();
    const auto fallbackMonitor = CanvasCore::HyprlandRuntime::monitorById(fallbackMember.monitorId);
    if (!fallbackMonitor)
        return false;

    const auto fallbackWorkspace = CanvasCore::HyprlandRuntime::workspaceById(fallbackMember.workspaceId);
    return CanvasLayoutInternal::focus_monitor_workspace(fallbackMonitor,
                                                         fallbackWorkspace,
                                                         fallbackMember.workspaceId,
                                                         true,
                                                         context);
}

bool CanvasWorkspaceController::finalizeCanvasTarget(WORKSPACEID workspaceId, const PHLWINDOW& window, bool warpCursor) const {
    if (!window)
        return true;

    auto* layout = CanvasLayoutInternal::get_canvas_for_workspace(workspaceId);
    if (!layout)
        return false;

    const auto monitorId = window->monitorID();
    layout->onWindowFocusChange(window);
    layout->recalculateMonitor(monitorId);
    return CanvasLayoutInternal::switch_to_window(window, warpCursor);
}

bool CanvasWorkspaceController::applyDesktopSwitch(int previousCanvasId,
                                                   int preferredMonitorId,
                                                   int targetCanvasId,
                                                   const std::vector<SyntheticCanvasWorkspace>& pending,
                                                   const char* context) {
    auto& canvasRepo = repo();
    if (canvasRepo.find(targetCanvasId))
        canvasRepo.ensureCanvasHasVisibleMembers(targetCanvasId);

    const auto previewCanvases = canvasRepo.previewCanvases(pending);
    const auto* targetCanvas = find_canvas(previewCanvases, targetCanvasId);
    if (!targetCanvas)
        return false;

    const auto* member = find_member(*targetCanvas, preferredMonitorId);
    if (!member && !targetCanvas->members.empty())
        member = &targetCanvas->members.front();

    const auto targetMonitorId = member ? member->monitorId : preferredMonitorId;
    const auto targetWorkspaceId = member ? member->workspaceId : WORKSPACE_INVALID;
    const auto targetWindow = targetWorkspaceId != WORKSPACE_INVALID
        ? preferred_window_for_workspace(targetWorkspaceId)
        : nullptr;

    if (!activateCanvas(targetCanvasId, targetMonitorId, !targetWindow, pending, context))
        return false;

    if (targetWindow && !finalizeCanvasTarget(targetWorkspaceId, targetWindow, true))
        return false;

    if (!pending.empty())
        canvasRepo.commitSyntheticCanvases(pending, targetCanvasId);
    else
        canvasRepo.markActive(targetCanvasId);

    Overview::Minimap::notifySwitch(previousCanvasId, canvasRepo.activeCanvasId());
    return true;
}

bool CanvasWorkspaceController::switchByDirection(Direction direction) {
    auto& canvasRepo = repo();
    const int currentCanvasId = canvasRepo.ensureCurrentVisibleCanvas();
    if (currentCanvasId < 0)
        return false;

    canvasRepo.refreshTemporaryCanvases();
    const auto plan = planDirectionalCanvasMove(to_tile_nodes(canvasRepo.canvases()), currentCanvasId, direction);
    if (plan.action == CanvasNavigationAction::None)
        return false;

    const auto monitor = CanvasCore::monitorForKeyboardAction();
    const int preferredMonitorId = monitor ? static_cast<int>(monitor->m_id) : -1;

    std::vector<SyntheticCanvasWorkspace> pending;
    int targetCanvasId = plan.targetCanvasId;
    if (plan.action == CanvasNavigationAction::CreateSynthetic) {
        pending.push_back(canvasRepo.buildSyntheticCanvas(plan.createDirection, plan.anchorCanvasId));
        targetCanvasId = pending.back().canvas.canvasId;
    }

    return applyDesktopSwitch(currentCanvasId, preferredMonitorId, targetCanvasId, pending, "switch_canvas_direction");
}

bool CanvasWorkspaceController::switchByNumber(int number) {
    auto& canvasRepo = repo();
    const int currentCanvasId = canvasRepo.ensureCurrentVisibleCanvas();
    if (currentCanvasId < 0)
        return false;

    canvasRepo.refreshTemporaryCanvases();
    const auto plan = planNumberedCanvasMove(to_tile_nodes(canvasRepo.canvases()), currentCanvasId, number);
    if (plan.action == CanvasNavigationAction::None)
        return false;

    const auto monitor = CanvasCore::monitorForKeyboardAction();
    const int preferredMonitorId = monitor ? static_cast<int>(monitor->m_id) : -1;

    std::vector<SyntheticCanvasWorkspace> pending;
    int targetCanvasId = plan.targetCanvasId;
    if (plan.action == CanvasNavigationAction::CreateSynthetic) {
        pending.push_back(canvasRepo.buildSyntheticCanvas(plan.createDirection, plan.anchorCanvasId));
        targetCanvasId = pending.back().canvas.canvasId;
    }

    return applyDesktopSwitch(currentCanvasId, preferredMonitorId, targetCanvasId, pending, "switch_canvas_number");
}

bool CanvasWorkspaceController::moveFocusedWindowToCanvas(Direction direction) {
    if (direction != Direction::Left && direction != Direction::Right &&
        direction != Direction::Up && direction != Direction::Down)
        return false;

    const auto window = Desktop::focusState()->window();
    if (!window || !window->mapped() || window->isFloating() || window->onSpecialWorkspace())
        return false;

    const auto sourceId = CanvasCore::HyprlandRuntime::windowWorkspaceId(window);
    const auto source = CanvasCore::HyprlandRuntime::workspaceById(sourceId);
    auto* sourceLayout = CanvasLayoutInternal::get_canvas_for_workspace(sourceId);
    if (!source || !sourceLayout || !sourceLayout->isWindowTiled(window))
        return false;

    const auto monitor = CanvasCore::HyprlandRuntime::monitorById(window->monitorID());
    if (!monitor)
        return false;

    auto& canvasRepo = repo();
    const int previousCanvasId = canvasRepo.ensureCurrentVisibleCanvas();
    if (previousCanvasId < 0)
        return false;

    canvasRepo.refreshTemporaryCanvases();
    const auto plan = planDirectionalCanvasMove(to_tile_nodes(canvasRepo.canvases()), previousCanvasId, direction);
    if (plan.action == CanvasNavigationAction::None)
        return false;

    std::vector<SyntheticCanvasWorkspace> pending;
    int targetCanvasId = plan.targetCanvasId;
    if (plan.action == CanvasNavigationAction::CreateSynthetic) {
        pending.push_back(canvasRepo.buildSyntheticCanvas(plan.createDirection, plan.anchorCanvasId));
        targetCanvasId = pending.back().canvas.canvasId;
    } else {
        canvasRepo.ensureCanvasHasVisibleMembers(targetCanvasId);
    }

    const auto previewCanvases = canvasRepo.previewCanvases(pending);
    const auto* targetCanvas = find_canvas(previewCanvases, targetCanvasId);
    const auto* destMember = targetCanvas ? find_member(*targetCanvas, static_cast<int>(monitor->m_id)) : nullptr;
    if (!destMember || destMember->workspaceId == sourceId)
        return false;

    auto target = CanvasCore::HyprlandRuntime::workspaceById(destMember->workspaceId);
    if (!target) {
        target = State::workspaceState()->createNumbered(
            Workspace::SWorkspaceNumberedID(static_cast<Workspace::WorkspaceIDContainer>(destMember->workspaceId)),
            monitor);
    }
    if (!target || target->monitorID() != monitor->m_id ||
        !CanvasLayoutInternal::get_canvas_for_workspace(destMember->workspaceId))
        return false;

    const auto previousTargetWindow = target->getLastFocusedWindow();
    // The native silent dispatcher deliberately refocuses another source
    // window. Transfer ownership directly, then perform one canvas activation.
    Desktop::globalWindowController()->moveWindowToWorkspace(window, target);
    if (CanvasCore::HyprlandRuntime::windowWorkspaceId(window) != destMember->workspaceId)
        return false;

    auto* liveSourceLayout = CanvasLayoutInternal::get_canvas_for_workspace(sourceId);
    const auto remaining = liveSourceLayout ? liveSourceLayout->getNextCandidate(nullptr) : nullptr;
    source->rememberFocusedWindow(remaining ? remaining->window() : nullptr);
    target->rememberFocusedWindow(window);

    const bool activated =
        activateCanvas(targetCanvasId, static_cast<int>(monitor->m_id), false, pending, "move_window_to_canvas") &&
        finalizeCanvasTarget(destMember->workspaceId, window, true);
    if (!activated) {
        Desktop::globalWindowController()->moveWindowToWorkspace(window, source);
        source->rememberFocusedWindow(window);
        target->rememberFocusedWindow(previousTargetWindow);
        (void)activateCanvas(previousCanvasId, static_cast<int>(monitor->m_id), false, {}, "move_window_to_canvas_rollback");
        (void)finalizeCanvasTarget(sourceId, window, false);
        return false;
    }

    if (!pending.empty())
        canvasRepo.commitSyntheticCanvases(pending, targetCanvasId);
    else
        canvasRepo.markActive(targetCanvasId);

    Overview::Minimap::notifySwitch(previousCanvasId, targetCanvasId);
    return true;
}

CanvasCore::ServiceSlot<CanvasWorkspaceController>& workspaceControllerSlot() {
    static CanvasCore::ServiceSlot<CanvasWorkspaceController> slot;
    return slot;
}

CanvasWorkspaceController& workspaceController() {
    return workspaceControllerSlot().get();
}

} // namespace CanvasLayoutState
