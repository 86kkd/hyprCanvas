#include <string>

#include "core/karta_workspace_snapshot.h"
#include "core/layout_snapshot.h"

#include "test_suite.h"
#include "test_support.h"

namespace {

void expect_box_eq(const KartaCore::Box &actual, const KartaCore::Box &expected, std::string_view label) {
    expect_near(actual.x, expected.x, 1e-9, std::string(label) + " x");
    expect_near(actual.y, expected.y, 1e-9, std::string(label) + " y");
    expect_near(actual.w, expected.w, 1e-9, std::string(label) + " w");
    expect_near(actual.h, expected.h, 1e-9, std::string(label) + " h");
}

void test_snapshot_round_trip() {
    using namespace KartaLayoutSnapshot;

    RepositorySnapshot repository;
    KartaSnapshot karta;
    karta.workspaceId = 7;
    karta.activeLaneIndex = 1;

    LaneSnapshot firstLane;
    firstLane.mode = static_cast<int>(Mode::Row);
    firstLane.ephemeral = false;
    firstLane.activeStackIndex = 0;

    StackSnapshot firstStack;
    firstStack.width = 3;
    firstStack.fullscreened = true;
    firstStack.maximized = false;
    firstStack.geom = {10.0, 20.0, 300.0, 400.0};
    firstStack.memGeom = {1.0, 2.0, 3.0, 4.0};
    firstStack.activeWindowKey = 0x22;
    firstStack.windows.push_back({.key = 0x11, .heightMode = 0, .geomY = 50.0, .geomH = 150.0, .memY = 5.0, .memH = 15.0});
    firstStack.windows.push_back({.key = 0x22, .heightMode = 5, .geomY = 200.0, .geomH = 125.0, .memY = 25.0, .memH = 35.0});
    firstLane.stacks.push_back(firstStack);

    LaneSnapshot secondLane;
    secondLane.mode = static_cast<int>(Mode::Column);
    secondLane.ephemeral = true;
    secondLane.activeStackIndex = 0;
    secondLane.stacks.push_back({
        .width = 1,
        .fullscreened = false,
        .maximized = true,
        .geom = {100.0, 120.0, 800.0, 900.0},
        .memGeom = {101.0, 121.0, 700.0, 800.0},
        .activeWindowKey = 0x33,
        .windows = {{.key = 0x33, .heightMode = 4, .geomY = 15.0, .geomH = 600.0, .memY = 10.0, .memH = 580.0}},
    });

    karta.lanes.push_back(firstLane);
    karta.lanes.push_back(secondLane);
    repository.emplace(karta.workspaceId, karta);

    const auto serialized = serialize_repository(repository);
    const auto parsed = deserialize_repository(serialized);
    expect_true(parsed.has_value(), "layout snapshot round trip parses");
    if (!parsed)
        return;

    const auto it = parsed->find(7);
    expect_true(it != parsed->end(), "layout snapshot round trip keeps workspace");
    if (it == parsed->end())
        return;

    const auto &restored = it->second;
    expect_eq(restored.version, kFormatVersion, "layout snapshot keeps current format version");
    expect_eq(restored.workspaceId, 7, "layout snapshot keeps workspace id");
    expect_eq(restored.activeLaneIndex, static_cast<size_t>(1), "layout snapshot keeps active lane index");
    expect_eq(restored.lanes.size(), static_cast<size_t>(2), "layout snapshot keeps lane count");
    expect_eq(restored.lanes[0].stacks.size(), static_cast<size_t>(1), "layout snapshot keeps stack count");
    expect_eq(restored.lanes[0].stacks[0].windows.size(), static_cast<size_t>(2), "layout snapshot keeps window count");
    expect_true(restored.lanes[1].ephemeral, "layout snapshot keeps lane ephemeral flag");
    expect_true(restored.lanes[0].stacks[0].fullscreened, "layout snapshot keeps fullscreened flag");
    expect_true(restored.lanes[1].stacks[0].maximized, "layout snapshot keeps maximized flag");
    expect_eq(restored.lanes[0].stacks[0].activeWindowKey, static_cast<uintptr_t>(0x22), "layout snapshot keeps active window key");
    expect_eq(restored.lanes[0].stacks[0].windows[1].heightMode, 5, "layout snapshot keeps window height mode");
    expect_box_eq(restored.lanes[1].stacks[0].geom, {100.0, 120.0, 800.0, 900.0}, "layout snapshot keeps stack geom");
}

void test_snapshot_accepts_legacy_lane_format() {
    const std::string legacy =
        "VERSION 1\n"
        "WORKSPACE 4 0 1\n"
        "LANE 0 0 0 0 1\n"
        "STACK 1 0 0 0 10 20 300 400 1 2 3 4 11 1\n"
        "WINDOW 11 3 20 300 20 300\n";
    const auto parsed = KartaLayoutSnapshot::deserialize_repository(legacy);
    expect_true(parsed.has_value(), "layout snapshot accepts legacy v1 lane format");
    if (!parsed)
        return;

    const auto it = parsed->find(4);
    expect_true(it != parsed->end(), "legacy layout snapshot keeps workspace");
    if (it == parsed->end())
        return;

    expect_eq(it->second.lanes.size(), static_cast<size_t>(1), "legacy layout snapshot keeps lanes");
    expect_eq(it->second.version, KartaLayoutSnapshot::kLegacyFormatVersion, "legacy snapshot keeps source format version");
    expect_eq(it->second.lanes[0].stacks[0].width, 2, "legacy one-half width becomes two visible stacks");
}

void test_snapshot_maps_legacy_width_presets() {
    const std::string v5 =
        "VERSION 5\n"
        "WORKSPACE 4 0 1 5\n"
        "LANE 0 0 0 0 4\n"
        "STACK 0 0 0 0 0 0 600 900 0 0 0 0 11 0\n"
        "STACK 1 0 0 0 0 0 900 900 0 0 0 0 12 0\n"
        "STACK 2 0 0 0 0 0 1200 900 0 0 0 0 13 0\n"
        "STACK 4 0 0 0 0 0 700 900 0 0 0 0 14 0\n";
    const auto parsed = KartaLayoutSnapshot::deserialize_repository(v5);
    expect_true(parsed.has_value(), "layout snapshot accepts v5 width presets");
    if (!parsed)
        return;

    const auto &stacks = parsed->at(4).lanes[0].stacks;
    expect_eq(stacks[0].width, 3, "legacy one-third width becomes three visible stacks");
    expect_eq(stacks[1].width, 2, "legacy one-half width becomes two visible stacks");
    expect_eq(stacks[2].width, 0, "legacy two-thirds width becomes a free stack");
    expect_eq(stacks[3].width, 0, "legacy free width stays free");
    expect_near(stacks[2].geom.w, 1200.0, 1e-9, "legacy two-thirds stack keeps its stored span");

    const auto reparsed = KartaLayoutSnapshot::deserialize_repository(KartaLayoutSnapshot::serialize_repository(*parsed));
    expect_true(reparsed.has_value() && reparsed->at(4).lanes[0].stacks[0].width == 3,
                "migrated widths survive a current-format round trip");
}

void test_snapshot_ignores_removed_grid_state() {
    const std::string v3 =
        "VERSION 3\n"
        "WORKSPACE 4 0 1\n"
        "LANE 0 0 0 0 1\n"
        "STACK 1 0 0 0 10 20 300 400 1 2 3 4 11 1\n"
        "WINDOW 11 3 20 300 20 300\n"
        "GRID 1 0 2 -1 1 0\n"
        "GRIDITEM 11 2 0 1 1\n";
    const auto parsed = KartaLayoutSnapshot::deserialize_repository(v3);
    expect_true(parsed.has_value(), "layout snapshot accepts files written with kartagrid state");
    if (!parsed)
        return;

    const auto it = parsed->find(4);
    expect_true(it != parsed->end(), "grid-era snapshot keeps workspace");
    if (it == parsed->end())
        return;

    expect_eq(it->second.lanes.size(), static_cast<size_t>(1), "grid-era snapshot keeps lane state");
    expect_eq(it->second.lanes[0].stacks.size(), static_cast<size_t>(1), "grid lines do not truncate lane stacks");
    expect_true(KartaLayoutSnapshot::serialize_repository(*parsed).find("GRID") == std::string::npos,
                "serialized snapshot no longer writes grid state");
}

void test_snapshot_preserves_mixed_window_key_versions() {
    const std::string mixed =
        "VERSION 5\n"
        "WORKSPACE 1 0 0 4\n"
        "WORKSPACE 2 0 0 5\n";
    const auto parsed = KartaLayoutSnapshot::deserialize_repository(mixed);
    expect_true(parsed.has_value(), "layout snapshot accepts mixed key versions during migration");
    if (!parsed)
        return;

    expect_eq(parsed->at(1).version, KartaLayoutSnapshot::kFullscreenFormatVersion,
              "mixed snapshot keeps legacy pointer key version");
    expect_eq(parsed->at(2).version, KartaLayoutSnapshot::kStableWindowKeyFormatVersion,
              "mixed snapshot keeps stable window key version");
}

void test_snapshot_rejects_invalid_version() {
    const auto parsed = KartaLayoutSnapshot::deserialize_repository("VERSION 99\n");
    expect_true(!parsed.has_value(), "layout snapshot rejects unsupported versions");
}

void test_snapshot_rejects_malformed_stack_line() {
    const std::string malformed =
        "VERSION 1\n"
        "WORKSPACE 4 0 1\n"
        "LANE 0 0 0 1\n"
        "STACK 1 0 0 0 1 2 3\n";
    const auto parsed = KartaLayoutSnapshot::deserialize_repository(malformed);
    expect_true(!parsed.has_value(), "layout snapshot rejects malformed stack lines");
}

void test_karta_workspace_snapshot_round_trip() {
    using namespace KartaRepoSnapshot;

    RepositorySnapshot repository;
    repository.activeKartaId = 4;
    repository.kartas.push_back({
        .kartaId = 4,
        .tileX = 1,
        .tileY = -1,
        .members = {
            {.monitorId = 1, .workspaceId = 7, .special = false},
            {.monitorId = 2, .workspaceId = 12, .special = true},
        },
    });

    const auto serialized = serialize_repository(repository);
    const auto parsed = deserialize_repository(serialized);
    expect_true(parsed.has_value(), "karta workspace snapshot round trip parses");
    if (!parsed)
        return;

    expect_eq(parsed->activeKartaId, 4, "karta workspace snapshot keeps active karta id");
    expect_eq(parsed->kartas.size(), static_cast<size_t>(1), "karta workspace snapshot keeps karta count");
    expect_eq(parsed->kartas[0].kartaId, 4, "karta workspace snapshot keeps karta id");
    expect_eq(parsed->kartas[0].tileX, 1, "karta workspace snapshot keeps tile x");
    expect_eq(parsed->kartas[0].tileY, -1, "karta workspace snapshot keeps tile y");
    expect_eq(parsed->kartas[0].members.size(), static_cast<size_t>(2), "karta workspace snapshot keeps member count");
    expect_true(parsed->kartas[0].members[1].special, "karta workspace snapshot keeps special member flag");
}

void test_legacy_karta_members_become_exclusive() {
    using namespace KartaRepoSnapshot;
    RepositorySnapshot repository;
    repository.activeKartaId = 1; // Stale ACTIVE must not trump visible karta 0.
    repository.kartas = {
        {0, 0, 0, {{0, 1, false}, {1, 2, false}}},
        {1, 1, 0, {{0, 1, false}, {1, 4, false}}},
        {2, 2, 0, {{0, 1, false}, {1, 3, false}}},
    };
    expect_true(make_members_exclusive(repository, 0, 21), "legacy shared monitor members are migrated");
    expect_eq(repository.kartas[0].members[0].workspaceId, 1, "visible left workspace stays intact");
    expect_eq(repository.kartas[0].members[1].workspaceId, 2, "visible right workspace stays intact");
    expect_eq(repository.kartas[1].members[0].workspaceId, 21, "second karta receives independent left workspace");
    expect_eq(repository.kartas[2].members[0].workspaceId, 22, "third karta receives another independent left workspace");
    expect_eq(repository.kartas[1].members[1].workspaceId, 4, "unique hidden workspace keeps its windows");
    expect_true(!make_members_exclusive(repository, 0, 23), "migration is idempotent");

    repository.kartas.push_back({3, 3, 0, {{0, 1, false}, {1, 2, false}}});
    expect_true(make_members_exclusive(repository, 3, 23), "new native combination claims its visible members");
    expect_eq(repository.kartas[3].members[0].workspaceId, 1, "new visible karta retains real windows");
    expect_eq(repository.kartas[0].members[0].workspaceId, 23, "older karta cannot retain shared member");
}

void test_karta_members_follow_monitors() {
    using namespace KartaRepoSnapshot;

    const auto visible = visible_members({{.monitorId = 2, .workspaceId = 12}, {.monitorId = 0, .workspaceId = 3}});
    expect_eq(visible.size(), static_cast<size_t>(2), "visible members hold one entry per monitor");
    expect_eq(visible[0].monitorId, 0, "visible members are ordered by monitor");
    expect_true(!visible[0].special && !visible[1].special, "visible members are never special workspaces");

    KartaWorkspaceSnapshot karta{
        .kartaId = 1,
        .members = {
            {.monitorId = 0, .workspaceId = 3, .special = false},
            {.monitorId = 0, .workspaceId = 4, .special = false},
            {.monitorId = 1, .workspaceId = -98, .special = true},
            {.monitorId = 5, .workspaceId = 9, .special = false},
        },
    };
    expect_true(fill_monitor_members(karta, {0, 1, 2}, 20), "monitor members are normalized");
    expect_eq(karta.members.size(), static_cast<size_t>(4), "each connected monitor gets one member, disconnected ones stay");
    expect_eq(karta.members[0].workspaceId, 3, "the first member of a monitor wins over duplicates");
    expect_true(karta.members[1].monitorId == 1 && !karta.members[1].special && karta.members[1].workspaceId == 20,
                "a special member is replaced by a fresh ordinary workspace");
    expect_true(karta.members[2].monitorId == 2 && karta.members[2].workspaceId == 21,
                "a missing monitor receives the next fresh workspace");
    expect_true(karta.members[3].monitorId == 5 && karta.members[3].workspaceId == 9,
                "a disconnected monitor keeps its member for later");
    expect_true(!fill_monitor_members(karta, {0, 1, 2}, 20), "monitor member normalization is idempotent");
}

void test_special_members_are_not_exclusive_pages() {
    using namespace KartaRepoSnapshot;
    RepositorySnapshot repository;
    repository.kartas = {
        {0, 0, 0, {{0, 1, false}, {1, -98, true}}},
        {1, 1, 0, {{0, 5, false}, {1, -98, true}}},
    };
    expect_true(!make_members_exclusive(repository, 0, 10), "legacy special members are not reassigned as pages");
    expect_eq(repository.kartas[1].members[1].workspaceId, -98, "a shared special member keeps its workspace");
}

void test_temporary_karta_lifecycle() {
    using namespace KartaRepoSnapshot;
    RepositorySnapshot repository;
    repository.activeKartaId = 1;
    repository.kartas = {
        {0, 0, 0, {{0, 1, false}, {1, 2, false}}},
        {1, 1, 0, {{0, 3, false}, {1, 4, false}}, true},
    };
    auto restored = deserialize_repository(serialize_repository(repository));
    expect_true(restored && restored->kartas[1].ephemeral, "temporary status survives plugin reload");
    if (!restored)
        return;
    expect_true(!reconcile_temporary_kartas(repository, {}, true), "active blank insertion page remains visible");
    repository.activeKartaId = 0;
    expect_true(reconcile_temporary_kartas(repository, {}, true), "leaving unused temporary karta removes it");
    expect_eq(repository.kartas.size(), size_t{1}, "no empty karta left between populated kartas");

    repository = *restored;
    expect_true(reconcile_temporary_kartas(repository, {4}, false), "window on any member promotes whole karta");
    expect_true(!repository.kartas[1].ephemeral, "promoted karta is permanent");
    repository.activeKartaId = 0;
    expect_true(!reconcile_temporary_kartas(repository, {}, true), "closing its last window does not undo promotion");
    expect_eq(repository.kartas.size(), size_t{2}, "previously populated karta remains");

    const auto legacy = deserialize_repository("VERSION 1\nACTIVE 0\nKARTA 0 0 0 1\nMEMBER 0 1 0\n");
    expect_true(legacy && !legacy->kartas[0].ephemeral, "legacy kartas remain permanent");
    expect_true(!deserialize_repository("VERSION 1\nACTIVE 0\nKARTA 0 0 0 0 2\n"), "invalid temporary flag is rejected");
}

void test_karta_workspace_snapshot_rejects_missing_active_line() {
    const auto parsed = KartaRepoSnapshot::deserialize_repository(
        "VERSION 1\n"
        "KARTA 0 0 0 0\n");
    expect_true(!parsed.has_value(), "karta workspace snapshot rejects missing ACTIVE line");
}

} // namespace

void run_layout_snapshot_tests() {
    test_snapshot_round_trip();
    test_snapshot_accepts_legacy_lane_format();
    test_snapshot_maps_legacy_width_presets();
    test_snapshot_ignores_removed_grid_state();
    test_snapshot_preserves_mixed_window_key_versions();
    test_snapshot_rejects_invalid_version();
    test_snapshot_rejects_malformed_stack_line();
    test_karta_workspace_snapshot_round_trip();
    test_temporary_karta_lifecycle();
    test_legacy_karta_members_become_exclusive();
    test_karta_members_follow_monitors();
    test_special_members_are_not_exclusive_pages();
    test_karta_workspace_snapshot_rejects_missing_active_line();
}
