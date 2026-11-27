/**
 * @file overview/session/session.cpp
 * @brief Global read-only overview preview session spanning all canvases and monitors.
 *
 * This file builds a monitor-scoped preview model from the current set of
 * tiled windows, keeps a logical selection independent from Hyprland focus,
 * and delegates final canvas activation to `CanvasWorkspaceController` only
 * when overview closes with acceptance or origin restore.
 */
#include "overview/session/session.h"

#include <algorithm>
#include <vector>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/workspace/HLWorkspace.hpp>
#include <spdlog/spdlog.h>

#include "core/hyprland_runtime.h"
#include "layout/canvas/workspace_controller.h"
#include "layout/canvas/workspace_controller_logic.h"
#include "overview/minimap/runtime.h"
#include "overview/navigation/logic.h"
#include "overview/navigation/selection.h"
#include "overview/render/state.h"
#include "overview/session/effects.h"

namespace Overview {
namespace {

const MonitorRegion* initial_empty_region(const Model& model) {
    if (model.monitors().empty())
        return nullptr;

    const auto cursorMonitor = CanvasCore::HyprlandRuntime::monitorFromCursor();
    if (!cursorMonitor)
        return &model.monitors().front();

    const auto* region = model.regionForMonitor(cursorMonitor->m_id);
    return region ? region : &model.monitors().front();
}

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

std::vector<CanvasLayoutState::CanvasTileNode> canvas_tiles_from_model(
    const Model& model,
    const CanvasLayoutState::CanvasWorkspaceRepository& repo) {
    const auto& graph = model.canvasGraph();
    std::vector<CanvasLayoutState::CanvasTileNode> nodes;
    nodes.reserve(graph.size());
    for (const auto& entry : graph) {
        const auto record = repo.find(entry.canvasId);
        nodes.push_back({
            .canvasId = entry.canvasId,
            .tileX = entry.tileX,
            .tileY = entry.tileY,
            .ephemeral = entry.synthetic || (record && record->ephemeral),
            .synthetic = entry.synthetic,
        });
    }
    return nodes;
}

} // namespace

Session::Session(CanvasLayoutState::CanvasWorkspaceRepository* repository,
                 CanvasLayoutState::CanvasWorkspaceController* controller)
    : repository_(repository), controller_(controller) {}

CanvasLayoutState::CanvasWorkspaceRepository& Session::canvasRepo() const {
    return repository_ ? *repository_ : CanvasLayoutState::canvasRepository();
}

CanvasLayoutState::CanvasWorkspaceController& Session::workspaceController() const {
    return controller_ ? *controller_ : CanvasLayoutState::workspaceController();
}

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

CanvasCore::ServiceSlot<Session>& sessionSlot() {
    static CanvasCore::ServiceSlot<Session> slot;
    return slot;
}

Session& session() {
    return sessionSlot().get();
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

    if (const auto* target = model_.resolve(*ref)) {
        const auto workspace = CanvasCore::HyprlandRuntime::workspaceById(target->workspaceId);
        const auto remembered = workspace ? workspace->getLastFocusedWindow() : nullptr;
        if (remembered && CanvasCore::HyprlandRuntime::windowWorkspaceId(remembered) == target->workspaceId) {
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

    const auto origin = SessionEffects::captureOrigin();
    originCanvasId_ = canvasRepo().ensureCurrentVisibleCanvas();
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
                     CanvasCore::direction_name(direction),
                     selection ? selection->canvasId : INVALID_CANVAS_ID,
                     selection ? selection->workspaceId : INVALID_WORKSPACE_ID,
                     static_cast<const void*>(selection && selection->window ? selection->window.get() : nullptr),
                     selection ? selection->synthetic : false);
        damageMonitors();
        return true;
    }

    return false;
}

bool Session::switchPreviewCanvas(int canvasId, int preferredMonitorId, bool keepSelectedPending) {
    if (keepSelectedPending)
        std::erase_if(pendingCanvases_, [&](const auto& pending) { return pending.canvas.canvasId != canvasId; });
    viewCanvasId_ = canvasId;
    model_.rebuild(pendingCanvases_, viewCanvasId_);
    renderState().rebuildPreviewAnimations(model_);
    const bool selected = selectCanvas(canvasId, preferredMonitorId);
    damageMonitors();
    return selected;
}

bool Session::moveCanvasSelection(Direction direction) {
    const auto* currentSelection = model_.selection();
    if (!active_ || !currentSelection)
        return false;

    const auto currentCanvasId = model_.selectionCanvasId();
    if (!currentCanvasId)
        return false;

    auto& repo = canvasRepo();
    repo.refreshTemporaryCanvases();
    const auto plan = CanvasLayoutState::planDirectionalCanvasMove(
        canvas_tiles_from_model(model_, repo), *currentCanvasId, direction);

    const auto preferredMonitorId = currentSelection->monitorId;
    if (plan.action == CanvasLayoutState::CanvasNavigationAction::SelectExisting) {
        if (!switchPreviewCanvas(plan.targetCanvasId, preferredMonitorId))
            return false;

        const auto* selection = model_.selection();
        spdlog::info("overview_move_canvas: direction={} canvas={} workspace={}",
                     CanvasCore::direction_name(direction),
                     selection ? selection->canvasId : INVALID_CANVAS_ID,
                     selection ? selection->workspaceId : INVALID_WORKSPACE_ID);
        return true;
    }

    if (plan.action != CanvasLayoutState::CanvasNavigationAction::CreateSynthetic)
        return false;

    pendingCanvases_.clear();
    pendingCanvases_.push_back(repo.buildSyntheticCanvas(plan.createDirection, plan.anchorCanvasId, pendingCanvases_));
    if (!switchPreviewCanvas(pendingCanvases_.back().canvas.canvasId, preferredMonitorId, false))
        return false;

    const auto* selection = model_.selection();
    spdlog::info("overview_create_canvas: direction={} canvas={} workspace={}",
                 CanvasCore::direction_name(direction),
                 selection ? selection->canvasId : INVALID_CANVAS_ID,
                 selection ? selection->workspaceId : INVALID_WORKSPACE_ID);
    return true;
}

bool Session::previewCanvas(int canvasId) {
    if (!active_)
        return false;
    const auto& graph = model_.canvasGraph();
    if (std::none_of(graph.begin(), graph.end(), [&](const auto& node) { return node.canvasId == canvasId; }))
        return false;
    const auto* selection = model_.selection();
    const int monitorId = selection ? selection->monitorId : model_.origin().monitorId;
    return switchPreviewCanvas(canvasId, monitorId);
}

bool Session::selectCanvasNumber(int number) {
    if (!active_ || number < 1 || number > 10)
        return false;

    auto& repo = canvasRepo();
    repo.refreshTemporaryCanvases();
    const auto currentCanvasId = model_.selectionCanvasId().value_or(viewCanvasId_);
    const auto plan = CanvasLayoutState::planNumberedCanvasMove(
        canvas_tiles_from_model(model_, repo), currentCanvasId, number);
    if (plan.action == CanvasLayoutState::CanvasNavigationAction::None)
        return false;

    int targetId = plan.targetCanvasId;
    bool prunePending = true;
    if (plan.action == CanvasLayoutState::CanvasNavigationAction::CreateSynthetic) {
        pendingCanvases_.push_back(repo.buildSyntheticCanvas(plan.createDirection, plan.anchorCanvasId, pendingCanvases_));
        targetId = pendingCanvases_.back().canvas.canvasId;
        prunePending = false;
    }

    const auto* selection = model_.selection();
    const auto monitorId = selection ? selection->monitorId : model_.origin().monitorId;
    return switchPreviewCanvas(targetId, monitorId, prunePending);
}

bool Session::acceptSelection() {
    const auto* selection = model_.selection();
    if (!selection)
        return false;

    auto selectionCopy = *selection;
    if (selectionCopy.canvasId != INVALID_CANVAS_ID) {
        auto& repo = canvasRepo();
        if (repo.find(selectionCopy.canvasId))
            repo.ensureCanvasHasVisibleMembers(selectionCopy.canvasId);

        const auto previewCanvases = repo.previewCanvases(pendingCanvases_);
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
        auto& controller = workspaceController();
        if (!controller.activateCanvas(selectionCopy.canvasId,
                                       selectionCopy.monitorId,
                                       requireSelectedMonitorFocus,
                                       pendingCanvases_,
                                       "overview_accept_canvas"))
            return false;

        if (!controller.finalizeCanvasTarget(selectionCopy.workspaceId, selectionCopy.window, true))
            return false;

        if (!pendingCanvases_.empty())
            repo.commitSyntheticCanvases(pendingCanvases_, selectionCopy.canvasId);
        else
            repo.markActive(selectionCopy.canvasId);
        pendingCanvases_.clear();
        return true;
    }

    return SessionEffects::acceptTarget(selectionCopy);
}

bool Session::restoreOrigin() {
    if (originCanvasId_ != INVALID_CANVAS_ID) {
        auto& repo = canvasRepo();
        if (repo.find(originCanvasId_))
            repo.ensureCanvasHasVisibleMembers(originCanvasId_);
        const auto& origin = model_.origin();
        const auto restoreWindow = origin.window && origin.window->mapped();
        auto& controller = workspaceController();
        if (!controller.activateCanvas(originCanvasId_, origin.monitorId, !restoreWindow, pendingCanvases_, "overview_restore_canvas"))
            return false;
        return restoreWindow ? controller.finalizeCanvasTarget(origin.workspaceId, origin.window, false) : true;
    }

    return SessionEffects::restoreOrigin(model_.origin());
}

void Session::close(bool acceptSelectionFlag) {
    if (!active_)
        return;

    auto resolved = acceptSelectionFlag ? acceptSelection() : restoreOrigin();
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
    Minimap::reset();
}

void Session::dismiss() {
    Minimap::reset();
    if (!active_)
        return;

    const auto* selection = model_.selection();
    spdlog::info("overview_dismiss: selection_canvas={} selection_workspace={} selection_window={}",
                 selection ? selection->canvasId : INVALID_CANVAS_ID,
                 selection ? selection->workspaceId : INVALID_WORKSPACE_ID,
                 static_cast<const void*>(selection && selection->window ? selection->window.get() : nullptr));
    damageMonitors();
    clear();
}

} // namespace Overview
