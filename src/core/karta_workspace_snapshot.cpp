#include "karta_workspace_snapshot.h"

#include <charconv>
#include <algorithm>
#include <unordered_set>
#include <cstdlib>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace KartaRepoSnapshot {

std::vector<MonitorMemberSnapshot> visible_members(std::vector<MonitorWorkspace> monitors) {
    std::sort(monitors.begin(), monitors.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.monitorId < rhs.monitorId;
    });
    std::vector<MonitorMemberSnapshot> members;
    members.reserve(monitors.size());
    for (const auto& monitor : monitors)
        members.push_back({.monitorId = monitor.monitorId, .workspaceId = monitor.workspaceId, .special = false});
    return members;
}

bool fill_monitor_members(KartaWorkspaceSnapshot& karta, const std::vector<int>& monitorIds, int nextWorkspaceId) {
    for (const auto& member : karta.members)
        nextWorkspaceId = std::max(nextWorkspaceId, member.workspaceId + 1);

    const auto connected = [&](int monitorId) {
        return std::find(monitorIds.begin(), monitorIds.end(), monitorId) != monitorIds.end();
    };
    std::unordered_set<int> covered;
    std::vector<MonitorMemberSnapshot> members;
    members.reserve(karta.members.size() + monitorIds.size());
    for (const auto& member : karta.members) {
        if (connected(member.monitorId) && (member.special || covered.contains(member.monitorId)))
            continue;
        covered.insert(member.monitorId);
        members.push_back(member);
    }
    for (const auto monitorId : monitorIds) {
        if (covered.insert(monitorId).second)
            members.push_back({.monitorId = monitorId, .workspaceId = nextWorkspaceId++, .special = false});
    }

    std::sort(members.begin(), members.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.monitorId < rhs.monitorId;
    });
    const bool changed = members.size() != karta.members.size() ||
        !std::equal(members.begin(), members.end(), karta.members.begin(), [](const auto& lhs, const auto& rhs) {
            return lhs.monitorId == rhs.monitorId && lhs.workspaceId == rhs.workspaceId && lhs.special == rhs.special;
        });
    karta.members = std::move(members);
    return changed;
}

bool make_members_exclusive(RepositorySnapshot& snapshot, int preferredKartaId, int nextWorkspaceId) {
    for (const auto& karta : snapshot.kartas)
        for (const auto& member : karta.members)
            nextWorkspaceId = std::max(nextWorkspaceId, member.workspaceId + 1);

    std::unordered_set<int> owned;
    bool changed = false;
    const auto claim = [&](auto& karta) {
        for (auto& member : karta.members) {
            // Special workspaces are overlays, not ordinary karta pages.
            if (member.special)
                continue;
            if (!owned.insert(member.workspaceId).second) {
                member.workspaceId = nextWorkspaceId++;
                owned.insert(member.workspaceId);
                changed = true;
            }
        }
    };
    for (auto& karta : snapshot.kartas)
        if (karta.kartaId == preferredKartaId)
            claim(karta);
    for (auto& karta : snapshot.kartas)
        if (karta.kartaId != preferredKartaId)
            claim(karta);
    return changed;
}

bool reconcile_temporary_kartas(RepositorySnapshot& snapshot, const std::vector<int>& occupiedWorkspaceIds, bool dropInactive) {
    bool changed = false;
    for (auto& karta : snapshot.kartas) {
        if (!karta.ephemeral)
            continue;
        const bool occupied = std::any_of(karta.members.begin(), karta.members.end(), [&](const auto& member) {
            return !member.special && std::find(occupiedWorkspaceIds.begin(), occupiedWorkspaceIds.end(), member.workspaceId) != occupiedWorkspaceIds.end();
        });
        if (occupied) {
            karta.ephemeral = false;
            changed = true;
        }
    }
    if (dropInactive) {
        changed = std::erase_if(snapshot.kartas, [&](const auto& karta) {
            return karta.ephemeral && karta.kartaId != snapshot.activeKartaId;
        }) > 0 || changed;
    }
    return changed;
}

namespace {

template <typename T>
bool parse_token(std::string_view token, T& out);

template <>
bool parse_token<int>(std::string_view token, int& out) {
    const auto* begin = token.data();
    const auto* end = token.data() + token.size();
    const auto [ptr, ec] = std::from_chars(begin, end, out);
    return ec == std::errc{} && ptr == end;
}

bool parse_bool_token(std::string_view token, bool& out) {
    int value = 0;
    if (!parse_token(token, value))
        return false;
    if (value != 0 && value != 1)
        return false;
    out = value != 0;
    return true;
}

std::vector<std::string_view> split_tokens(std::string_view line) {
    std::vector<std::string_view> tokens;
    size_t start = 0;
    while (start < line.size()) {
        const auto first = line.find_first_not_of(" \t\r", start);
        if (first == std::string_view::npos)
            break;
        const auto last = line.find_first_of(" \t\r", first);
        if (last == std::string_view::npos) {
            tokens.push_back(line.substr(first));
            break;
        }
        tokens.push_back(line.substr(first, last - first));
        start = last + 1;
    }
    return tokens;
}

} // namespace

std::string serialize_repository(const RepositorySnapshot& snapshot) {
    std::ostringstream out;
    out << "VERSION " << snapshot.version << '\n';
    out << "ACTIVE " << snapshot.activeKartaId << '\n';
    for (const auto& karta : snapshot.kartas) {
        out << "KARTA " << karta.kartaId << ' ' << karta.tileX << ' ' << karta.tileY << ' ' << karta.members.size() << ' ' << (karta.ephemeral ? 1 : 0) << '\n';
        for (const auto& member : karta.members)
            out << "MEMBER " << member.monitorId << ' ' << member.workspaceId << ' ' << (member.special ? 1 : 0) << '\n';
    }
    return out.str();
}

std::optional<RepositorySnapshot> deserialize_repository(std::string_view data) {
    RepositorySnapshot snapshot;
    std::istringstream input{std::string(data)};
    std::string line;
    auto* currentKarta = static_cast<KartaWorkspaceSnapshot*>(nullptr);
    bool sawVersion = false;
    bool sawActive = false;

    while (std::getline(input, line)) {
        const auto tokens = split_tokens(line);
        if (tokens.empty())
            continue;

        if (tokens[0] == "VERSION") {
            if (tokens.size() != 2 || !parse_token(tokens[1], snapshot.version) || snapshot.version != kFormatVersion)
                return std::nullopt;
            sawVersion = true;
            continue;
        }

        if (!sawVersion)
            return std::nullopt;

        if (tokens[0] == "ACTIVE") {
            if (tokens.size() != 2 || !parse_token(tokens[1], snapshot.activeKartaId))
                return std::nullopt;
            sawActive = true;
            continue;
        }

        if (tokens[0] == "KARTA") {
            KartaWorkspaceSnapshot karta;
            int memberCount = 0;
            if ((tokens.size() != 5 && tokens.size() != 6) ||
                (tokens.size() == 6 && !parse_bool_token(tokens[5], karta.ephemeral)) ||
                !parse_token(tokens[1], karta.kartaId) ||
                !parse_token(tokens[2], karta.tileX) ||
                !parse_token(tokens[3], karta.tileY) ||
                !parse_token(tokens[4], memberCount) ||
                memberCount < 0)
                return std::nullopt;
            karta.members.reserve(static_cast<size_t>(memberCount));
            snapshot.kartas.push_back(std::move(karta));
            currentKarta = &snapshot.kartas.back();
            continue;
        }

        if (tokens[0] == "MEMBER") {
            if (!currentKarta)
                return std::nullopt;

            MonitorMemberSnapshot member;
            if (tokens.size() != 4 ||
                !parse_token(tokens[1], member.monitorId) ||
                !parse_token(tokens[2], member.workspaceId) ||
                !parse_bool_token(tokens[3], member.special))
                return std::nullopt;
            currentKarta->members.push_back(std::move(member));
            continue;
        }

        return std::nullopt;
    }

    if (!sawVersion || !sawActive)
        return std::nullopt;

    return snapshot;
}

} // namespace KartaRepoSnapshot
