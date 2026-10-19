#include "env/CoverTable.h"

#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>
#include "TableWatch.h"
#include "env/Materials.h"

namespace ia::covertable {
    namespace {
        // The float fields that inherit with -1, in Values order. TintStrength carries TintColor with
        // it (the row that decides the strength decides the colour); Depth carries Flags the same way
        // (0 is a valid Flags value, so it can't inherit on its own).
        enum Field { kDepth, kMaxSlope, kSlopeFade, kDrift, kBreakup, kRim, kRelax, kTint, kZOffset, kWetness, kMeltKept, kMeltGone, kMoistureMin, kFieldCount };
        constexpr const char* kFloatColumns[kFieldCount] = {
            "Depth", "MaxSlope", "SlopeFade", "DriftNoise", "EdgeBreakup", "Rim", "RelaxSeconds", "TintStrength",
            "ZOffset", "Wetness", "MeltKeptShare", "MeltGoneTemperature", "MoistureMin",
        };

        // Whether a row sets field f. ZOffset is signed: -1000 and below inherit (the rule for every
        // signed column), and so does exactly -1 (what older files wrote). MeltGoneTemperature is signed too.
        bool IsSet(int f, float v) {
            if (f == kZOffset) return v != -1.0f && v > -1000.0f;
            if (f == kMeltGone) return v > -1000.0f;
            return v >= 0.0f;
        }

        struct Row {
            float value[kFieldCount] = {};
            uint32_t tintColor = 0;
            uint32_t flags = 0;
            uint32_t coverMaterial = 0; // 0 = from the parent material's row
            int coverTexture = -1;      // -1 = from the parent material's row, 0 = none, 1.. = ID
        };

        TableWatch g_table("SurfaceCover");
        uint32_t g_materialGeneration = 0;

        std::vector<std::string> g_coverPaths; // CoverTexture paths in the order the table first names them; ID = index + 1
        std::vector<bool> g_coverIgnoreSpecular;
        std::unordered_map<uint32_t, Row> g_rows;     // by MaterialID
        std::unordered_map<uint32_t, Values> g_cache; // by MaterialID, resolved
        std::string g_status = "not loaded yet";
        uint32_t g_generation = 1;

        std::string Normalize(const std::string& path) {
            std::string s = path;

            for (char& c : s) {
                if (c == '/') {
                    c = '\\';
                } else if (c >= 'A' && c <= 'Z') {
                    c = static_cast<char>(c - 'A' + 'a');
                }
            }

            return s;
        }

        void Load() {
            g_rows.clear();
            g_cache.clear();
            g_coverPaths.clear();
            g_coverIgnoreSpecular.clear();
            g_generation++;
            const sxc::data::Table& t = g_table.Table();

            if (!t.Loaded()) {
                g_status = std::string("no SurfaceCover.cdbc; ") + materials::Status();
                return;
            }

            int columns[kFieldCount];

            for (int f = 0; f < kFieldCount; f++) {
                columns[f] = t.Field(kFloatColumns[f]);
            }

            const int material = t.Field("MaterialID"), coverMaterial = t.Field("CoverMaterial"), tint = t.Field("TintColor"), flags = t.Field("Flags"),
                      texture = t.Field("CoverTexture");

            for (uint32_t i = 0; i < t.RowCount(); i++) {
                const sxc::data::Row rec = t.At(i);
                Row r;

                for (int f = 0; f < kFieldCount; f++) {
                    r.value[f] = rec.Float(columns[f]);
                }

                r.tintColor = rec.Uint(tint);
                r.flags = rec.Uint(flags);
                r.coverMaterial = rec.Uint(coverMaterial);

                if (r.coverMaterial == 0xFFFFFFFFu) {
                    r.coverMaterial = 0;
                }

                // CoverTexture: empty = from the parent material, "-" = none, else a path sharing an ID
                // with every row that names the same file.
                const std::string cover(rec.String(texture));

                if (cover == "-") {
                    r.coverTexture = 0;
                } else if (!cover.empty()) {
                    const std::string key = Normalize(cover);
                    int id = 0;

                    for (size_t k = 0; k < g_coverPaths.size() && !id; k++) {
                        if (Normalize(g_coverPaths[k]) == key) {
                            id = static_cast<int>(k) + 1;
                        }
                    }

                    if (!id) {
                        g_coverPaths.push_back(cover);
                        g_coverIgnoreSpecular.push_back(false);
                        id = static_cast<int>(g_coverPaths.size());
                    }

                    r.coverTexture = id;

                    if (r.flags & kFlagIgnoreSpecular) {
                        g_coverIgnoreSpecular[id - 1] = true;
                    }
                }

                g_rows[rec.Uint(material)] = r;
            }

            char line[200];
            std::snprintf(line, sizeof(line), "%u cover row(s), %u cover texture(s); %s", static_cast<unsigned>(g_rows.size()),
                          static_cast<unsigned>(g_coverPaths.size()), materials::Status());
            g_status = line;
        }

        // A material's cover: each field from the first row along its Parent chain that sets it.
        Values ForMaterial(uint32_t material) {
            if (auto it = g_cache.find(material); it != g_cache.end()) {
                return it->second;
            }

            float value[kFieldCount] = {};
            bool set[kFieldCount] = {};
            uint32_t tint = 0, flags = 0, coverMaterial = 0;
            int cover = -1;

            materials::ForChain(material, [&](uint32_t id) {
                auto it = g_rows.find(id);

                if (it == g_rows.end()) {
                    return true;
                }

                const Row& r = it->second;

                if (cover < 0 && r.coverTexture >= 0) {
                    cover = r.coverTexture;
                }

                if (!coverMaterial && r.coverMaterial) {
                    coverMaterial = r.coverMaterial;
                }

                for (int f = 0; f < kFieldCount; f++) {
                    if (!set[f] && IsSet(f, r.value[f])) {
                        set[f] = true;
                        value[f] = r.value[f];

                        if (f == kTint) tint = r.tintColor;
                        if (f == kDepth) flags = r.flags;
                    }
                }

                return true;
            });

            Values v; // fields no row sets keep the defaults
            v.material = material;

            if (set[kDepth]) {
                v.depth = value[kDepth];
                v.flags = flags;
            }

            if (set[kMaxSlope]) v.maxSlope = value[kMaxSlope];
            if (set[kSlopeFade]) v.slopeFade = value[kSlopeFade];
            if (set[kDrift]) v.driftNoise = value[kDrift];
            if (set[kBreakup]) v.edgeBreakup = value[kBreakup];
            if (set[kRim]) v.rim = value[kRim];
            if (set[kRelax]) v.relaxSeconds = value[kRelax];

            if (set[kTint]) {
                v.tintStrength = value[kTint];
                v.tintColor = tint;
            }

            if (set[kZOffset]) v.zOffset = value[kZOffset];
            if (set[kWetness]) v.wetness = value[kWetness] > 1.0f ? 1.0f : value[kWetness];
            if (set[kMeltKept]) v.meltKeptShare = value[kMeltKept] > 1.0f ? 1.0f : value[kMeltKept];
            if (set[kMeltGone]) v.meltGoneTemperature = value[kMeltGone];
            if (set[kMoistureMin]) v.moistureMin = value[kMoistureMin];
            if (cover > 0) v.coverTexture = cover;

            // What lies on top is the cover's own material; without one, the ground's. Its stiffness
            // decides how far feet press the cover down.
            v.coverMaterial = coverMaterial ? coverMaterial : material;
            v.stiffness = materials::Get(v.coverMaterial).stiffness;
            g_cache.emplace(material, v);
            return v;
        }
    }

    void Update() {
        const bool changed = g_table.Changed();

        if (changed || g_materialGeneration != materials::Generation()) {
            g_materialGeneration = materials::Generation();
            Load();
        }
    }

    Values Resolve(uint32_t areaId, int mapId, const char* texturePath, uint32_t groundEffectId, int terrainType) {
        const uint32_t material = materials::Select(areaId, mapId, texturePath, groundEffectId, terrainType);
        return material ? ForMaterial(material) : Values{};
    }

    bool IsSnow(uint32_t coverMaterial, uint32_t areaId, int mapId) {
        if (!coverMaterial) {
            return false;
        }

        constexpr int kSnowTerrainType = 3;
        const uint32_t global = materials::Select(0, -1, nullptr, 0, kSnowTerrainType);
        const uint32_t local = materials::Select(areaId, mapId, nullptr, 0, kSnowTerrainType);

        if (!global && !local) {
            return false;
        }

        bool snow = false;

        materials::ForChain(coverMaterial, [&](uint32_t id) {
            if (id == global || id == local) {
                snow = true;
                return false;
            }

            return true;
        });

        return snow;
    }

    Values SnowLook(uint32_t areaId, int mapId, const char* texturePath, uint32_t groundEffectId, int terrainType) {
        const Values own = Resolve(areaId, mapId, texturePath, groundEffectId, terrainType);

        if (own.material && IsSnow(own.coverMaterial, areaId, mapId)) {
            return own;
        }

        if (const uint32_t fallback = materials::Select(areaId, mapId, nullptr, 0, 3)) {
            return ForMaterial(fallback);
        }

        return Values{};
    }

    int CoverTextureCount() { return static_cast<int>(g_coverPaths.size()); }

    const char* CoverTexturePath(int index) {
        return index >= 1 && index <= static_cast<int>(g_coverPaths.size()) ? g_coverPaths[index - 1].c_str() : "";
    }

    bool CoverTextureIgnoresSpecular(int index) {
        return index >= 1 && index <= static_cast<int>(g_coverIgnoreSpecular.size()) && g_coverIgnoreSpecular[index - 1];
    }

    unsigned RowCount() { return static_cast<unsigned>(g_rows.size()); }
    const char* Status() { return g_status.c_str(); }
    uint32_t Generation() { return g_generation; }
}
