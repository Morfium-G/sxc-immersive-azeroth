#include "fog/FogSettings.h"

#include <cstdio>
#include <map>
#include <string>
#include <tuple>
#include <vector>
#include "TableWatch.h"
#include "World.h"

namespace ia::fogsettings {
    namespace {
        enum Scope : uint32_t { kGlobal = 0, kMap = 1, kArea = 2 };

        // The float columns in the order they're stored in Raw::value (Color is one column of three).
        enum Float { kStrength, kDensity, kThickness, kBuildUp, kBurnOff, kWindThin, kWindDrift, kPatchiness, kWeight, kColorR, kColorG, kColorB, kSaturation, kBrightness, kFloats };
        constexpr const char* kScalarColumns[] = { "Strength", "Density", "Thickness", "BuildUp", "BurnOff", "WindThin", "WindDrift", "Patchiness", "Weight" };

        struct Raw {
            uint32_t kind = 0;   // table numbering: 0 all kinds, 1..5 = fog::Kind + 1
            float value[kFloats] = {};
            int colorMode = -1;  // -1 = from the next row
            uint32_t flags = 0;
        };

        TableWatch g_table("FogSettings");
        std::map<std::pair<uint32_t, uint32_t>, std::vector<Raw>> g_rows; // (scope type, id) -> rows
        std::map<std::tuple<uint32_t, int, int>, Values> g_cache;
        unsigned g_rowCount = 0;
        std::string g_status = "not loaded yet";
        uint32_t g_generation = 1;

        const Raw* RowFor(const std::vector<Raw>& rows, uint32_t kind) {
            for (const Raw& r : rows) {
                if (r.kind == kind) {
                    return &r;
                }
            }

            return nullptr;
        }

        void Fill(Values& v, const Raw& r, bool set[kFloats + 1]) {
            float* targets[kFloats] = { &v.strength, &v.density, &v.thickness, &v.buildUp, &v.burnOff, &v.windThin, &v.windDrift, &v.patchiness, &v.weight,
                                        &v.color[0], &v.color[1], &v.color[2], &v.saturation, &v.brightness };

            for (int f = 0; f < kFloats; f++) {
                if (!set[f] && r.value[f] >= 0.0f) {
                    *targets[f] = r.value[f];
                    set[f] = true;
                }
            }

            if (!set[kFloats] && r.colorMode >= 0) {
                v.colorMode = r.colorMode;
                set[kFloats] = true;
            }
        }

        void Load() {
            g_rows.clear();
            g_cache.clear();
            g_rowCount = 0;
            g_generation++;
            const sxc::data::Table& t = g_table.Table();

            if (!t.Loaded()) {
                g_status = "no FogSettings.cdbc: built-in fog settings";
                return;
            }

            int scalar[kWeight + 1];

            for (int f = 0; f <= kWeight; f++) {
                scalar[f] = t.Field(kScalarColumns[f]);
            }

            const int scopeType = t.Field("ScopeType"), scopeId = t.Field("ScopeID"), kind = t.Field("Kind"), colorMode = t.Field("ColorMode"),
                      color = t.Field("Color"), saturation = t.Field("Saturation"), brightness = t.Field("Brightness"), flags = t.Field("Flags");

            for (uint32_t i = 0; i < t.RowCount(); i++) {
                const sxc::data::Row rec = t.At(i);
                const uint32_t scope = rec.Uint(scopeType);
                const uint32_t id = scope == kGlobal ? 0 : rec.Uint(scopeId);
                Raw r;
                r.kind = rec.Uint(kind);

                for (int f = 0; f <= kWeight; f++) {
                    r.value[f] = rec.Float(scalar[f]);
                }

                for (int c = 0; c < 3; c++) {
                    r.value[kColorR + c] = rec.Float(color, c);
                }

                r.value[kSaturation] = rec.Float(saturation);
                r.value[kBrightness] = rec.Float(brightness);
                r.colorMode = rec.Int(colorMode);
                r.flags = rec.Uint(flags);
                g_rows[{ scope, id }].push_back(r);
                g_rowCount++;
            }

            char line[64];
            std::snprintf(line, sizeof(line), "%u row(s)", g_rowCount);
            g_status = line;
        }
    }

    Values Defaults(int kind) {
        Values v;

        // Kinds as fog::Kind: mist, water fog, steam, area fog, haze.
        switch (kind) {
            case 0: v.buildUp = 300.0f; v.burnOff = 900.0f; v.thickness = 1.5f; v.density = 0.06f; v.weight = 0.7f; v.windThin = 0.4f; break;
            case 1: v.buildUp = 300.0f; v.burnOff = 1200.0f; v.thickness = 3.0f; v.density = 0.05f; v.weight = 0.4f; v.windThin = 0.5f; break;
            case 2: v.buildUp = 10.0f; v.burnOff = 30.0f; v.thickness = 2.0f; v.density = 0.08f; v.weight = 0.0f; v.windThin = 0.8f; break;
            case 3: v.buildUp = 600.0f; v.burnOff = 1800.0f; v.thickness = 8.0f; v.density = 0.03f; v.weight = 0.3f; v.windThin = 0.7f; break;
            case 4: v.buildUp = 120.0f; v.burnOff = 600.0f; v.thickness = 20.0f; v.density = 0.01f; v.weight = 0.0f; v.windThin = 1.0f; break;
            default: break;
        }

        return v;
    }

    void Update() {
        if (g_table.Changed()) {
            Load();
        }
    }

    const Values& Resolve(uint32_t areaId, int mapId, int kind) {
        const auto key = std::make_tuple(areaId, mapId, kind);

        if (auto it = g_cache.find(key); it != g_cache.end()) {
            return it->second;
        }

        std::pair<uint32_t, uint32_t> places[world::kMaxAreaChain + 2];
        int n = 0;
        uint32_t chain[world::kMaxAreaChain];
        const int c = world::AreaChain(areaId, chain, world::kMaxAreaChain);

        for (int i = 0; i < c; i++) {
            places[n++] = { kArea, chain[i] };
        }

        if (mapId >= 0) {
            places[n++] = { kMap, static_cast<uint32_t>(mapId) };
        }

        places[n++] = { kGlobal, 0 };
        Values v = Defaults(kind);
        bool set[kFloats + 1] = {};
        bool flagsSet = false;

        for (int i = 0; i < n; i++) {
            auto rows = g_rows.find(places[i]);

            if (rows == g_rows.end()) {
                continue;
            }

            // The kind's own row first, then the "all kinds" row, at each place.
            const Raw* order[2] = { kind >= 0 ? RowFor(rows->second, static_cast<uint32_t>(kind + 1)) : nullptr, RowFor(rows->second, 0) };

            for (const Raw* r : order) {
                if (!r) {
                    continue;
                }

                Fill(v, *r, set);

                if (!flagsSet) {
                    v.flags = r->flags;
                    flagsSet = true;
                }
            }
        }

        // A colour without a mode means "use this colour": hue mode (keeps the stock colour's
        // brightness). Stock mode would ignore the colour, which reads as "the colour does nothing".
        if (!set[kFloats] && (set[kColorR] || set[kColorG] || set[kColorB])) {
            v.colorMode = kColorHue;
        }

        if (v.colorMode < kColorStock || v.colorMode > kColorReplace) {
            v.colorMode = kColorStock;
        }

        if (g_cache.size() > 4096) {
            g_cache.clear();
        }

        return g_cache.emplace(key, v).first->second;
    }

    const char* Status() { return g_status.c_str(); }
    uint32_t Generation() { return g_generation; }
}
