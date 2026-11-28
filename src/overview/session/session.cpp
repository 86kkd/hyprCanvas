/**
 * @file overview/session/session.cpp
 * @brief Global read-only overview preview session spanning all kartas and monitors.
 *
 * This file builds a monitor-scoped preview model from the current set of
 * tiled windows, keeps a logical selection independent from Hyprland focus,
 * and delegates final karta activation to `KartaWorkspaceController` only
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
#include "layout/karta/workspace_controller.h"
#include "layout/karta/workspace_controller_logic.h"
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

    const auto cursorMonitor = KartaCore::HyprlandRuntime::monitorFromCursor();
    if (!cursorMonitor)
        return &model.monitors().front();

    const auto* region = model.regionForMonitor(cursorMonitor->m_id);
    return region ? region : &model.monitors().front();
}

Target initial_empty_target(const MonitorRegion& region) {
    return makeEmptyTarget(INVALID_KARTA_ID,
                           SessionEffects::nextWorkspaceId(),
                           region.monitorId,
                           false,
                           {region.box.x + region.box.w * 0.16,
                            region.box.y + region.box.h * 0.16,
                            region.box.w * 0.68,
                            region.box.h * 0.68},
                           true);
}

bool try_select_origin_window(Model& model, int kartaId) {
    const auto originWindow = model.origin().window;
    if (!originWindow)
        return false;

    const auto ref = model.findByWindow(originWindow, kartaId);
    if (!ref)
        return false;

    model.setSelection(*ref);
    return true;
}

bool try_select_origin_workspace(Model& model, int kartaId) {
    const auto originWorkspace = model.origin().workspaceId;
    if (originWorkspace == INVALID_WORKSPACE_ID)
        return false;

    const auto ref = model.findByWorkspace(originWorkspace, kartaId);
    if (!ref)
        return false;

    model.setSelection(*ref);
    return true;
}

std::vector<KartaLayoutState::KartaTileNode> karta_tiles_from_model(
    const Model& model,
    const KartaLayoutState::KartaWorkspaceRepository& repo) {
    const auto& graph = model.kartaGraph();
    std::vector<KartaLayoutState::KartaTileNode> nodes;
    nodes.reserve(graph.size());
    for (const auto& entry : graph) {
        const auto record = repo.find(entry.kartaId);
        nodes.push_back({
            .kartaId = entry.kartaId,
            .tileX = entry.tileX,
            .tileY = entry.tileY,
            .ephemeral = entry.synthetic || (record && record->ephemeral),
            .synthetic = entry.synthetic,
        });
    }
    return nodes;
}

} // namespace

Session::Session(KartaLayoutState::KartaWorkspaceRepository* repository,
                 KartaLayoutState::KartaWorkspaceController* controller)
    : repository_(repository), controller_(controller) {}

KartaLayoutState::KartaWorkspaceRepository& Session::kartaRepo() const {
    return repository_ ? *repository_ : KartaLayoutState::kartaRepository();
}

KartaLayoutState::KartaWorkspaceController& Session::workspaceController() const {
    return controller_ ? *controller_ : KartaLayoutState::workspaceController();
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

KartaCore::ServiceSlot<Session>& sessionSlot() {
    static KartaCore::ServiceSlot<Session> slot;
    return slot;
}

Session& session() {
    return sessionSlot().get();
}

void Session::clear() {
    active_ = false;
    inputHandling_.reset();
    model_.clear();
    originKartaId_ = INVALID_KARTA_ID;
    viewKartaId_ = INVALID_KARTA_ID;
    pendingKartas_.clear();
}

void Session::markInputHandled() {
    inputHandling_.markHandled();
}

bool Session::consumeInputHandled(bool released) {
    return inputHandling_.consume(released);
}

bool Session::selectInitialTarget() {
    const auto choice = chooseInitialSelectionChoice(!model_.targetGraph().empty(),
                                                     model_.findByWindow(model_.origin().window, originKartaId_).has_value(),
                                                     model_.findByWorkspace(model_.origin().workspaceId, originKartaId_).has_value(),
                                                     model_.firstTarget().has_value(),
                                                     initial_empty_region(model_) != nullptr);

    switch (choice) {
        case InitialSelectionChoice::OriginWindow:
            return try_select_origin_window(model_, originKartaId_);
        case InitialSelectionChoice::OriginWorkspace:
            return try_select_origin_workspace(model_, originKartaId_);
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

bool Session::selectKarta(int kartaId, std::optional<int> preferredMonitorId) {
    const auto ref = model_.firstTargetInKarta(kartaId, preferredMonitorId);
    if (!ref)
        return false;

    if (const auto* target = model_.resolve(*ref)) {
        const auto workspace = KartaCore::HyprlandRuntime::workspaceById(target->workspaceId);
        const auto remembered = workspace ? workspace->getLastFocusedWindow() : nullptr;
        if (remembered && KartaCore::HyprlandRuntime::windowWorkspaceId(remembered) == target->workspaceId) {
            if (const auto rememberedRef = model_.findByWindow(remembered, kartaId)) {
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
    originKartaId_ = kartaRepo().ensureCurrentVisibleKarta();
    viewKartaId_ = originKartaId_;
    model_.setOrigin(origin.monitorId, origin.workspaceId, origin.window);
    pendingKartas_.clear();
    SessionEffects::prepareSnapshots();
    model_.rebuild(pendingKartas_, viewKartaId_);
    if (!selectInitialTarget()) {
        clear();
        spdlog::warn("overview_open: no targets available");
        return;
    }

    if (const auto selectionKartaId = model_.selectionKartaId();
        selectionKartaId && *selectionKartaId != viewKartaId_) {
        viewKartaId_ = *selectionKartaId;
        model_.rebuild(pendingKartas_, viewKartaId_);
        (void)selectKarta(viewKartaId_, model_.origin().monitorId);
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

    std::vector<OverviewLogic::KartaTargetCandidate> candidates;
    std::vector<TargetRef> refs;
    candidates.reserve(targetGraph.size());
    refs.reserve(targetGraph.size());

    auto currentIndex = std::optional<size_t>{};
    for (const auto& node : targetGraph) {
        const auto* target = model_.resolve(node.ref);
        if (!target)
            continue;

        candidates.push_back({
            .kartaId = target->kartaId,
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

    const auto nextIndex = OverviewLogic::pickTargetIndexInKarta(candidates, *currentIndex, direction);
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
        spdlog::info("overview_move: direction={} karta={} workspace={} window={} synthetic={}",
                     KartaCore::direction_name(direction),
                     selection ? selection->kartaId : INVALID_KARTA_ID,
                     selection ? selection->workspaceId : INVALID_WORKSPACE_ID,
                     static_cast<const void*>(selection && selection->window ? selection->window.get() : nullptr),
                     selection ? selection->synthetic : false);
        damageMonitors();
        return true;
    }

    return false;
}

bool Session::switchPreviewKarta(int kartaId, int preferredMonitorId, bool keepSelectedPending) {
    if (keepSelectedPending)
        std::erase_if(pendingKartas_, [&](const auto& pending) { return pending.karta.kartaId != kartaId; });
    viewKartaId_ = kartaId;
    model_.rebuild(pendingKartas_, viewKartaId_);
    renderState().rebuildPreviewAnimations(model_);
    const bool selected = selectKarta(kartaId, preferredMonitorId);
    damageMonitors();
    return selected;
}

bool Session::moveKartaSelection(Direction direction) {
    const auto* currentSelection = model_.selection();
    if (!active_ || !currentSelection)
        return false;

    const auto currentKartaId = model_.selectionKartaId();
    if (!currentKartaId)
        return false;

    auto& repo = kartaRepo();
    repo.refreshTemporaryKartas();
    const auto plan = KartaLayoutState::planDirectionalKartaMove(
        karta_tiles_from_model(model_, repo), *currentKartaId, direction);

    const auto preferredMonitorId = currentSelection->monitorId;
    if (plan.action == KartaLayoutState::KartaNavigationAction::SelectExisting) {
        if (!switchPreviewKarta(plan.targetKartaId, preferredMonitorId))
            return false;

        const auto* selection = model_.selection();
        spdlog::info("overview_move_karta: direction={} karta={} workspace={}",
                     KartaCore::direction_name(direction),
                     selection ? selection->kartaId : INVALID_KARTA_ID,
                     selection ? selection->workspaceId : INVALID_WORKSPACE_ID);
        return true;
    }

    if (plan.action != KartaLayoutState::KartaNavigationAction::CreateSynthetic)
        return false;

    pendingKartas_.clear();
    pendingKartas_.push_back(repo.buildSyntheticKarta(plan.createDirection, plan.anchorKartaId, pendingKartas_));
    if (!switchPreviewKarta(pendingKartas_.back().karta.kartaId, preferredMonitorId, false))
        return false;

    const auto* selection = model_.selection();
    spdlog::info("overview_create_karta: direction={} karta={} workspace={}",
                 KartaCore::direction_name(direction),
                 selection ? selection->kartaId : INVALID_KARTA_ID,
                 selection ? selection->workspaceId : INVALID_WORKSPACE_ID);
    return true;
}

bool Session::previewKarta(int kartaId) {
    if (!active_)
        return false;
    const auto& graph = model_.kartaGraph();
    if (std::none_of(graph.begin(), graph.end(), [&](const auto& node) { return node.kartaId == kartaId; }))
        return false;
    const auto* selection = model_.selection();
    const int monitorId = selection ? selection->monitorId : model_.origin().monitorId;
    return switchPreviewKarta(kartaId, monitorId);
}

bool Session::selectKartaNumber(int number) {
    if (!active_ || number < 1 || number > 10)
        return false;

    auto& repo = kartaRepo();
    repo.refreshTemporaryKartas();
    const auto currentKartaId = model_.selectionKartaId().value_or(viewKartaId_);
    const auto plan = KartaLayoutState::planNumberedKartaMove(
        karta_tiles_from_model(model_, repo), currentKartaId, number);
    if (plan.action == KartaLayoutState::KartaNavigationAction::None)
        return false;

    int targetId = plan.targetKartaId;
    bool prunePending = true;
    if (plan.action == KartaLayoutState::KartaNavigationAction::CreateSynthetic) {
        pendingKartas_.push_back(repo.buildSyntheticKarta(plan.createDirection, plan.anchorKartaId, pendingKartas_));
        targetId = pendingKartas_.back().karta.kartaId;
        prunePending = false;
    }

    const auto* selection = model_.selection();
    const auto monitorId = selection ? selection->monitorId : model_.origin().monitorId;
    return switchPreviewKarta(targetId, monitorId, prunePending);
}

bool Session::acceptSelection() {
    const auto* selection = model_.selection();
    if (!selection)
        return false;

    auto selectionCopy = *selection;
    if (selectionCopy.kartaId != INVALID_KARTA_ID) {
        auto& repo = kartaRepo();
        if (repo.find(selectionCopy.kartaId))
            repo.ensureKartaHasVisibleMembers(selectionCopy.kartaId);

        const auto previewKartas = repo.previewKartas(pendingKartas_);
        const auto kartaIt = std::find_if(previewKartas.begin(), previewKartas.end(), [&](const auto& karta) {
            return karta.kartaId == selectionCopy.kartaId;
        });
        if (kartaIt != previewKartas.end()) {
            const auto memberIt = std::find_if(kartaIt->members.begin(), kartaIt->members.end(), [&](const auto& member) {
                return member.monitorId == selectionCopy.monitorId;
            });
            if (memberIt != kartaIt->members.end()) {
                selectionCopy.workspaceId = memberIt->workspaceId;
                selectionCopy.specialWorkspace = memberIt->special;
            }
        }

        const auto requireSelectedMonitorFocus = selectionCopy.type != TargetType::Window || !selectionCopy.window;
        auto& controller = workspaceController();
        if (!controller.activateKarta(selectionCopy.kartaId,
                                       selectionCopy.monitorId,
                                       requireSelectedMonitorFocus,
                                       pendingKartas_,
                                       "overview_accept_karta"))
            return false;

        if (!controller.finalizeKartaTarget(selectionCopy.workspaceId, selectionCopy.window, true))
            return false;

        if (!pendingKartas_.empty())
            repo.commitSyntheticKartas(pendingKartas_, selectionCopy.kartaId);
        else
            repo.markActive(selectionCopy.kartaId);
        pendingKartas_.clear();
        return true;
    }

    return SessionEffects::acceptTarget(selectionCopy);
}

bool Session::restoreOrigin() {
    if (originKartaId_ != INVALID_KARTA_ID) {
        auto& repo = kartaRepo();
        if (repo.find(originKartaId_))
            repo.ensureKartaHasVisibleMembers(originKartaId_);
        const auto& origin = model_.origin();
        const auto restoreWindow = origin.window && origin.window->mapped();
        auto& controller = workspaceController();
        if (!controller.activateKarta(originKartaId_, origin.monitorId, !restoreWindow, pendingKartas_, "overview_restore_karta"))
            return false;
        return restoreWindow ? controller.finalizeKartaTarget(origin.workspaceId, origin.window, false) : true;
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
    spdlog::info("overview_close: accepted={} resolved={} selection_karta={} selection_workspace={} selection_window={}",
                 acceptSelectionFlag,
                 resolved,
                 selection ? selection->kartaId : INVALID_KARTA_ID,
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
    spdlog::info("overview_dismiss: selection_karta={} selection_workspace={} selection_window={}",
                 selection ? selection->kartaId : INVALID_KARTA_ID,
                 selection ? selection->workspaceId : INVALID_WORKSPACE_ID,
                 static_cast<const void*>(selection && selection->window ? selection->window.get() : nullptr));
    damageMonitors();
    clear();
}

} // namespace Overview
