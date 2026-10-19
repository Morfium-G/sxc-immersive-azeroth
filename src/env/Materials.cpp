#include "env/Materials.h"

#include <cstdio>
#include <map>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>
#include "TableWatch.h"
#include "World.h"

namespace ia::materials {
    namespace {
        enum Scope : uint32_t { kGlobal = 0, kMap = 1, kArea = 2 };

        struct Material {
            std::string name;
            uint32_t parent = 0;
            float restMoisture = -1.0f, stiffness = -1.0f, absorbency = -1.0f, temperature = -1000.0f;
            uint32_t flags = 0;
        };

        struct Selector {
            std::string texture;  // normalized; empty = any texture
            uint32_t effect = 0;  // 0 = any ground effect
            int32_t terrain = -1; // -1 = any TerrainType (0 is Dirt)
            uint32_t material = 0;
        };

        TableWatch g_materialTable("GroundMaterial");
        TableWatch g_selectorTable("GroundMaterialSelector");

        std::unordered_map<uint32_t, Material> g_materials;
        std::map<std::pair<uint32_t, uint32_t>, std::vector<Selector>> g_selectors; // (scope type, scope id) -> rows
        std::map<std::pair<uint32_t, uint32_t>, std::unordered_map<uint32_t, uint32_t>> g_liquidSelectors; // place -> liquid -> material
        std::map<std::tuple<uint32_t, int, uint32_t>, uint32_t> g_liquidCache; // (area, map, liquid) -> material
        unsigned g_selectorCount = 0;
        std::string g_status = "not loaded yet";
        uint32_t g_generation = 1;

        struct Key {
            uint32_t area;
            int map;
            uint32_t effect;
            int terrain;
            std::string texture;

            bool operator==(const Key& o) const {
                return area == o.area && map == o.map && effect == o.effect && terrain == o.terrain && texture == o.texture;
            }
        };

        struct KeyHash {
            size_t operator()(const Key& k) const {
                size_t h = std::hash<std::string>()(k.texture);
                h ^= (static_cast<size_t>(k.area) * 0x9E3779B1u) ^ (static_cast<size_t>(k.map) << 7) ^ (static_cast<size_t>(k.effect) << 13) ^
                     static_cast<size_t>(k.terrain + 1);
                return h;
            }
        };

        std::unordered_map<Key, uint32_t, KeyHash> g_cache;

        // Lower case, / as \: texture paths compare the way the client's archive lookups do.
        std::string Normalize(std::string_view path) {
            std::string s(path);

            for (char& c : s) {
                if (c == '/') {
                    c = '\\';
                } else if (c >= 'A' && c <= 'Z') {
                    c = static_cast<char>(c - 'A' + 'a');
                }
            }

            return s;
        }

        // The row of one "what" level at one place, if any. Levels: 0 texture, 1 ground effect,
        // 2 TerrainType, 3 everything.
        const Selector* AtLevel(const std::vector<Selector>& rows, int level, const std::string& texture, uint32_t effect, int terrain) {
            for (const Selector& r : rows) {
                switch (level) {
                    case 0: if (!r.texture.empty() && r.texture == texture) return &r; break;
                    case 1: if (r.texture.empty() && r.effect && effect && r.effect == effect) return &r; break;
                    case 2: if (r.texture.empty() && !r.effect && r.terrain >= 0 && r.terrain == terrain) return &r; break;
                    case 3: if (r.texture.empty() && !r.effect && r.terrain < 0) return &r; break;
                }
            }

            return nullptr;
        }

        const Material* Find(uint32_t id) {
            auto it = g_materials.find(id);
            return it == g_materials.end() ? nullptr : &it->second;
        }

        void LoadMaterials() {
            g_materials.clear();
            const sxc::data::Table& t = g_materialTable.Table();

            if (!t.Loaded()) {
                return;
            }

            const int name = t.Field("Name"), parent = t.Field("Parent"), rest = t.Field("RestMoisture"), stiffness = t.Field("Stiffness"),
                      flags = t.Field("Flags"), absorbency = t.Field("Absorbency"), temperature = t.Field("Temperature");

            for (uint32_t i = 0; i < t.RowCount(); i++) {
                const sxc::data::Row r = t.At(i);
                Material m;
                m.name = std::string(r.String(name));
                m.parent = r.Uint(parent);
                m.restMoisture = r.Float(rest);
                m.stiffness = r.Float(stiffness);
                m.flags = r.Uint(flags);
                m.absorbency = r.Float(absorbency);
                m.temperature = r.Float(temperature);

                if (m.parent == 0xFFFFFFFFu) {
                    m.parent = 0; // -1 written for "none" works too
                }

                g_materials[r.Id()] = std::move(m);
            }
        }

        void LoadSelectors() {
            g_selectors.clear();
            g_liquidSelectors.clear();
            g_selectorCount = 0;
            const sxc::data::Table& t = g_selectorTable.Table();

            if (!t.Loaded()) {
                return;
            }

            const int scopeType = t.Field("ScopeType"), scopeId = t.Field("ScopeID"), texture = t.Field("TexturePath"), effect = t.Field("GroundEffectID"),
                      terrain = t.Field("TerrainType"), liquid = t.Field("LiquidType"), material = t.Field("MaterialID");

            for (uint32_t i = 0; i < t.RowCount(); i++) {
                const sxc::data::Row r = t.At(i);
                const uint32_t scope = r.Uint(scopeType);
                const uint32_t id = scope == kGlobal ? 0 : r.Uint(scopeId);
                g_selectorCount++;

                if (const uint32_t liquidType = r.Uint(liquid)) {
                    g_liquidSelectors[{ scope, id }][liquidType] = r.Uint(material);
                    continue;
                }

                Selector s;
                s.texture = Normalize(r.String(texture));
                s.effect = r.Uint(effect);
                s.terrain = r.Int(terrain);
                s.material = r.Uint(material);
                g_selectors[{ scope, id }].push_back(s);
            }
        }
    }

    void Update() {
        // Both watches are asked every frame, so each notes its own change.
        const bool materials = g_materialTable.Changed();
        const bool selectors = g_selectorTable.Changed();

        if (!materials && !selectors) {
            return;
        }

        if (materials) {
            LoadMaterials();
        }

        if (selectors) {
            LoadSelectors();
        }

        g_cache.clear();
        g_liquidCache.clear();
        g_generation++;

        char line[160];
        std::snprintf(line, sizeof(line), "%u material(s)%s, %u selector(s)%s", static_cast<unsigned>(g_materials.size()),
                      g_materialTable.Table().Loaded() ? "" : " (no GroundMaterial.cdbc)", g_selectorCount,
                      g_selectorTable.Table().Loaded() ? "" : " (no GroundMaterialSelector.cdbc)");
        g_status = line;
    }

    uint32_t Select(uint32_t areaId, int mapId, const char* texturePath, uint32_t groundEffectId, int terrainType) {
        Key key{ areaId, mapId, groundEffectId, terrainType, Normalize(texturePath ? texturePath : "") };

        if (auto it = g_cache.find(key); it != g_cache.end()) {
            return it->second;
        }

        // Places, most specific first: the cell's area chain, its map, global. The first matching row
        // wins (place first, then what). A matching row with MaterialID 0 means "no material here" and
        // still ends the search, so a zone can switch off a broader row.
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
        uint32_t material = 0;
        bool found = false;

        for (int i = 0; i < n && !found; i++) {
            auto rows = g_selectors.find(places[i]);

            if (rows == g_selectors.end()) {
                continue;
            }

            for (int level = 0; level < 4 && !found; level++) {
                if (const Selector* s = AtLevel(rows->second, level, key.texture, groundEffectId, terrainType)) {
                    material = s->material;
                    found = true;
                }
            }
        }

        if (g_cache.size() > 20000) {
            g_cache.clear();
        }

        g_cache.emplace(std::move(key), material);
        return material;
    }

    uint32_t SelectLiquid(uint32_t liquidType, uint32_t areaId, int mapId) {
        const auto key = std::make_tuple(areaId, mapId, liquidType);

        if (auto it = g_liquidCache.find(key); it != g_liquidCache.end()) {
            return it->second;
        }

        uint32_t material = 0;
        bool found = false;

        auto look = [&](uint32_t scope, uint32_t id) {
            if (found) {
                return;
            }

            auto rows = g_liquidSelectors.find({ scope, id });

            if (rows == g_liquidSelectors.end()) {
                return;
            }

            auto row = rows->second.find(liquidType);

            if (row != rows->second.end()) {
                material = row->second;
                found = true;
            }
        };

        uint32_t chain[world::kMaxAreaChain];
        const int n = world::AreaChain(areaId, chain, world::kMaxAreaChain);

        for (int i = 0; i < n; i++) {
            look(kArea, chain[i]);
        }

        if (mapId >= 0) {
            look(kMap, static_cast<uint32_t>(mapId));
        }

        look(kGlobal, 0);

        if (g_liquidCache.size() > 4096) {
            g_liquidCache.clear();
        }

        g_liquidCache.emplace(key, material);
        return material;
    }

    Values Get(uint32_t materialId) {
        Values v;
        bool moisture = false, stiffness = false, absorbency = false, temperature = false;

        ForChain(materialId, [&](uint32_t id) {
            const Material* m = Find(id);

            if (!m) {
                return false;
            }

            if (!moisture && m->restMoisture >= 0.0f) {
                v.restMoisture = m->restMoisture > 1.0f ? 1.0f : m->restMoisture;
                moisture = true;
            }

            if (!stiffness && m->stiffness >= 0.0f) {
                v.stiffness = m->stiffness > 1.0f ? 1.0f : m->stiffness;
                stiffness = true;
            }

            if (!absorbency && m->absorbency >= 0.0f) {
                v.absorbency = m->absorbency > 1.0f ? 1.0f : m->absorbency;
                absorbency = true;
            }

            if (!temperature && m->temperature > -1000.0f) {
                v.temperature = m->temperature;
                temperature = true;
            }

            if (id == materialId) {
                v.flags = m->flags;
            }

            return !(moisture && stiffness && absorbency && temperature);
        });

        v.restMoistureSet = moisture;
        v.hasTemperature = temperature;
        return v;
    }

    uint32_t Parent(uint32_t materialId) {
        const Material* m = Find(materialId);
        return m && m->parent != materialId ? m->parent : 0;
    }

    bool Exists(uint32_t materialId) { return Find(materialId) != nullptr; }

    const char* Name(uint32_t materialId) {
        const Material* m = Find(materialId);
        return m ? m->name.c_str() : "";
    }

    unsigned MaterialCount() { return static_cast<unsigned>(g_materials.size()); }
    unsigned SelectorCount() { return g_selectorCount; }
    const char* Status() { return g_status.c_str(); }
    uint32_t Generation() { return g_generation; }
}
