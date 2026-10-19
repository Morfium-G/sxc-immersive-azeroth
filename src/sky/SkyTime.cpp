#include "sky/SkyTime.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include "TableWatch.h"

namespace ia::sky::skytime {
    namespace {
        enum Scope : uint32_t { kGlobal = 0, kMap = 1, kArea = 2 };

        // -1 inherits PinnedHour (-2 and below: explicitly not pinned), -1000 inherits OffsetHours, any
        // negative BlendSeconds inherits, PinnedDay 0 inherits (-1: the realm date's day).
        struct Raw {
            float pinnedHour = -1.0f, offsetHours = -1000.0f, blendSeconds = -1.0f;
            int pinnedDay = 0;
        };

        TableWatch g_table("SkyTime");
        std::map<std::pair<uint32_t, uint32_t>, Raw> g_rows;
        std::map<std::pair<uint32_t, int>, Rule> g_cache;
        std::string g_status = "not loaded yet";
        char g_describe[128] = "normal time";
        Rule g_last;

        void Load() {
            g_rows.clear();
            g_cache.clear();
            const sxc::data::Table& t = g_table.Table();

            if (!t.Loaded()) {
                g_status = "no SkyTime.cdbc: stock time everywhere";
                return;
            }

            const int scopeType = t.Field("ScopeType"), scopeId = t.Field("ScopeID"), pinned = t.Field("PinnedHour"), offset = t.Field("OffsetHours"),
                      blend = t.Field("BlendSeconds"), day = t.Field("PinnedDay");

            for (uint32_t i = 0; i < t.RowCount(); i++) {
                const sxc::data::Row row = t.At(i);
                const uint32_t scope = row.Uint(scopeType);
                Raw r;
                r.pinnedHour = row.Float(pinned);
                r.offsetHours = row.Float(offset);
                r.blendSeconds = row.Float(blend);
                r.pinnedDay = row.Int(day);
                g_rows[{ scope, scope == kGlobal ? 0u : row.Uint(scopeId) }] = r;
            }

            char line[64];
            std::snprintf(line, sizeof(line), "%u row(s)", t.RowCount());
            g_status = line;
        }
    }

    const char* Status() { return g_status.c_str(); }

    const char* Describe() {
        static const char* const kScope[] = { "global", "map", "area" };
        const Rule& r = g_last;
        const char* scope = r.scopeType >= 0 && r.scopeType <= 2 ? kScope[r.scopeType] : "-";

        if (r.pinned) {
            std::snprintf(g_describe, sizeof(g_describe), "pinned at %02d:%02d (%s %u row)", static_cast<int>(r.pinMinute) / 60,
                          static_cast<int>(r.pinMinute) % 60, scope, r.scopeId);
        } else if (r.offsetMinutes != 0.0f) {
            std::snprintf(g_describe, sizeof(g_describe), "shifted %+.1f h (%s %u row)", r.offsetMinutes / 60.0f, scope, r.scopeId);
        } else {
            std::snprintf(g_describe, sizeof(g_describe), "normal time");
        }

        if (r.pinnedDay > 0) {
            const size_t len = std::strlen(g_describe);
            std::snprintf(g_describe + len, sizeof(g_describe) - len, ", date pinned to day %d of the year", r.pinnedDay);
        }

        return g_describe;
    }

    const Rule& Here(const world::Snapshot& snap) {
        g_last = Resolve(snap);
        return g_last;
    }

    const Rule& Resolve(const world::Snapshot& snap) {
        static const Rule kNone;

        if (g_table.Changed()) {
            Load();
        }

        if (!snap.inWorld) {
            return kNone;
        }

        const uint32_t area = snap.areaCount > 0 ? snap.areaChain[0] : 0;
        const auto key = std::make_pair(area, snap.mapId);

        if (auto it = g_cache.find(key); it != g_cache.end()) {
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

        Rule r;
        bool pinSet = false, offsetSet = false, blendSet = false, daySet = false;

        for (int i = 0; i < n; i++) {
            auto it = g_rows.find(places[i]);

            if (it == g_rows.end()) {
                continue;
            }

            const Raw& raw = it->second;
            bool used = false;
            const bool inheritPin = raw.pinnedHour > -1.5f && raw.pinnedHour < -0.5f; // -1

            if (!pinSet && !inheritPin) {
                pinSet = true;
                used = true;
                r.pinned = raw.pinnedHour >= 0.0f; // -2 and below: explicitly not pinned

                if (r.pinned) {
                    r.pinMinute = (raw.pinnedHour > 24.0f ? 24.0f : raw.pinnedHour) * 60.0f;
                }
            }

            if (!offsetSet && raw.offsetHours > -1000.0f) {
                offsetSet = true;
                used = true;
                r.offsetMinutes = raw.offsetHours * 60.0f;
            }

            if (!blendSet && raw.blendSeconds >= 0.0f) {
                blendSet = true;
                used = true;
                r.blendSeconds = raw.blendSeconds;
            }

            if (!daySet && raw.pinnedDay != 0) {
                daySet = true;
                used = true;
                // A lore year has no leap day: 366 reads as 365.
                r.pinnedDay = raw.pinnedDay > 0 ? (raw.pinnedDay > 365 ? 365 : raw.pinnedDay) : 0;
            }

            if (used && r.scopeType < 0) {
                r.scopeType = static_cast<int>(places[i].first);
                r.scopeId = places[i].second;
            }
        }

        if (g_cache.size() > 4096) {
            g_cache.clear();
        }

        return g_cache.emplace(key, r).first->second;
    }
}
