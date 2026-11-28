#include "karta_workspace_repository.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>
#include <string>
#include <system_error>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/workspace/HLWorkspace.hpp>
#include <hyprland/src/output/Monitor.hpp>

#include "core/hyprland_runtime.h"
#include "internal.h"

namespace KartaLayoutState {
namespace {

using SnapshotKarta = KartaRepoSnapshot::KartaWorkspaceSnapshot;
using SnapshotMember = KartaRepoSnapshot::MonitorMemberSnapshot;

std::string runtime_base_dir() {
    if (const auto* xdgRuntime = std::getenv("XDG_RUNTIME_DIR"); xdgRuntime && xdgRuntime[0] != '\0')
        return xdgRuntime;
    return "/tmp";
}

std::string hyprland_instance_signature() {
    if (const auto* signature = std::getenv("HYPRLAND_INSTANCE_SIGNATURE"); signature && signature[0] != '\0')
        return signature;
    return "default";
}

KartaWorkspaceRecord from_snapshot(const SnapshotKarta& karta) {
    KartaWorkspaceRecord record;
    record.kartaId = karta.kartaId;
    record.tileX = karta.tileX;
    record.tileY = karta.tileY;
    record.ephemeral = karta.ephemeral;
    record.members.reserve(karta.members.size());
    for (const auto& member : karta.members) {
        record.members.push_back({
            .monitorId = member.monitorId,
            .workspaceId = static_cast<WORKSPACEID>(member.workspaceId),
            .special = member.special,
        });
    }
    std::sort(record.members.begin(), record.members.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.monitorId < rhs.monitorId;
    });
    return record;
}

SnapshotKarta to_snapshot(const KartaWorkspaceRecord& karta) {
    SnapshotKarta snapshot;
    snapshot.kartaId = karta.kartaId;
    snapshot.tileX = karta.tileX;
    snapshot.tileY = karta.tileY;
    snapshot.ephemeral = karta.ephemeral;
    snapshot.members.reserve(karta.members.size());
    for (const auto& member : karta.members)
        snapshot.members.push_back({
            .monitorId = member.monitorId,
            .workspaceId = static_cast<int>(member.workspaceId),
            .special = member.special,
        });
    std::sort(snapshot.members.begin(), snapshot.members.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.monitorId < rhs.monitorId;
    });
    return snapshot;
}

KartaWorkspaceRecord* find_karta(std::vector<KartaWorkspaceRecord>& kartas, int kartaId) {
    const auto it = std::find_if(kartas.begin(), kartas.end(), [&](const auto& karta) {
        return karta.kartaId == kartaId;
    });
    return it == kartas.end() ? nullptr : &*it;
}

bool members_match(std::vector<KartaWorkspaceMember> lhs, std::vector<KartaWorkspaceMember> rhs) {
    const auto sortMembers = [](auto& members) {
        std::sort(members.begin(), members.end(), [](const auto& a, const auto& b) {
            if (a.monitorId != b.monitorId)
                return a.monitorId < b.monitorId;
            if (a.workspaceId != b.workspaceId)
                return a.workspaceId < b.workspaceId;
            return a.special < b.special;
        });
    };
    sortMembers(lhs);
    sortMembers(rhs);
    if (lhs.size() != rhs.size())
        return false;
    for (size_t index = 0; index < lhs.size(); ++index) {
        if (lhs[index].monitorId != rhs[index].monitorId ||
            lhs[index].workspaceId != rhs[index].workspaceId ||
            lhs[index].special != rhs[index].special)
            return false;
    }
    return true;
}

WORKSPACEID next_workspace_id(const KartaRepoSnapshot::RepositorySnapshot& snapshot,
                              const std::vector<SyntheticKartaWorkspace>& synthetics = {}) {
    WORKSPACEID maxWorkspaceId = 0;
    if (g_pCompositor) {
        for (const auto& workspaceRef : KartaCore::HyprlandRuntime::workspaces()) {
            const auto workspace = workspaceRef.lock();
            if (!workspace)
                continue;
            maxWorkspaceId = std::max(maxWorkspaceId, KartaCore::HyprlandRuntime::workspaceId(workspace));
        }
    }
    for (const auto& karta : snapshot.kartas) {
        for (const auto& member : karta.members)
            maxWorkspaceId = std::max(maxWorkspaceId, static_cast<WORKSPACEID>(member.workspaceId));
    }
    for (const auto& synthetic : synthetics) {
        for (const auto& member : synthetic.karta.members)
            maxWorkspaceId = std::max(maxWorkspaceId, member.workspaceId);
    }
    return maxWorkspaceId + 1;
}

int next_karta_id(const KartaRepoSnapshot::RepositorySnapshot& snapshot,
                   const std::vector<SyntheticKartaWorkspace>& synthetics = {}) {
    auto nextId = 0;
    for (const auto& karta : snapshot.kartas)
        nextId = std::max(nextId, karta.kartaId + 1);
    for (const auto& synthetic : synthetics)
        nextId = std::max(nextId, synthetic.karta.kartaId + 1);
    return nextId;
}

std::vector<KartaWorkspaceMember> blank_members_for_visible_monitors(const KartaRepoSnapshot::RepositorySnapshot& snapshot,
                                                                     const std::vector<SyntheticKartaWorkspace>& synthetics = {}) {
    std::vector<KartaWorkspaceMember> members;
    if (!g_pCompositor)
        return members;

    auto workspaceId = next_workspace_id(snapshot, synthetics);
    for (const auto& monitor : KartaCore::HyprlandRuntime::monitors()) {
        if (!monitor)
            continue;
        members.push_back({
            .monitorId = static_cast<int>(monitor->m_id),
            .workspaceId = workspaceId++,
            .special = false,
        });
    }
    std::sort(members.begin(), members.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.monitorId < rhs.monitorId;
    });
    return members;
}

void apply_preview_insertion(std::vector<KartaWorkspaceRecord>& kartas, const SyntheticKartaWorkspace& synthetic) {
    const auto* anchor = find_karta(kartas, synthetic.anchorKartaId);
    if (!anchor) {
        kartas.push_back(synthetic.karta);
        return;
    }

    const auto shiftMatching = [&](auto&& predicate, int dx, int dy) {
        for (auto& karta : kartas) {
            if (predicate(karta)) {
                karta.tileX += dx;
                karta.tileY += dy;
            }
        }
    };

    const auto targetX = synthetic.karta.tileX;
    const auto targetY = synthetic.karta.tileY;
    switch (synthetic.direction) {
        case Direction::Left:
            shiftMatching([&](const auto& karta) {
                return karta.tileY == anchor->tileY && karta.tileX <= targetX;
            }, -1, 0);
            break;
        case Direction::Right:
            shiftMatching([&](const auto& karta) {
                return karta.tileY == anchor->tileY && karta.tileX >= targetX;
            }, 1, 0);
            break;
        case Direction::Up:
            shiftMatching([&](const auto& karta) {
                return karta.tileX == anchor->tileX && karta.tileY <= targetY;
            }, 0, -1);
            break;
        case Direction::Down:
            shiftMatching([&](const auto& karta) {
                return karta.tileX == anchor->tileX && karta.tileY >= targetY;
            }, 0, 1);
            break;
        default:
            break;
    }

    kartas.push_back(synthetic.karta);
}

void apply_preview_insertions(std::vector<KartaWorkspaceRecord>& kartas, const std::vector<SyntheticKartaWorkspace>& synthetics) {
    for (const auto& synthetic : synthetics)
        apply_preview_insertion(kartas, synthetic);
}

} // namespace

KartaCore::ServiceSlot<KartaWorkspaceRepository>& kartaRepositorySlot() {
    static KartaCore::ServiceSlot<KartaWorkspaceRepository> slot;
    return slot;
}

KartaWorkspaceRepository& kartaRepository() {
    return kartaRepositorySlot().get();
}

void KartaWorkspaceRepository::initialize() {
    if (initialized_)
        return;

    initialized_ = true;
    loadFromDisk();
    windowOpen_ = Event::bus()->m_events.window.openLate.listen([this](PHLWINDOW window) {
        if (window)
            promoteWorkspace(KartaCore::HyprlandRuntime::windowWorkspaceId(window));
    });
    windowMove_ = Event::bus()->m_events.window.moveToWorkspace.listen([this](PHLWINDOW window, PHLWORKSPACE workspace) {
        if (window)
            promoteWorkspace(KartaCore::HyprlandRuntime::windowWorkspaceId(window));
        if (workspace)
            promoteWorkspace(KartaCore::HyprlandRuntime::workspaceId(workspace));
    });
}

void KartaWorkspaceRepository::shutdown() {
    windowOpen_.reset();
    windowMove_.reset();
    flush();
}

void KartaWorkspaceRepository::promoteWorkspace(WORKSPACEID workspaceId) {
    if (KartaRepoSnapshot::reconcile_temporary_kartas(snapshot_, {static_cast<int>(workspaceId)}, false))
        flushToDisk();
}

void KartaWorkspaceRepository::refreshTemporaryKartas(bool dropInactive) {
    std::vector<int> occupied;
    for (const auto& karta : snapshot_.kartas) {
        for (const auto& member : karta.members) {
            const auto workspace = KartaCore::HyprlandRuntime::workspaceById(member.workspaceId);
            if (workspace && workspace->getWindowCount() > 0)
                occupied.push_back(member.workspaceId);
        }
    }
    if (KartaRepoSnapshot::reconcile_temporary_kartas(snapshot_, occupied, dropInactive))
        flushToDisk();
}

void KartaWorkspaceRepository::flush() const {
    if (!initialized_)
        return;
    flushToDisk();
}

int KartaWorkspaceRepository::activeKartaId() const {
    return snapshot_.activeKartaId;
}

std::vector<KartaWorkspaceRecord> KartaWorkspaceRepository::kartas() const {
    std::vector<KartaWorkspaceRecord> records;
    records.reserve(snapshot_.kartas.size());
    for (const auto& karta : snapshot_.kartas)
        records.push_back(from_snapshot(karta));
    return records;
}

std::optional<KartaWorkspaceRecord> KartaWorkspaceRepository::find(int kartaId) const {
    for (const auto& karta : snapshot_.kartas) {
        if (karta.kartaId == kartaId)
            return from_snapshot(karta);
    }
    return std::nullopt;
}

std::vector<KartaWorkspaceMember> KartaWorkspaceRepository::currentVisibleMembers() const {
    std::vector<KartaWorkspaceMember> members;
    if (!g_pCompositor)
        return members;

    std::vector<KartaRepoSnapshot::MonitorWorkspace> monitors;
    for (const auto& monitor : KartaCore::HyprlandRuntime::monitors()) {
        if (!monitor)
            continue;
        monitors.push_back({
            .monitorId = static_cast<int>(monitor->m_id),
            .workspaceId = static_cast<int>(KartaCore::HyprlandRuntime::activeWorkspaceId(monitor)),
        });
    }

    for (const auto& member : KartaRepoSnapshot::visible_members(std::move(monitors)))
        members.push_back({
            .monitorId = member.monitorId,
            .workspaceId = member.workspaceId,
            .special = member.special,
        });
    return members;
}

int KartaWorkspaceRepository::ensureCurrentVisibleKarta() {
    initialize();
    const auto visible = currentVisibleMembers();
    if (visible.empty())
        return -1;

    for (const auto& karta : snapshot_.kartas) {
        if (members_match(from_snapshot(karta).members, visible)) {
            snapshot_.activeKartaId = karta.kartaId;
            // Imported native workspace combinations may share an entire
            // monitor. Preserve what is visible and give older groups their
            // own empty backing workspaces before exposing the karta graph.
            KartaRepoSnapshot::make_members_exclusive(snapshot_, karta.kartaId,
                                                           static_cast<int>(next_workspace_id(snapshot_)));
            const auto id = karta.kartaId;
            refreshTemporaryKartas(true);
            flushToDisk();
            return id;
        }
    }

    auto records = kartas();
    const auto* active = find_karta(records, snapshot_.activeKartaId);
    const auto nextId = next_karta_id(snapshot_);
    const auto tileX = active ? active->tileX + 1 : (records.empty() ? 0 : std::max_element(records.begin(), records.end(),
        [](const auto& lhs, const auto& rhs) {
            return lhs.tileX < rhs.tileX;
        })->tileX + 1);
    const auto tileY = active ? active->tileY : 0;

    snapshot_.kartas.push_back(to_snapshot({
        .kartaId = nextId,
        .tileX = tileX,
        .tileY = tileY,
        .members = visible,
    }));
    snapshot_.activeKartaId = nextId;
    KartaRepoSnapshot::make_members_exclusive(snapshot_, nextId,
                                                   static_cast<int>(next_workspace_id(snapshot_)));
    refreshTemporaryKartas(true);
    flushToDisk();
    return nextId;
}

SyntheticKartaWorkspace KartaWorkspaceRepository::buildSyntheticKarta(Direction direction,
                                                                         int anchorKartaId,
                                                                         const std::vector<SyntheticKartaWorkspace>& existingSynthetics) const {
    auto records = previewKartas(existingSynthetics);
    const auto* anchor = find_karta(records, anchorKartaId);
    if (!anchor)
        anchor = find_karta(records, snapshot_.activeKartaId);
    if (!anchor && !records.empty())
        anchor = &records.front();

    auto synthetic = SyntheticKartaWorkspace{};
    synthetic.anchorKartaId = anchor ? anchor->kartaId : -1;
    synthetic.direction = direction;
    synthetic.karta.ephemeral = true;
    synthetic.karta.kartaId = next_karta_id(snapshot_, existingSynthetics);
    synthetic.karta.members = blank_members_for_visible_monitors(snapshot_, existingSynthetics);
    synthetic.karta.tileX = anchor ? anchor->tileX : 0;
    synthetic.karta.tileY = anchor ? anchor->tileY : 0;
    switch (direction) {
        case Direction::Left:
            synthetic.karta.tileX -= 1;
            break;
        case Direction::Right:
            synthetic.karta.tileX += 1;
            break;
        case Direction::Up:
            synthetic.karta.tileY -= 1;
            break;
        case Direction::Down:
            synthetic.karta.tileY += 1;
            break;
        default:
            break;
    }
    return synthetic;
}

std::vector<KartaWorkspaceRecord> KartaWorkspaceRepository::previewKartas(const std::vector<SyntheticKartaWorkspace>& synthetics) const {
    auto records = kartas();
    apply_preview_insertions(records, synthetics);
    return records;
}

void KartaWorkspaceRepository::commitSyntheticKartas(const std::vector<SyntheticKartaWorkspace>& synthetics, int activeKartaId) {
    initialize();
    auto records = previewKartas(synthetics);
    snapshot_.kartas.clear();
    snapshot_.kartas.reserve(records.size());
    for (const auto& karta : records)
        snapshot_.kartas.push_back(to_snapshot(karta));
    snapshot_.activeKartaId = activeKartaId;
    refreshTemporaryKartas(true);
    flushToDisk();
}

void KartaWorkspaceRepository::markActive(int kartaId) {
    initialize();
    snapshot_.activeKartaId = kartaId;
    refreshTemporaryKartas(true);
    flushToDisk();
}

void KartaWorkspaceRepository::ensureKartaHasVisibleMembers(int kartaId) {
    initialize();
    const auto karta = std::find_if(snapshot_.kartas.begin(), snapshot_.kartas.end(), [&](const auto& candidate) {
        return candidate.kartaId == kartaId;
    });
    if (karta == snapshot_.kartas.end())
        return;

    std::vector<int> monitorIds;
    for (const auto& member : currentVisibleMembers())
        monitorIds.push_back(member.monitorId);
    if (!KartaRepoSnapshot::fill_monitor_members(*karta, monitorIds, static_cast<int>(next_workspace_id(snapshot_))))
        return;
    flushToDisk();
}

std::string KartaWorkspaceRepository::repositoryPath() const {
    return runtime_base_dir() + "/karta-" + hyprland_instance_signature() + ".state";
}

void KartaWorkspaceRepository::loadFromDisk() {
    snapshot_ = {};

    std::ifstream input(repositoryPath());
    if (!input.is_open())
        return;

    const std::string data((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const auto parsed = KartaRepoSnapshot::deserialize_repository(data);
    if (!parsed) {
        spdlog::warn("karta_repository: failed to parse snapshot file path={}", repositoryPath());
        return;
    }

    snapshot_ = *parsed;
    spdlog::info("karta_repository: loaded karta_count={} path={}", snapshot_.kartas.size(), repositoryPath());
}

void KartaWorkspaceRepository::flushToDisk() const {
    namespace fs = std::filesystem;

    const auto path = repositoryPath();
    const fs::path filePath(path);
    const auto directory = filePath.parent_path();
    if (!directory.empty()) {
        std::error_code ec;
        fs::create_directories(directory, ec);
        if (ec) {
            spdlog::warn("karta_repository: failed to create directory path={} error={}", directory.string(), ec.message());
            return;
        }
    }

    const auto tempPath = path + ".tmp";
    std::ofstream output(tempPath, std::ios::trunc);
    if (!output.is_open()) {
        spdlog::warn("karta_repository: failed to open temp file path={} errno={}", tempPath, errno);
        return;
    }

    output << KartaRepoSnapshot::serialize_repository(snapshot_);
    output.close();

    std::error_code ec;
    fs::rename(tempPath, path, ec);
    if (ec) {
        spdlog::warn("karta_repository: failed to replace path={} error={}", path, ec.message());
        fs::remove(tempPath, ec);
    }
}

} // namespace KartaLayoutState
