/**
 * @file layout/karta/workspace_controller.cpp
 * @brief Runtime implementation of multi-monitor karta switching and transfer.
 */
#include "layout/karta/workspace_controller.h"

#include <algorithm>
#include <optional>
#include <vector>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/animation/WorkspaceAnimationController.hpp>
#include <hyprland/src/desktop/state/GlobalWindowController.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/workspace/HLWorkspace.hpp>
#include <spdlog/spdlog.h>

#include "core/karta_transition.h"
#include "core/core.h"
#include "core/hyprland_runtime.h"
#include "layout/karta/internal.h"
#include "layout/karta/layout.h"
#include "layout/karta/workspace_controller_logic.h"
#include "overview/minimap/runtime.h"

namespace KartaLayoutState {
namespace {

std::vector<KartaTileNode> to_tile_nodes(const std::vector<KartaWorkspaceRecord>& records) {
    std::vector<KartaTileNode> nodes;
    nodes.reserve(records.size());
    for (const auto& record : records) {
        nodes.push_back({
            .kartaId = record.kartaId,
            .tileX = record.tileX,
            .tileY = record.tileY,
            .ephemeral = record.ephemeral,
            .synthetic = false,
        });
    }
    return nodes;
}

const KartaWorkspaceRecord* find_karta(const std::vector<KartaWorkspaceRecord>& kartas, int kartaId) {
    const auto it = std::find_if(kartas.begin(), kartas.end(), [&](const auto& karta) {
        return karta.kartaId == kartaId;
    });
    return it == kartas.end() ? nullptr : &*it;
}

const KartaWorkspaceMember* find_member(const KartaWorkspaceRecord& karta, int monitorId) {
    const auto it = std::find_if(karta.members.begin(), karta.members.end(), [&](const auto& member) {
        return member.monitorId == monitorId;
    });
    return it == karta.members.end() ? nullptr : &*it;
}

PHLWINDOW preferred_window_for_workspace(WORKSPACEID workspaceId) {
    const auto workspace = KartaCore::HyprlandRuntime::workspaceById(workspaceId);
    if (!workspace)
        return nullptr;

    auto remembered = workspace->getLastFocusedWindow();
    if (remembered && remembered->mapped() && !remembered->isFloating() &&
        KartaCore::HyprlandRuntime::windowWorkspaceId(remembered) == workspaceId)
        return remembered;

    if (auto* layout = KartaLayoutInternal::get_karta_for_workspace(workspaceId)) {
        if (const auto candidate = layout->getNextCandidate(nullptr)) {
            auto window = candidate->window();
            if (window && window->mapped() && !window->isFloating())
                return window;
        }
    }
    return nullptr;
}

} // namespace

KartaWorkspaceController::KartaWorkspaceController(KartaWorkspaceRepository* repository)
    : repository_(repository) {}

KartaWorkspaceRepository& KartaWorkspaceController::repo() const {
    return repository_ ? *repository_ : kartaRepository();
}

bool KartaWorkspaceController::activateKarta(int kartaId,
                                               int selectedMonitorId,
                                               bool requireSelectedMonitorFocus,
                                               const std::vector<SyntheticKartaWorkspace>& pending,
                                               const char* context) const {
    auto& kartaRepo = repo();
    const auto previewKartas = kartaRepo.previewKartas(pending);
    const auto* targetKarta = find_karta(previewKartas, kartaId);
    if (!targetKarta)
        return false;

    // Capture the origin before activating any member: native workspace change
    // events can update repository state while the remaining monitors switch.
    const auto sourceKarta = kartaRepo.find(kartaRepo.activeKartaId());

    std::vector<KartaWorkspaceMember> members;
    for (const auto& monitor : KartaCore::HyprlandRuntime::monitors()) {
        if (!monitor)
            continue;

        if (const auto* member = find_member(*targetKarta, static_cast<int>(monitor->m_id)))
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
        const auto monitor = KartaCore::HyprlandRuntime::monitorById(member.monitorId);
        if (!monitor)
            continue;

        const auto workspace = KartaCore::HyprlandRuntime::workspaceById(member.workspaceId);
        const auto outgoingWorkspace = monitor->m_activeWorkspace;
        const auto outgoingOffset = outgoingWorkspace && outgoingWorkspace->m_renderOffset
            ? outgoingWorkspace->m_renderOffset->value() : Vector2D{};
        const auto selectedMember = member.monitorId == selectedMonitorId;
        const auto requireMonitorFocus = selectedMember && requireSelectedMonitorFocus;
        visitedSelectedMonitor = visitedSelectedMonitor || selectedMember;
        if (!KartaLayoutInternal::focus_monitor_workspace(monitor,
                                                           workspace,
                                                           member.workspaceId,
                                                           requireMonitorFocus,
                                                           context))
            return false;

        const auto incomingWorkspace = monitor->m_activeWorkspace;
        if (sourceKarta && sourceKarta->kartaId != kartaId && incomingWorkspace &&
            outgoingWorkspace != incomingWorkspace) {
            const auto offset = KartaCore::karta_transition_offset(sourceKarta->tileX, sourceKarta->tileY,
                                                                       targetKarta->tileX, targetKarta->tileY, monitor->m_size);
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
            spdlog::debug("karta_transition: source={} target={} monitor={} offset=({}, {})",
                          sourceKarta->kartaId, kartaId, member.monitorId, offset.x, offset.y);
        }
    }

    if (visitedSelectedMonitor)
        return true;

    const auto fallbackMember = members.front();
    const auto fallbackMonitor = KartaCore::HyprlandRuntime::monitorById(fallbackMember.monitorId);
    if (!fallbackMonitor)
        return false;

    const auto fallbackWorkspace = KartaCore::HyprlandRuntime::workspaceById(fallbackMember.workspaceId);
    return KartaLayoutInternal::focus_monitor_workspace(fallbackMonitor,
                                                         fallbackWorkspace,
                                                         fallbackMember.workspaceId,
                                                         true,
                                                         context);
}

bool KartaWorkspaceController::finalizeKartaTarget(WORKSPACEID workspaceId, const PHLWINDOW& window, bool warpCursor) const {
    if (!window)
        return true;

    auto* layout = KartaLayoutInternal::get_karta_for_workspace(workspaceId);
    if (!layout)
        return false;

    const auto monitorId = window->monitorID();
    layout->onWindowFocusChange(window);
    layout->recalculateMonitor(monitorId);
    return KartaLayoutInternal::switch_to_window(window, warpCursor);
}

bool KartaWorkspaceController::applyDesktopSwitch(int previousKartaId,
                                                   int preferredMonitorId,
                                                   int targetKartaId,
                                                   const std::vector<SyntheticKartaWorkspace>& pending,
                                                   const char* context) {
    auto& kartaRepo = repo();
    if (kartaRepo.find(targetKartaId))
        kartaRepo.ensureKartaHasVisibleMembers(targetKartaId);

    const auto previewKartas = kartaRepo.previewKartas(pending);
    const auto* targetKarta = find_karta(previewKartas, targetKartaId);
    if (!targetKarta)
        return false;

    const auto* member = find_member(*targetKarta, preferredMonitorId);
    if (!member && !targetKarta->members.empty())
        member = &targetKarta->members.front();

    const auto targetMonitorId = member ? member->monitorId : preferredMonitorId;
    const auto targetWorkspaceId = member ? member->workspaceId : WORKSPACE_INVALID;
    const auto targetWindow = targetWorkspaceId != WORKSPACE_INVALID
        ? preferred_window_for_workspace(targetWorkspaceId)
        : nullptr;

    if (!activateKarta(targetKartaId, targetMonitorId, !targetWindow, pending, context))
        return false;

    if (targetWindow && !finalizeKartaTarget(targetWorkspaceId, targetWindow, true))
        return false;

    if (!pending.empty())
        kartaRepo.commitSyntheticKartas(pending, targetKartaId);
    else
        kartaRepo.markActive(targetKartaId);

    Overview::Minimap::notifySwitch(previousKartaId, kartaRepo.activeKartaId());
    return true;
}

bool KartaWorkspaceController::switchByDirection(Direction direction) {
    auto& kartaRepo = repo();
    const int currentKartaId = kartaRepo.ensureCurrentVisibleKarta();
    if (currentKartaId < 0)
        return false;

    kartaRepo.refreshTemporaryKartas();
    const auto plan = planDirectionalKartaMove(to_tile_nodes(kartaRepo.kartas()), currentKartaId, direction);
    if (plan.action == KartaNavigationAction::None)
        return false;

    const auto monitor = KartaCore::monitorForKeyboardAction();
    const int preferredMonitorId = monitor ? static_cast<int>(monitor->m_id) : -1;

    std::vector<SyntheticKartaWorkspace> pending;
    int targetKartaId = plan.targetKartaId;
    if (plan.action == KartaNavigationAction::CreateSynthetic) {
        pending.push_back(kartaRepo.buildSyntheticKarta(plan.createDirection, plan.anchorKartaId));
        targetKartaId = pending.back().karta.kartaId;
    }

    return applyDesktopSwitch(currentKartaId, preferredMonitorId, targetKartaId, pending, "switch_karta_direction");
}

bool KartaWorkspaceController::switchByNumber(int number) {
    auto& kartaRepo = repo();
    const int currentKartaId = kartaRepo.ensureCurrentVisibleKarta();
    if (currentKartaId < 0)
        return false;

    kartaRepo.refreshTemporaryKartas();
    const auto plan = planNumberedKartaMove(to_tile_nodes(kartaRepo.kartas()), currentKartaId, number);
    if (plan.action == KartaNavigationAction::None)
        return false;

    const auto monitor = KartaCore::monitorForKeyboardAction();
    const int preferredMonitorId = monitor ? static_cast<int>(monitor->m_id) : -1;

    std::vector<SyntheticKartaWorkspace> pending;
    int targetKartaId = plan.targetKartaId;
    if (plan.action == KartaNavigationAction::CreateSynthetic) {
        pending.push_back(kartaRepo.buildSyntheticKarta(plan.createDirection, plan.anchorKartaId));
        targetKartaId = pending.back().karta.kartaId;
    }

    return applyDesktopSwitch(currentKartaId, preferredMonitorId, targetKartaId, pending, "switch_karta_number");
}

bool KartaWorkspaceController::moveFocusedWindowToKarta(Direction direction) {
    if (direction != Direction::Left && direction != Direction::Right &&
        direction != Direction::Up && direction != Direction::Down)
        return false;

    const auto window = Desktop::focusState()->window();
    if (!window || !window->mapped() || window->isFloating() || window->onSpecialWorkspace())
        return false;

    const auto sourceId = KartaCore::HyprlandRuntime::windowWorkspaceId(window);
    const auto source = KartaCore::HyprlandRuntime::workspaceById(sourceId);
    auto* sourceLayout = KartaLayoutInternal::get_karta_for_workspace(sourceId);
    if (!source || !sourceLayout || !sourceLayout->isWindowTiled(window))
        return false;

    const auto monitor = KartaCore::HyprlandRuntime::monitorById(window->monitorID());
    if (!monitor)
        return false;

    auto& kartaRepo = repo();
    const int previousKartaId = kartaRepo.ensureCurrentVisibleKarta();
    if (previousKartaId < 0)
        return false;

    kartaRepo.refreshTemporaryKartas();
    const auto plan = planDirectionalKartaMove(to_tile_nodes(kartaRepo.kartas()), previousKartaId, direction);
    if (plan.action == KartaNavigationAction::None)
        return false;

    std::vector<SyntheticKartaWorkspace> pending;
    int targetKartaId = plan.targetKartaId;
    if (plan.action == KartaNavigationAction::CreateSynthetic) {
        pending.push_back(kartaRepo.buildSyntheticKarta(plan.createDirection, plan.anchorKartaId));
        targetKartaId = pending.back().karta.kartaId;
    } else {
        kartaRepo.ensureKartaHasVisibleMembers(targetKartaId);
    }

    const auto previewKartas = kartaRepo.previewKartas(pending);
    const auto* targetKarta = find_karta(previewKartas, targetKartaId);
    const auto* destMember = targetKarta ? find_member(*targetKarta, static_cast<int>(monitor->m_id)) : nullptr;
    if (!destMember || destMember->workspaceId == sourceId)
        return false;

    auto target = KartaCore::HyprlandRuntime::workspaceById(destMember->workspaceId);
    if (!target) {
        target = State::workspaceState()->createNumbered(
            Workspace::SWorkspaceNumberedID(static_cast<Workspace::WorkspaceIDContainer>(destMember->workspaceId)),
            monitor);
    }
    if (!target || target->monitorID() != monitor->m_id ||
        !KartaLayoutInternal::get_karta_for_workspace(destMember->workspaceId))
        return false;

    const auto previousTargetWindow = target->getLastFocusedWindow();
    // The native silent dispatcher deliberately refocuses another source
    // window. Transfer ownership directly, then perform one karta activation.
    Desktop::globalWindowController()->moveWindowToWorkspace(window, target);
    if (KartaCore::HyprlandRuntime::windowWorkspaceId(window) != destMember->workspaceId)
        return false;

    auto* liveSourceLayout = KartaLayoutInternal::get_karta_for_workspace(sourceId);
    const auto remaining = liveSourceLayout ? liveSourceLayout->getNextCandidate(nullptr) : nullptr;
    source->rememberFocusedWindow(remaining ? remaining->window() : nullptr);
    target->rememberFocusedWindow(window);

    const bool activated =
        activateKarta(targetKartaId, static_cast<int>(monitor->m_id), false, pending, "move_window_to_karta") &&
        finalizeKartaTarget(destMember->workspaceId, window, true);
    if (!activated) {
        Desktop::globalWindowController()->moveWindowToWorkspace(window, source);
        source->rememberFocusedWindow(window);
        target->rememberFocusedWindow(previousTargetWindow);
        (void)activateKarta(previousKartaId, static_cast<int>(monitor->m_id), false, {}, "move_window_to_karta_rollback");
        (void)finalizeKartaTarget(sourceId, window, false);
        return false;
    }

    if (!pending.empty())
        kartaRepo.commitSyntheticKartas(pending, targetKartaId);
    else
        kartaRepo.markActive(targetKartaId);

    Overview::Minimap::notifySwitch(previousKartaId, targetKartaId);
    return true;
}

KartaCore::ServiceSlot<KartaWorkspaceController>& workspaceControllerSlot() {
    static KartaCore::ServiceSlot<KartaWorkspaceController> slot;
    return slot;
}

KartaWorkspaceController& workspaceController() {
    return workspaceControllerSlot().get();
}

} // namespace KartaLayoutState
