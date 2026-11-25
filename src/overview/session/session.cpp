/**
 * @file session.cpp
 * @brief Global overview-session construction and logical target navigation.
 *
 * This file builds a monitor-scoped preview model from the current set of
 * tiled windows, keeps a logical selection independent from Hyprland focus,
 * and resolves the final workspace/window jump only when overview closes with
 * acceptance.
 *
 * Reading guide:
 * - `session.cpp` is the control-flow layer for overview
 * - `model.cpp` builds the read-only target graph
 * - `scene.cpp` reshapes that graph into render DTOs
 * - `effects.cpp` performs real Hyprland side effects on accept/restore
 */
#include "overview/session/session.h"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/state/GlobalWindowController.hpp>
#include <hyprland/src/animation/WorkspaceAnimationController.hpp>
#include <hyprland/src/workspace/HLWorkspace.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <spdlog/spdlog.h>

#include "core/hyprland_runtime.h"
#include "core/canvas_transition.h"
#include "layout/canvas/internal.h"
#include "layout/canvas/layout.h"
#include "overview/render/state.h"
#include "overview/minimap/runtime.h"
#include "overview/navigation/logic.h"
#include "overview/session/effects.h"
#include "overview/navigation/selection.h"

namespace Overview {
namespace {

// When overview opens with no existing targets selected, prefer the monitor
// under the cursor so a synthetic empty target appears where the user is
// already interacting.
const MonitorRegion* initial_empty_region(const Model& model) {
    if (model.monitors().empty())
        return nullptr;

    const auto cursorMonitor = ScrollerCore::HyprlandRuntime::monitorFromCursor();
    if (!cursorMonitor)
        return &model.monitors().front();

    const auto* region = model.regionForMonitor(cursorMonitor->m_id);
    return region ? region : &model.monitors().front();
}

// Synthetic empty targets are intentionally smaller than the full monitor box so
// they render as a clear "new workspace candidate" instead of a full-screen fill.
Target initial_empty_target(const MonitorRegion& region) {
    return makeEmptyTarget(INVALID_CANVAS_ID,
                           SessionEffects::nextWorkspaceId(),
                           region.monitorId,
                           false,
                           {region.box.x + region.box.w * 0.16,
                            region.box.y + region.box.h * 0.16,
                            region.box.w * 0.68,
                            region.box.h * 0.68},
                           true);
}

// Initial selection prefers the concrete origin window when it is still present
// in the rebuilt model.
bool try_select_origin_window(Model& model, int canvasId) {
    const auto originWindow = model.origin().window;
    if (!originWindow)
        return false;

    const auto ref = model.findByWindow(originWindow, canvasId);
    if (!ref)
        return false;

    model.setSelection(*ref);
    return true;
}

// If the origin window disappeared, fall back to "same workspace" before using
// an arbitrary first target.
bool try_select_origin_workspace(Model& model, int canvasId) {
    const auto originWorkspace = model.origin().workspaceId;
    if (originWorkspace == INVALID_WORKSPACE_ID)
        return false;

    const auto ref = model.findByWorkspace(originWorkspace, canvasId);
    if (!ref)
        return false;

    model.setSelection(*ref);
    return true;
}

} // namespace

bool Session::active() const {
    return active_;
}

const Model& Session::model() const {
    return model_;
}

void Session::damageMonitors() const {
    for (const auto& region : model_.monitors()) {
        if (region.monitor)
            g_pHyprRenderer->damageMonitor(region.monitor);
    }
}

Session& session() {
    static Session instance;
    return instance;
}

void Session::clear() {
    active_ = false;
    inputHandling_.reset();
    model_.clear();
    originCanvasId_ = INVALID_CANVAS_ID;
    viewCanvasId_ = INVALID_CANVAS_ID;
    pendingCanvases_.clear();
}

void Session::markInputHandled() {
    inputHandling_.markHandled();
}

bool Session::consumeInputHandled(bool released) {
    return inputHandling_.consume(released);
}

bool Session::selectInitialTarget() {
    // `chooseInitialSelectionChoice` stays pure and testable. This method turns
    // that decision into actual model mutation in one place.
    const auto choice = chooseInitialSelectionChoice(!model_.targetGraph().empty(),
                                                     model_.findByWindow(model_.origin().window, originCanvasId_).has_value(),
                                                     model_.findByWorkspace(model_.origin().workspaceId, originCanvasId_).has_value(),
                                                     model_.firstTarget().has_value(),
                                                     initial_empty_region(model_) != nullptr);

    switch (choice) {
        case InitialSelectionChoice::OriginWindow:
            return try_select_origin_window(model_, originCanvasId_);
        case InitialSelectionChoice::OriginWorkspace:
            return try_select_origin_workspace(model_, originCanvasId_);
        case InitialSelectionChoice::FirstTarget:
            if (const auto ref = model_.firstTarget()) {
                model_.setSelection(*ref);
                return true;
            }
            return false;
        case InitialSelectionChoice::InitialEmpty:
            if (const auto* region = initial_empty_region(model_)) {
                model_.setSyntheticSelection(initial_empty_target(*region));
                return true;
            }
            return false;
        case InitialSelectionChoice::None:
        default:
            return false;
    }
}

bool Session::selectCanvas(int canvasId, std::optional<int> preferredMonitorId) {
    const auto ref = model_.firstTargetInCanvas(canvasId, preferredMonitorId);
    if (!ref)
        return false;

    // Canvas navigation should restore the workspace's remembered window,
    // not arbitrarily select the first stack after a transfer or departure.
    if (const auto* target = model_.resolve(*ref)) {
        const auto workspace = ScrollerCore::HyprlandRuntime::workspaceById(target->workspaceId);
        const auto remembered = workspace ? workspace->getLastFocusedWindow() : nullptr;
        if (remembered && ScrollerCore::HyprlandRuntime::windowWorkspaceId(remembered) == target->workspaceId) {
            if (const auto rememberedRef = model_.findByWindow(remembered, canvasId)) {
                model_.setSelection(*rememberedRef);
                return true;
            }
        }
    }
    model_.setSelection(*ref);
    return true;
}

void Session::open() {
    if (active_)
        return;

    // Open happens in three phases:
    // 1. capture enough origin state to restore or accept later
    // 2. prepare/rebuild the read-only logical overview model
    // 3. choose an initial selection and start damaging monitors for rendering
    const auto origin = SessionEffects::captureOrigin();
    originCanvasId_ = CanvasLayoutState::canvasRepository().ensureCurrentVisibleCanvas();
    viewCanvasId_ = originCanvasId_;
    model_.setOrigin(origin.monitorId, origin.workspaceId, origin.window);
    pendingCanvases_.clear();
    SessionEffects::prepareSnapshots();
    model_.rebuild(pendingCanvases_, viewCanvasId_);
    if (!selectInitialTarget()) {
        clear();
        spdlog::warn("overview_open: no targets available");
        return;
    }

    if (const auto selectionCanvasId = model_.selectionCanvasId();
        selectionCanvasId && *selectionCanvasId != viewCanvasId_) {
        viewCanvasId_ = *selectionCanvasId;
        model_.rebuild(pendingCanvases_, viewCanvasId_);
        (void)selectCanvas(viewCanvasId_, model_.origin().monitorId);
    }

    active_ = true;
    damageMonitors();
    const auto* selection = model_.selection();
    spdlog::info("overview_open: origin_workspace={} origin_window={} monitors={} selection_workspace={} selection_window={} synthetic={}",
                 model_.origin().workspaceId,
                 static_cast<const void*>(model_.origin().window ? model_.origin().window.get() : nullptr),
                 model_.monitors().size(),
                 selection ? selection->workspaceId : INVALID_WORKSPACE_ID,
                 static_cast<const void*>(selection && selection->window ? selection->window.get() : nullptr),
                 selection ? selection->synthetic : false);
}

std::optional<TargetRef> Session::findBestTarget(Direction direction) const {
    if (!model_.selectionRef())
        return std::nullopt;

    const auto& targetGraph = model_.targetGraph();
    if (targetGraph.empty())
        return std::nullopt;

    // Convert the richer overview target graph into the minimal pure-routing
    // representation that `OverviewLogic` understands. Movefocus stays inside
    // the selected canvas, but empty-workspace targets remain selectable.
    std::vector<OverviewLogic::CanvasTargetCandidate> candidates;
    std::vector<TargetRef> refs;
    candidates.reserve(targetGraph.size());
    refs.reserve(targetGraph.size());

    auto currentIndex = std::optional<size_t>{};
    for (const auto& node : targetGraph) {
        const auto* target = model_.resolve(node.ref);
        if (!target)
            continue;

        candidates.push_back({
            .canvasId = target->canvasId,
            .monitorId = target->monitorId,
            .box = target->box,
            .stackId = reinterpret_cast<uintptr_t>(target->stackActiveWindow.get()),
            .stackActive = target->window && target->window == target->stackActiveWindow,
        });
        refs.push_back(node.ref);

        if (node.ref == *model_.selectionRef())
            currentIndex = refs.size() - 1;
    }

    if (!currentIndex)
        return std::nullopt;

    const auto nextIndex = OverviewLogic::pickTargetIndexInCanvas(candidates, *currentIndex, direction);
    if (!nextIndex)
        return std::nullopt;

    return refs[*nextIndex];
}

bool Session::moveSelection(Direction direction) {
    if (!active_ || !model_.selection())
        return false;

    if (const auto targetRef = findBestTarget(direction)) {
        model_.setSelection(*targetRef);
        const auto* selection = model_.selection();
        spdlog::info("overview_move: direction={} canvas={} workspace={} window={} synthetic={}",
                     ScrollerCore::direction_name(direction),
                     selection ? selection->canvasId : INVALID_CANVAS_ID,
                     selection ? selection->workspaceId : INVALID_WORKSPACE_ID,
                     static_cast<const void*>(selection && selection->window ? selection->window.get() : nullptr),
                     selection ? selection->synthetic : false);
        damageMonitors();
        return true;
    }

    // Overview navigation is preview-only: reaching an edge should stop rather
    // than synthesizing a brand-new workspace candidate.
    return false;
}

bool Session::moveCanvasSelection(Direction direction) {
    const auto* currentSelection = model_.selection();
    if (!active_ || !currentSelection)
        return false;

    const auto currentCanvasId = model_.selectionCanvasId();
    if (!currentCanvasId)
        return false;

    const auto preferredMonitorId = currentSelection->monitorId;
    if (const auto nextCanvasId = model_.findAdjacentCanvas(*currentCanvasId, direction)) {
        std::erase_if(pendingCanvases_, [&](const auto& pending) { return pending.canvas.canvasId != *nextCanvasId; });
        viewCanvasId_ = *nextCanvasId;
        model_.rebuild(pendingCanvases_, viewCanvasId_);
        renderState().rebuildPreviewAnimations(model_);
        if (!selectCanvas(*nextCanvasId, preferredMonitorId))
            return false;

        const auto* selection = model_.selection();
        spdlog::info("overview_move_canvas: direction={} canvas={} workspace={}",
                     ScrollerCore::direction_name(direction),
                     selection ? selection->canvasId : INVALID_CANVAS_ID,
                     selection ? selection->workspaceId : INVALID_WORKSPACE_ID);
        damageMonitors();
        return true;
    }

    auto& canvasRepo = CanvasLayoutState::canvasRepository();
    canvasRepo.refreshTemporaryCanvases();
    const auto current = canvasRepo.find(*currentCanvasId);
    // A blank provisional page is already the insertion point. Extending it
    // again would leave an empty hole when a later page receives a window.
    if (!current || current->ephemeral)
        return false;
    pendingCanvases_.clear();
    pendingCanvases_.push_back(canvasRepo.buildSyntheticCanvas(direction, *currentCanvasId, pendingCanvases_));
    viewCanvasId_ = pendingCanvases_.back().canvas.canvasId;
    model_.rebuild(pendingCanvases_, viewCanvasId_);
    renderState().rebuildPreviewAnimations(model_);
    if (!selectCanvas(pendingCanvases_.back().canvas.canvasId, preferredMonitorId))
        return false;

    const auto* selection = model_.selection();
    spdlog::info("overview_create_canvas: direction={} canvas={} workspace={}",
                 ScrollerCore::direction_name(direction),
                 selection ? selection->canvasId : INVALID_CANVAS_ID,
                 selection ? selection->workspaceId : INVALID_WORKSPACE_ID);
    damageMonitors();
    return true;
}

// Move the focused tiled window to the same physical monitor in an adjacent
// canvas. Reuse preview routing without accepting the old target window first:
// activating it would briefly steal focus and rewrite source focus history.
bool Session::moveWindowToCanvas(Direction direction) {
    if (active_ || (direction != Direction::Left && direction != Direction::Right &&
                    direction != Direction::Up && direction != Direction::Down))
        return false;
    const auto window = Desktop::focusState()->window();
    if (!window || !window->mapped() || window->isFloating() || window->onSpecialWorkspace())
        return false;
    const auto sourceId = ScrollerCore::HyprlandRuntime::windowWorkspaceId(window);
    const auto source = ScrollerCore::HyprlandRuntime::workspaceById(sourceId);
    auto* sourceLayout = CanvasLayoutInternal::get_canvas_for_workspace(sourceId);
    if (!source || !sourceLayout || !sourceLayout->isWindowTiled(window))
        return false;
    const auto monitor = ScrollerCore::HyprlandRuntime::monitorById(window->monitorID());
    if (!monitor)
        return false;

    open();
    if (!active_ || !moveCanvasSelection(direction) || !model_.selection()) {
        dismiss();
        return false;
    }
    const auto destination = *model_.selection();
    if (destination.monitorId != monitor->m_id || destination.workspaceId == sourceId) {
        dismiss();
        return false;
    }
    auto target = ScrollerCore::HyprlandRuntime::workspaceById(destination.workspaceId);
    if (!target)
        target = State::workspaceState()->createNumbered(
            Workspace::SWorkspaceNumberedID(static_cast<Workspace::WorkspaceIDContainer>(destination.workspaceId)), monitor);
    if (!target || target->monitorID() != monitor->m_id ||
        !CanvasLayoutInternal::get_canvas_for_workspace(destination.workspaceId)) {
        dismiss();
        return false;
    }

    const auto previousTargetWindow = target->getLastFocusedWindow();
    // The native silent dispatcher deliberately refocuses another source
    // window. Transfer ownership directly, then perform one canvas activation.
    Desktop::globalWindowController()->moveWindowToWorkspace(window, target);
    if (ScrollerCore::HyprlandRuntime::windowWorkspaceId(window) != destination.workspaceId) {
        dismiss();
        return false;
    }
    const auto remaining = sourceLayout->getNextCandidate(nullptr);
    source->rememberFocusedWindow(remaining ? remaining->window() : nullptr);
    target->rememberFocusedWindow(window);

    model_.rebuild(pendingCanvases_, viewCanvasId_);
    const auto moved = model_.findByWindow(window, destination.canvasId);
    if (!moved) {
        Desktop::globalWindowController()->moveWindowToWorkspace(window, source);
        source->rememberFocusedWindow(window);
        target->rememberFocusedWindow(previousTargetWindow);
        restoreOrigin();
        dismiss();
        return false;
    }
    model_.setSelection(*moved);
    const int previousCanvas = originCanvasId_;
    const bool accepted = acceptSelection();
    if (!accepted) {
        Desktop::globalWindowController()->moveWindowToWorkspace(window, source);
        source->rememberFocusedWindow(window);
        target->rememberFocusedWindow(previousTargetWindow);
        restoreOrigin();
    }
    dismiss();
    if (accepted)
        Minimap::notifySwitch(previousCanvas, destination.canvasId);
    return accepted;
}

// Map clicks select an existing canvas by identity, never by native workspace
// or by a number limited to ten. Accept/cancel remains owned by the session.
bool Session::previewCanvas(int canvasId) {
    if (!active_)
        return false;
    const auto& graph = model_.canvasGraph();
    if (std::none_of(graph.begin(), graph.end(), [&](const auto& node) { return node.canvasId == canvasId; }))
        return false;
    const auto* selection = model_.selection();
    const int monitorId = selection ? selection->monitorId : model_.origin().monitorId;
    std::erase_if(pendingCanvases_, [&](const auto& pending) { return pending.canvas.canvasId != canvasId; });
    viewCanvasId_ = canvasId;
    model_.rebuild(pendingCanvases_, viewCanvasId_);
    renderState().rebuildPreviewAnimations(model_);
    const bool selected = selectCanvas(canvasId, monitorId);
    damageMonitors();
    return selected;
}

bool Session::selectCanvasNumber(int number) {
    if (!active_ || number < 1 || number > 10)
        return false;

    // Number keys address stable canvas creation order, never native workspace
    // IDs (which are separate per monitor). Missing groups are created lazily.
    auto canvases = model_.canvasGraph();
    std::sort(canvases.begin(), canvases.end(), [](const auto& a, const auto& b) {
        return a.canvasId < b.canvasId;
    });
    if (canvases.empty())
        return false;
    auto& repo = CanvasLayoutState::canvasRepository();
    repo.refreshTemporaryCanvases();
    int targetId = canvases.back().canvasId;
    if (number > static_cast<int>(canvases.size())) {
        // Numbered navigation cannot bypass the one-empty-page boundary.
        if (number != static_cast<int>(canvases.size()) + 1)
            return false;
        const auto anchor = repo.find(targetId);
        const auto currentId = model_.selectionCanvasId();
        const auto current = currentId ? repo.find(*currentId) : std::nullopt;
        if (!anchor || anchor->ephemeral || !current || current->ephemeral)
            return false;
        pendingCanvases_.push_back(repo.buildSyntheticCanvas(Direction::Right, targetId, pendingCanvases_));
        targetId = pendingCanvases_.back().canvas.canvasId;
    } else {
        targetId = canvases[number - 1].canvasId;
        std::erase_if(pendingCanvases_, [&](const auto& pending) { return pending.canvas.canvasId != targetId; });
    }
    const auto* selection = model_.selection();
    const auto monitorId = selection ? selection->monitorId : model_.origin().monitorId;
    viewCanvasId_ = targetId;
    model_.rebuild(pendingCanvases_, viewCanvasId_);
    renderState().rebuildPreviewAnimations(model_);
    const bool selected = selectCanvas(targetId, monitorId);
    damageMonitors();
    return selected;
}

bool Session::activateCanvas(int canvasId, int selectedMonitorId, bool requireSelectedMonitorFocus, const char* context) {
    auto& canvasRepo = CanvasLayoutState::canvasRepository();
    const auto previewCanvases = canvasRepo.previewCanvases(pendingCanvases_);
    const auto canvasIt = std::find_if(previewCanvases.begin(), previewCanvases.end(), [&](const auto& canvas) {
        return canvas.canvasId == canvasId;
    });
    if (canvasIt == previewCanvases.end())
        return false;

    // Capture the origin before activating any member: native workspace change
    // events can update repository state while the remaining monitors switch.
    const auto sourceCanvas = canvasRepo.find(canvasRepo.activeCanvasId());

    std::vector<CanvasLayoutState::CanvasWorkspaceMember> members;
    for (const auto& monitor : ScrollerCore::HyprlandRuntime::monitors()) {
        if (!monitor)
            continue;

        const auto memberIt = std::find_if(canvasIt->members.begin(), canvasIt->members.end(), [&](const auto& member) {
            return member.monitorId == monitor->m_id;
        });
        if (memberIt != canvasIt->members.end())
            members.push_back(*memberIt);
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
        const auto monitor = ScrollerCore::HyprlandRuntime::monitorById(member.monitorId);
        if (!monitor)
            continue;

        const auto workspace = ScrollerCore::HyprlandRuntime::workspaceById(member.workspaceId);
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
            const auto offset = ScrollerCore::canvas_transition_offset(sourceCanvas->tileX, sourceCanvas->tileY,
                                                                       canvasIt->tileX, canvasIt->tileY, monitor->m_size);
            if (offset.x == 0.0 && offset.y == 0.0)
                continue;

            // Keep native timing, damage callbacks and disabled-animation behavior.
            // Override only this transition, without changing the user's config.
            using namespace Animation::Workspace;
            if (outgoingWorkspace) {
                startAnimation(outgoingWorkspace, ANIMATION_TYPE_OUT, true, false, "slide");
                outgoingWorkspace->m_renderOffset->setValue(outgoingOffset);
                *outgoingWorkspace->m_renderOffset = -offset;
            }
            startAnimation(incomingWorkspace, ANIMATION_TYPE_IN, true, false, "slide");
            incomingWorkspace->m_renderOffset->setValue(offset);
            *incomingWorkspace->m_renderOffset = Vector2D{};
            spdlog::debug("canvas_transition: source={} target={} monitor={} offset=({}, {})",
                          sourceCanvas->canvasId, canvasId, member.monitorId, offset.x, offset.y);
        }
    }

    if (visitedSelectedMonitor)
        return true;

    const auto fallbackMember = members.front();
    const auto fallbackMonitor = ScrollerCore::HyprlandRuntime::monitorById(fallbackMember.monitorId);
    if (!fallbackMonitor)
        return false;

    const auto fallbackWorkspace = ScrollerCore::HyprlandRuntime::workspaceById(fallbackMember.workspaceId);
    return CanvasLayoutInternal::focus_monitor_workspace(fallbackMonitor,
                                                         fallbackWorkspace,
                                                         fallbackMember.workspaceId,
                                                         true,
                                                         context);
}

bool Session::finalizeCanvasTarget(const Target& target, bool warpCursor) {
    if (!target.window)
        return true;

    auto* layout = CanvasLayoutInternal::get_canvas_for_workspace(target.workspaceId);
    if (!layout)
        return false;

    const auto monitorId = target.window->monitorID();
    layout->onWindowFocusChange(target.window);
    layout->recalculateMonitor(monitorId);
    return CanvasLayoutInternal::switch_to_window(target.window, warpCursor);
}

bool Session::finalizeCanvasOrigin(const OriginState& origin) {
    if (!origin.window || !origin.window->mapped())
        return true;

    const auto target = Target{
        .type = TargetType::Window,
        .workspaceId = origin.workspaceId,
        .monitorId = origin.monitorId,
        .window = origin.window,
        .box = {},
        .sourceBox = {},
        .synthetic = false,
    };
    return finalizeCanvasTarget(target, false);
}

bool Session::acceptSelection() {
    const auto* selection = model_.selection();
    if (!selection)
        return false;

    auto selectionCopy = *selection;
    if (selectionCopy.canvasId != INVALID_CANVAS_ID) {
        auto& canvasRepo = CanvasLayoutState::canvasRepository();
        if (canvasRepo.find(selectionCopy.canvasId))
            canvasRepo.ensureCanvasHasVisibleMembers(selectionCopy.canvasId);

        const auto previewCanvases = canvasRepo.previewCanvases(pendingCanvases_);
        const auto canvasIt = std::find_if(previewCanvases.begin(), previewCanvases.end(), [&](const auto& canvas) {
            return canvas.canvasId == selectionCopy.canvasId;
        });
        if (canvasIt != previewCanvases.end()) {
            const auto memberIt = std::find_if(canvasIt->members.begin(), canvasIt->members.end(), [&](const auto& member) {
                return member.monitorId == selectionCopy.monitorId;
            });
            if (memberIt != canvasIt->members.end()) {
                selectionCopy.workspaceId = memberIt->workspaceId;
                selectionCopy.specialWorkspace = memberIt->special;
            }
        }

        const auto requireSelectedMonitorFocus = selectionCopy.type != TargetType::Window || !selectionCopy.window;
        if (!activateCanvas(selectionCopy.canvasId, selectionCopy.monitorId, requireSelectedMonitorFocus, "overview_accept_canvas"))
            return false;

        if (!finalizeCanvasTarget(selectionCopy, true))
            return false;

        if (!pendingCanvases_.empty())
            canvasRepo.commitSyntheticCanvases(pendingCanvases_, selectionCopy.canvasId);
        else
            canvasRepo.markActive(selectionCopy.canvasId);
        pendingCanvases_.clear();
        return true;
    }

    return SessionEffects::acceptTarget(selectionCopy);
}

bool Session::restoreOrigin() {
    if (originCanvasId_ != INVALID_CANVAS_ID) {
        auto& canvasRepo = CanvasLayoutState::canvasRepository();
        if (canvasRepo.find(originCanvasId_))
            canvasRepo.ensureCanvasHasVisibleMembers(originCanvasId_);
        const auto& origin = model_.origin();
        const auto restoreWindow = origin.window && origin.window->mapped();
        if (!activateCanvas(originCanvasId_, origin.monitorId, !restoreWindow, "overview_restore_canvas"))
            return false;
        return finalizeCanvasOrigin(origin);
    }

    return SessionEffects::restoreOrigin(model_.origin());
}

void Session::close(bool acceptSelectionFlag) {
    if (!active_)
        return;

    // Overview always resolves exactly once on close: either accept the current
    // selection or restore the remembered origin. Only after that do we clear
    // the logical model and remove the overlay.
    auto resolved = acceptSelectionFlag
        ? acceptSelection()
        : restoreOrigin();
    if (acceptSelectionFlag && !resolved) {
        spdlog::warn("overview_close: accept failed, restoring origin");
        resolved = restoreOrigin();
    }

    const auto* selection = model_.selection();
    spdlog::info("overview_close: accepted={} resolved={} selection_canvas={} selection_workspace={} selection_window={}",
                 acceptSelectionFlag,
                 resolved,
                 selection ? selection->canvasId : INVALID_CANVAS_ID,
                 selection ? selection->workspaceId : INVALID_WORKSPACE_ID,
                 static_cast<const void*>(selection && selection->window ? selection->window.get() : nullptr));
    damageMonitors();
    clear();
    // Explicit overview exit also ends its map, including any earlier HUD timer.
    Minimap::reset();
}

void Session::dismiss() {
    Minimap::reset();
    if (!active_)
        return;

    // Dismiss differs from close(false): it simply tears the overlay down
    // without running accept/restore side effects.
    const auto* selection = model_.selection();
    spdlog::info("overview_dismiss: selection_canvas={} selection_workspace={} selection_window={}",
                 selection ? selection->canvasId : INVALID_CANVAS_ID,
                 selection ? selection->workspaceId : INVALID_WORKSPACE_ID,
                 static_cast<const void*>(selection && selection->window ? selection->window.get() : nullptr));
    damageMonitors();
    clear();
}

} // namespace Overview
