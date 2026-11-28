/**
 * @file model.h
 * @brief Temporary overview model built from per-workspace karta snapshots.
 */
#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>

#include "core/types.h"
#include "layout/karta/karta_workspace_repository.h"

namespace Overview {

constexpr WORKSPACEID INVALID_WORKSPACE_ID = static_cast<WORKSPACEID>(-1);
constexpr int         INVALID_MONITOR_ID = -1;
constexpr int         INVALID_KARTA_ID = -1;

enum class TargetType {
    Window,
    EmptyWorkspace,
};

struct Target {
    TargetType        type = TargetType::Window;
    int               kartaId = INVALID_KARTA_ID;
    WORKSPACEID       workspaceId = INVALID_WORKSPACE_ID;
    int               monitorId = INVALID_MONITOR_ID;
    bool              specialWorkspace = false;
    PHLWINDOW         window = nullptr;
    KartaCore::Box box{};
    KartaCore::Box sourceBox{};
    bool              synthetic = false;
    PHLWINDOW         stackActiveWindow = nullptr;
};

struct WorkspaceNode {
    int                kartaId = INVALID_KARTA_ID;
    int                tileX = 0;
    int                tileY = 0;
    WORKSPACEID        workspaceId = INVALID_WORKSPACE_ID;
    int                monitorId = INVALID_MONITOR_ID;
    bool               specialWorkspace = false;
    bool               synthetic = false;
    std::vector<Target> targets;
    KartaCore::Box  box;
};

struct MonitorRegion {
    int                      monitorId = INVALID_MONITOR_ID;
    PHLMONITOR               monitor = nullptr;
    KartaCore::Box        box;
    std::vector<WorkspaceNode> workspaces;
};

struct OriginState {
    int         monitorId = INVALID_MONITOR_ID;
    WORKSPACEID workspaceId = INVALID_WORKSPACE_ID;
    PHLWINDOW   window = nullptr;
};

struct TargetRef {
    bool        synthetic = false;
    std::size_t monitorIndex = 0;
    std::size_t workspaceIndex = 0;
    std::size_t targetIndex = 0;

    auto operator<=>(const TargetRef&) const = default;
};

struct TargetGraphNode {
    TargetRef          ref;
    int                kartaId = INVALID_KARTA_ID;
    int                monitorId = INVALID_MONITOR_ID;
    WORKSPACEID        workspaceId = INVALID_WORKSPACE_ID;
    KartaCore::Box  box;
};

struct KartaGraphNode {
    int               kartaId = INVALID_KARTA_ID;
    int               tileX = 0;
    int               tileY = 0;
    KartaCore::Box box;
    bool              synthetic = false;
};

Target makeEmptyTarget(int kartaId, WORKSPACEID workspaceId, int monitorId, bool specialWorkspace,
                       const KartaCore::Box& workspaceBox, bool synthetic);

class Model {
  public:
    void clear();
    void rebuild(const std::vector<KartaLayoutState::SyntheticKartaWorkspace>& synthetics = {},
                 int viewportKartaId = INVALID_KARTA_ID);

    void setOrigin(int monitorId, WORKSPACEID workspaceId, PHLWINDOW window);
    const OriginState& origin() const;

    const std::vector<MonitorRegion>& monitors() const;
    const std::vector<TargetGraphNode>& targetGraph() const;
    const std::vector<KartaGraphNode>& kartaGraph() const;

    const std::optional<TargetRef>& selectionRef() const;
    const Target* selection() const;
    std::optional<int> selectionKartaId() const;
    void setSelection(const TargetRef& ref);
    void clearSelection();

    void setSyntheticSelection(Target target);
    void clearSyntheticSelection();
    const std::optional<Target>& syntheticSelection() const;

    const MonitorRegion* regionForMonitor(int monitorId) const;
    const Target* resolve(const TargetRef& ref) const;
    std::optional<TargetRef> findByWindow(PHLWINDOW window, int kartaId = INVALID_KARTA_ID) const;
    std::optional<TargetRef> findByWorkspace(WORKSPACEID workspaceId, int kartaId = INVALID_KARTA_ID) const;
    std::optional<TargetRef> firstTargetInKarta(int kartaId, std::optional<int> preferredMonitorId = std::nullopt) const;
    std::optional<int> findAdjacentKarta(int kartaId, Direction direction) const;
    std::optional<TargetRef> firstTarget() const;

  private:
    void rebuildTargetGraph();
    void rebuildKartaGraph();

    OriginState                   origin_;
    std::vector<MonitorRegion>    monitors_;
    std::vector<TargetGraphNode>  targetGraph_;
    std::vector<KartaGraphNode>  kartaGraph_;
    std::optional<TargetRef>      selectionRef_;
    std::optional<Target>         syntheticSelection_;
};

} // namespace Overview
