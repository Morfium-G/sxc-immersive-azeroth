#include "sky/BodyTables.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include "TableWatch.h"

namespace ia::sky::bodytables {
    namespace {
        enum Scope : uint32_t { kGlobal = 0, kMap = 1, kArea = 2 };

        // Profile rows as read: BodySet 0 inherits, the flags inherit below 0, NorthOffset at -1000,
        // LoreYear at -1000000 (lore years go far below 0), the other lore columns below 0,
        // LoreStartDate at 0.
        constexpr int kLoreYearInherit = -1000000;

        struct RawProfile {
            int bodySet = 0;
            int hideZone = -1, hideAll = -1;
            float north = -1000.0f;
            int loreYear = kLoreYearInherit;
            float wobble = -1.0f;
            int wobbleMode = -1;
            float yearsPerDay = -1.0f;
            int loreStart = 0;
        };

        struct Set {
            uint32_t id = 0;
            std::string name;
            uint32_t parent = 0;
        };

        struct Member {
            uint32_t set = 0, body = 0, flags = 0;
        };

        TableWatch g_watch[] = { TableWatch("SkyProfile"), TableWatch("SkyBodySet"), TableWatch("SkyBodySetMember"),
                                 TableWatch("SkyBody"), TableWatch("SkyPath"), TableWatch("SkyPathKey") };
        int g_defaultLoreYear = 27;
        uint32_t g_generation = 1;
        std::map<std::pair<uint32_t, uint32_t>, RawProfile> g_profiles;
        std::map<std::pair<uint32_t, int>, Profile> g_profileCache;
        std::map<uint32_t, Set> g_sets;
        std::vector<Member> g_members;
        std::map<uint32_t, Body> g_bodies;
        std::map<uint32_t, Path> g_paths;
        std::map<uint32_t, std::vector<PathKey>> g_keys;
        std::map<int, std::vector<const Body*>> g_setCache;
        std::string g_status = "not loaded yet";

        // Field indices of a table, by name.
        struct Fields {
            const sxc::data::Table& t;
            int operator[](const char* name) const { return t.Field(name); }
        };

        std::string Str(const sxc::data::Row& r, int field) {
            return std::string(r.String(field));
        }

        void Load() {
            g_profiles.clear();
            g_profileCache.clear();
            g_sets.clear();
            g_members.clear();
            g_bodies.clear();
            g_paths.clear();
            g_keys.clear();
            g_setCache.clear();
            g_generation++;

            const sxc::data::Table& profiles = g_watch[0].Table();
            const sxc::data::Table& sets = g_watch[1].Table();
            const sxc::data::Table& members = g_watch[2].Table();
            const sxc::data::Table& bodies = g_watch[3].Table();
            const sxc::data::Table& paths = g_watch[4].Table();
            const sxc::data::Table& keys = g_watch[5].Table();

            {
                const Fields f{ profiles };
                const int scopeType = f["ScopeType"], scopeId = f["ScopeID"], bodySet = f["BodySet"], hideZone = f["HideZoneSkybox"],
                          hideAll = f["HideAllSkyboxes"], north = f["NorthOffset"], loreYear = f["LoreYear"], wobble = f["LoreWobbleYears"],
                          wobbleMode = f["LoreWobbleMode"], yearsPerDay = f["LoreYearsPerDay"], loreStart = f["LoreStartDate"];

                for (uint32_t i = 0; i < profiles.RowCount(); i++) {
                    const sxc::data::Row r = profiles.At(i);
                    const uint32_t scope = r.Uint(scopeType);
                    RawProfile p;
                    p.bodySet = r.Int(bodySet);
                    p.hideZone = r.Int(hideZone);
                    p.hideAll = r.Int(hideAll);
                    p.north = r.Float(north);
                    p.loreYear = r.Int(loreYear);
                    p.wobble = r.Float(wobble);
                    p.wobbleMode = r.Int(wobbleMode);
                    p.yearsPerDay = r.Float(yearsPerDay);
                    p.loreStart = r.Int(loreStart);
                    g_profiles[{ scope, scope == kGlobal ? 0u : r.Uint(scopeId) }] = p;
                }
            }

            {
                const Fields f{ sets };
                const int id = f["ID"], name = f["Name"], parent = f["ParentSet"];

                for (uint32_t i = 0; i < sets.RowCount(); i++) {
                    const sxc::data::Row r = sets.At(i);
                    Set s;
                    s.id = r.Uint(id);
                    s.name = Str(r, name);
                    s.parent = r.Uint(parent);
                    g_sets[s.id] = s;
                }
            }

            {
                const Fields f{ members };
                const int set = f["SetID"], body = f["BodyID"], flags = f["Flags"];

                for (uint32_t i = 0; i < members.RowCount(); i++) {
                    const sxc::data::Row r = members.At(i);
                    g_members.push_back({ r.Uint(set), r.Uint(body), r.Uint(flags) });
                }
            }

            {
                const Fields f{ bodies };
                const int id = f["ID"], name = f["Name"], texture = f["Texture"], role = f["StockRole"], size = f["Size"], color = f["Color"],
                          blend = f["Blend"], order = f["Order"], path = f["PathID"], period = f["PeriodDays"], phase = f["PhaseDays"],
                          visPeriod = f["VisiblePeriodDays"], visDays = f["VisibleDays"], visOffset = f["VisibleOffsetDays"],
                          ease = f["VisibleEase"], easeSeconds = f["VisibleEaseSeconds"], litMode = f["LitMode"], litPeriod = f["LitPeriodDays"],
                          lightColor = f["LightColor"], lightStrength = f["LightStrength"], flags = f["Flags"], clock = f["Clock"],
                          dayVisibility = f["DayVisibility"], rotation = f["Rotation"], rotationPerDay = f["RotationPerDay"];

                for (uint32_t i = 0; i < bodies.RowCount(); i++) {
                    const sxc::data::Row r = bodies.At(i);
                    Body b;
                    b.id = r.Uint(id);
                    b.name = Str(r, name);
                    b.texture = Str(r, texture);
                    b.role = static_cast<StockRole>(r.Int(role));
                    b.size = r.Float(size);
                    b.color = r.Uint(color);
                    b.blend = r.Int(blend);
                    b.order = r.Int(order);
                    b.pathId = r.Uint(path);
                    b.periodDays = r.Float(period);
                    b.phaseDays = r.Float(phase);
                    b.visiblePeriodDays = r.Float(visPeriod);
                    b.visibleDays = r.Float(visDays);
                    b.visibleOffsetDays = r.Float(visOffset);
                    b.visibleEase = r.Int(ease);
                    b.visibleEaseSeconds = r.Float(easeSeconds);
                    b.litMode = r.Int(litMode);
                    b.litPeriodDays = r.Float(litPeriod);
                    b.lightColor = r.Uint(lightColor);
                    b.lightStrength = r.Float(lightStrength);
                    b.flags = r.Uint(flags);
                    b.clock = r.Int(clock);
                    b.dayVisibility = r.Float(dayVisibility);
                    b.rotation = r.Float(rotation);
                    b.rotationPerDay = r.Float(rotationPerDay);
                    b.row = i;
                    g_bodies[b.id] = b;
                }
            }

            {
                const Fields f{ paths };
                const int id = f["ID"], name = f["Name"], type = f["Type"], frame = f["Frame"], rise = f["RiseAzimuth"], lift = f["Lift"],
                          height = f["Height"], follow = f["FollowBody"], sepMin = f["SeparationMin"], sepMax = f["SeparationMax"],
                          sepDays = f["SeparationDays"], sepAngle = f["SeparationAngle"], wobble = f["WobbleDegrees"], wobbleDays = f["WobbleDays"],
                          drift = f["DriftDegreesPerDay"], pulse = f["SizePulse"], pulseDays = f["SizePulseDays"], flags = f["Flags"];

                for (uint32_t i = 0; i < paths.RowCount(); i++) {
                    const sxc::data::Row r = paths.At(i);
                    Path p;
                    p.id = r.Uint(id);
                    p.name = Str(r, name);
                    p.type = static_cast<PathType>(r.Int(type));
                    p.frame = r.Int(frame);
                    p.riseAzimuth = r.Float(rise);
                    p.lift = r.Float(lift);
                    p.height = r.Float(height);
                    p.followBody = r.Uint(follow);
                    p.separationMin = r.Float(sepMin);
                    p.separationMax = r.Float(sepMax);
                    p.separationDays = r.Float(sepDays);
                    p.separationAngle = r.Float(sepAngle);
                    p.wobbleDegrees = r.Float(wobble);
                    p.wobbleDays = r.Float(wobbleDays);
                    p.driftDegreesPerDay = r.Float(drift);
                    p.sizePulse = r.Float(pulse);
                    p.sizePulseDays = r.Float(pulseDays);
                    p.flags = r.Uint(flags);
                    g_paths[p.id] = p;
                }
            }

            {
                const Fields f{ keys };
                const int path = f["PathID"], time = f["Time"], azimuth = f["Azimuth"], elevation = f["Elevation"], size = f["Size"],
                          leader = f["LeaderBrightness"], order = f["Order"];

                for (uint32_t i = 0; i < keys.RowCount(); i++) {
                    const sxc::data::Row r = keys.At(i);
                    PathKey k;
                    k.time = r.Float(time);
                    k.azimuth = r.Float(azimuth);
                    k.elevation = r.Float(elevation);
                    k.size = r.Float(size);

                    if (!(k.size > 0.0f)) {
                        k.size = 1.0f;
                    }

                    k.leaderBrightness = r.Float(leader);

                    if (!(k.leaderBrightness > 0.0f)) {
                        k.leaderBrightness = 1.0f;
                    }

                    k.order = r.Int(order);
                    k.time -= static_cast<float>(static_cast<int>(k.time)); // 0..1

                    if (k.time < 0.0f) {
                        k.time += 1.0f;
                    }

                    g_keys[r.Uint(path)].push_back(k);
                }

                for (auto& entry : g_keys) {
                    std::stable_sort(entry.second.begin(), entry.second.end(), [](const PathKey& a, const PathKey& b) { return a.time < b.time; });
                }
            }

            char line[192];

            if (!profiles.Loaded()) {
                std::snprintf(line, sizeof(line), "no SkyProfile.cdbc: stock sun and moons");
            } else {
                std::snprintf(line, sizeof(line), "%u profile row(s), %u set(s), %u body(ies), %zu member(s), %zu path(s), %u path key(s)",
                              profiles.RowCount(), sets.RowCount(), bodies.RowCount(), g_members.size(), g_paths.size(), keys.RowCount());
            }

            g_status = line;
        }
    }

    void Update() {
        bool changed = false;

        for (TableWatch& w : g_watch) {
            changed = w.Changed() || changed;
        }

        if (changed) {
            Load();
        }
    }

    void SetDefaultLoreYear(int year) {
        if (year != g_defaultLoreYear) {
            g_defaultLoreYear = year;
            g_profileCache.clear();
        }
    }

    int DefaultLoreYear() { return g_defaultLoreYear; }
    const char* Status() { return g_status.c_str(); }
    uint32_t Generation() { return g_generation; }

    const Profile& ProfileHere(const world::Snapshot& snap) {
        static const Profile kNone;

        if (!snap.inWorld) {
            return kNone;
        }

        const uint32_t area = snap.areaCount > 0 ? snap.areaChain[0] : 0;
        const auto key = std::make_pair(area, snap.mapId);

        if (auto it = g_profileCache.find(key); it != g_profileCache.end()) {
            return it->second;
        }

        std::pair<uint32_t, uint32_t> places[world::kMaxAreaChain + 2];
        int n = 0;

        for (int i = 0; i < snap.areaCount && i < world::kMaxAreaChain; i++) {
            places[n++] = { kArea, snap.areaChain[i] };
        }

        if (snap.mapId >= 0) {
            places[n++] = { kMap, static_cast<uint32_t>(snap.mapId) };
        }

        places[n++] = { kGlobal, 0 };

        Profile p;
        p.loreYear = g_defaultLoreYear;
        bool setSet = false, zoneSet = false, allSet = false, northSet = false;
        bool yearSet = false, wobbleSet = false, modeSet = false, speedSet = false, startSet = false;

        for (int i = 0; i < n; i++) {
            auto it = g_profiles.find(places[i]);

            if (it == g_profiles.end()) {
                continue;
            }

            const RawProfile& r = it->second;
            bool used = false;

            if (!setSet && r.bodySet != 0) { setSet = true; used = true; p.bodySet = r.bodySet; }
            if (!zoneSet && r.hideZone >= 0) { zoneSet = true; used = true; p.hideZoneSkybox = r.hideZone != 0; }
            if (!allSet && r.hideAll >= 0) { allSet = true; used = true; p.hideAllSkyboxes = r.hideAll != 0; }
            if (!northSet && r.north > -1000.0f) { northSet = true; used = true; p.northOffset = r.north; }
            if (!yearSet && r.loreYear != kLoreYearInherit) { yearSet = true; used = true; p.loreYear = r.loreYear; }
            if (!wobbleSet && r.wobble >= 0.0f) { wobbleSet = true; used = true; p.loreWobbleYears = r.wobble; }
            if (!modeSet && r.wobbleMode >= 0) { modeSet = true; used = true; p.loreWobbleMode = r.wobbleMode; }
            if (!speedSet && r.yearsPerDay >= 0.0f) { speedSet = true; used = true; p.loreYearsPerDay = r.yearsPerDay; }
            if (!startSet && r.loreStart != 0) { startSet = true; used = true; p.loreStartDate = r.loreStart; }

            if (used && p.scopeType < 0) {
                p.scopeType = static_cast<int>(places[i].first);
                p.scopeId = places[i].second;
            }
        }

        if (g_profileCache.size() > 4096) {
            g_profileCache.clear();
        }

        return g_profileCache.emplace(key, p).first->second;
    }

    const std::vector<const Body*>& SetBodies(int setId) {
        static const std::vector<const Body*> kEmpty;

        if (setId <= 0) {
            return kEmpty;
        }

        if (auto it = g_setCache.find(setId); it != g_setCache.end()) {
            return it->second;
        }

        // The set and its parents (child first), guarding against loops.
        std::vector<uint32_t> chain;
        std::set<uint32_t> seen;

        for (uint32_t s = static_cast<uint32_t>(setId); s && !seen.count(s) && g_sets.count(s); s = g_sets[s].parent) {
            seen.insert(s);
            chain.push_back(s);
        }

        // Root first, so a child's member row (e.g. switching a body off) overrides its parent's.
        std::map<uint32_t, bool> on;

        for (auto c = chain.rbegin(); c != chain.rend(); ++c) {
            for (const Member& m : g_members) {
                if (m.set == *c) {
                    on[m.body] = (m.flags & 1) == 0;
                }
            }
        }

        std::vector<const Body*> out;

        for (const auto& entry : on) {
            if (!entry.second) {
                continue;
            }

            if (auto b = g_bodies.find(entry.first); b != g_bodies.end()) {
                out.push_back(&b->second);
            }
        }

        std::sort(out.begin(), out.end(), [](const Body* a, const Body* b) { return a->order != b->order ? a->order < b->order : a->row < b->row; });
        return g_setCache.emplace(setId, std::move(out)).first->second;
    }

    const char* SetName(int setId) {
        auto it = g_sets.find(static_cast<uint32_t>(setId > 0 ? setId : 0));
        return it != g_sets.end() ? it->second.name.c_str() : (setId < 0 ? "stock" : "?");
    }

    const Path* FindPath(uint32_t id) {
        auto it = g_paths.find(id);
        return it != g_paths.end() ? &it->second : nullptr;
    }

    const Body* FindBody(uint32_t id) {
        auto it = g_bodies.find(id);
        return it != g_bodies.end() ? &it->second : nullptr;
    }

    const std::vector<PathKey>& PathKeys(uint32_t pathId) {
        static const std::vector<PathKey> kEmpty;
        auto it = g_keys.find(pathId);
        return it != g_keys.end() ? it->second : kEmpty;
    }
}
