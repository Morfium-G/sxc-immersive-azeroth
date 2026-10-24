#include "grass/Grass.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>
#include "TableWatch.h"
#include "env/Wind.h"
#include "wxl/sdk.hpp"

namespace ia::grass {
    namespace {
        Settings g_settings;
        bool g_running = false;
        unsigned g_actorsFed = 0;
        TableWatch g_table("GroundEffectDoodadWind");
        std::string g_tableStatus = "not loaded yet";
        bool g_tableSent = false;

        constexpr int kGrid = SXC_GRASS_GRID;
        constexpr float kSpacing = 8.0f;
        constexpr float kEaseSeconds = 0.35f;

        // World-snapped cells: the ground height (so hills above and below the player sample their own
        // surface) and an eased wind (so changes blend rather than snap).
        struct Cell {
            float groundZ = 0.0f, lastSeen = 0.0f;
            float value[3] = {};
            bool haveGround = false, fresh = true;
        };
        std::unordered_map<uint64_t, Cell> g_cells;
        float g_clock = 0.0f;

        uint64_t Key(int cx, int cy) { return (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32) | static_cast<uint32_t>(cy); }

        void SendTable(const sxc_grass_api* api) {
            std::vector<sxc_grass_doodad> rows;
            const sxc::data::Table& t = g_table.Table();

            if (t) {
                const int flags = t.Field("Flags"), stiffness = t.Field("Stiffness"), root = t.Field("RootV"), tip = t.Field("TipV");

                for (uint32_t i = 0; i < t.RowCount(); i++) {
                    const sxc::data::Row r = t.At(i);
                    rows.push_back(sxc_grass_doodad{ r.Id(), r.Uint(flags), r.Float(stiffness), r.Float(root), r.Float(tip) });
                }
            }

            api->set_doodads(rows.empty() ? nullptr : rows.data(), static_cast<uint32_t>(rows.size()));
            char line[96];
            std::snprintf(line, sizeof(line), "%u override(s) in GroundEffectDoodadWind%s", static_cast<unsigned>(rows.size()), t && t.Loaded() ? "" : " (no file)");
            g_tableStatus = line;
        }

        // --- the density (step 7d; WXL's features/GrassDensity) ---------------------------------------------
        // GroundEffectDoodadDensity by the workspace rule for scoped tables: place first (the chunk's area
        // chain, its map, global), then within each place the most specific "what" (doodad and ground effect,
        // doodad, ground effect, everything). A -1 field takes its value from the next row; values are never
        // combined. No row at all: no copies. The player's limits cap the result in the client.
        enum Scope : uint32_t { kGlobal = 0, kMap = 1, kArea = 2 };

        struct DensityRow {
            uint32_t doodad = 0, effect = 0;
            float value[3] = { -1.0f, -1.0f, -1.0f };   // multiplier, radius, spread
        };

        TableWatch g_densityTable("GroundEffectDoodadDensity");
        std::map<std::pair<uint32_t, uint32_t>, std::vector<DensityRow>> g_densityRows;   // (scope type, id) -> rows
        std::map<std::tuple<uint32_t, int32_t, uint32_t, uint32_t>, std::pair<bool, sxc_grass_density>> g_densityCache;
        std::string g_densityStatus = "not loaded yet";
        bool g_providerSet = false;

        // The row of one "what" level at one place: 0 doodad and effect, 1 doodad, 2 effect, 3 everything.
        const DensityRow* AtLevel(const std::vector<DensityRow>& rows, int level, uint32_t doodad, uint32_t effect) {
            for (const DensityRow& r : rows) {
                switch (level) {
                    case 0: if (r.doodad && r.effect && r.doodad == doodad && r.effect == effect) return &r; break;
                    case 1: if (r.doodad && !r.effect && r.doodad == doodad) return &r; break;
                    case 2: if (!r.doodad && r.effect && effect && r.effect == effect) return &r; break;
                    case 3: if (!r.doodad && !r.effect) return &r; break;
                }
            }

            return nullptr;
        }

        void LoadDensity() {
            g_densityRows.clear();
            g_densityCache.clear();
            const sxc::data::Table& t = g_densityTable.Table();

            if (!t || !t.Loaded()) {
                g_densityStatus = "no GroundEffectDoodadDensity.cdbc: no copies";
                return;
            }

            const int scopeType = t.Field("ScopeType"), scopeId = t.Field("ScopeID"), doodad = t.Field("DoodadID"), effect = t.Field("GroundEffectID"),
                      multiplier = t.Field("Multiplier"), radius = t.Field("Radius"), spread = t.Field("Spread");

            for (uint32_t i = 0; i < t.RowCount(); i++) {
                const sxc::data::Row rec = t.At(i);
                const uint32_t scope = rec.Uint(scopeType);
                DensityRow r;
                r.doodad = rec.Uint(doodad);
                r.effect = rec.Uint(effect);
                r.value[0] = rec.Float(multiplier);
                r.value[1] = rec.Float(radius);
                r.value[2] = rec.Float(spread);
                g_densityRows[{ scope, scope == kGlobal ? 0 : rec.Uint(scopeId) }].push_back(r);
            }

            char line[64];
            std::snprintf(line, sizeof(line), "%u row(s) in GroundEffectDoodadDensity", t.RowCount());
            g_densityStatus = line;
        }

        // The client asks while it places a chunk's plants.
        wxl_bool WXL_CALL ResolveDensity(void*, uint32_t area, int32_t map, uint32_t doodad, uint32_t effect, sxc_grass_density* out) {
            const auto key = std::make_tuple(area, map, doodad, effect);
            auto it = g_densityCache.find(key);

            if (it == g_densityCache.end()) {
                std::pair<uint32_t, uint32_t> places[world::kMaxAreaChain + 2];
                int n = 0;
                uint32_t chain[world::kMaxAreaChain];
                const int c = world::AreaChain(area, chain, world::kMaxAreaChain);

                for (int i = 0; i < c; i++) {
                    places[n++] = { kArea, chain[i] };
                }

                if (map >= 0) {
                    places[n++] = { kMap, static_cast<uint32_t>(map) };
                }

                places[n++] = { kGlobal, 0 };
                float value[3] = { -1.0f, -1.0f, -1.0f };
                bool any = false;

                for (int i = 0; i < n; i++) {
                    const auto rows = g_densityRows.find(places[i]);

                    if (rows == g_densityRows.end()) {
                        continue;
                    }

                    for (int level = 0; level < 4; level++) {
                        if (const DensityRow* r = AtLevel(rows->second, level, doodad, effect)) {
                            any = true;

                            for (int f = 0; f < 3; f++) {
                                if (value[f] < 0.0f && r->value[f] >= 0.0f) {
                                    value[f] = r->value[f];
                                }
                            }
                        }
                    }
                }

                // WXL's values where no row gave one: no copies, no radius, 1 yd of spread.
                const sxc_grass_density v = { value[0] >= 0.0f ? value[0] : 1.0f, value[1] >= 0.0f ? value[1] : 0.0f, value[2] >= 0.0f ? value[2] : 1.0f };

                if (g_densityCache.size() > 20000) {
                    g_densityCache.clear();
                }

                it = g_densityCache.emplace(key, std::make_pair(any, v)).first;
            }

            if (!it->second.first || !out) {
                return WXL_FALSE;
            }

            *out = it->second.second;
            return WXL_TRUE;
        }

        // Whether the client knows the density (an older one doesn't).
        bool HasDensity(const sxc_grass_api* api) {
            return api->struct_size >= offsetof(sxc_grass_api, set_density_limits) + sizeof(api->set_density_limits);
        }

        bool HasEdges(const sxc_grass_api* api) {
            return api->struct_size >= offsetof(sxc_grass_api, set_edges) + sizeof(api->set_edges);
        }
    }

    Settings& Config() { return g_settings; }

    const sxc_grass_api* Api() {
        static const sxc_grass_api* s_api =
            wxl::Host() ? static_cast<const sxc_grass_api*>(wxl::Host()->get_service(SXC_GRASS_SERVICE, SXC_GRASS_SERVICE_VERSION)) : nullptr;
        return s_api;
    }

    void Update(float dt, const world::Snapshot& snap) {
        const sxc_grass_api* api = Api();

        if (!api) {
            return;
        }

        if (g_table.Changed() || !g_tableSent) {
            SendTable(api);
            g_tableSent = true;
        }

        const Settings& p = g_settings;

        // Step 7d: a changed table is answered anew (the client places its plants again); the limits every frame.
        if (HasDensity(api)) {
            if (g_densityTable.Changed() || !g_providerSet) {
                LoadDensity();
                api->set_density_provider(ResolveDensity, nullptr);
                g_providerSet = true;
            }

            sxc_grass_density_limits l = {};
            l.struct_size = sizeof(l);
            l.on = p.density ? WXL_TRUE : WXL_FALSE;
            l.max_multiplier = p.maxMultiplier;
            l.max_radius = p.maxRadius;
            l.max_spread = p.maxSpread;
            api->set_density_limits(&l);

            // The soft edges (the client places the chunks anew only when they change).
            if (HasEdges(api)) {
                sxc_grass_edges e = {};
                e.struct_size = sizeof(e);
                e.on = p.edges ? WXL_TRUE : WXL_FALSE;
                e.band = p.edgeBand;
                e.shrink = p.edgeShrink;
                e.spill = p.edgeSpill;
                e.share = p.edgeShare;
                e.same_texture = p.edgeSameTexture ? WXL_TRUE : WXL_FALSE;
                api->set_edges(&e);
            }
        } else {
            g_densityStatus = "the client has no grass density (update the client)";
        }
        sxc_grass_motion m = {};
        m.struct_size = sizeof(m);
        m.on = p.motion && snap.inWorld ? WXL_TRUE : WXL_FALSE;
        g_running = true;
        g_actorsFed = 0;

        if (!m.on) {
            api->set_motion(&m);
            return;
        }

        g_clock = std::fmod(g_clock + (dt > 0.0f && dt < 1.0f ? dt : 1.0f / 60.0f), 3600.0f);
        m.time = g_clock;
        m.anchor = p.anchor;
        m.amplitude = p.amplitude;
        m.flutter = p.flutter;
        m.flutter_speed = p.flutterSpeed;
        m.push_strength = p.pushStrength;
        m.grid_spacing = kSpacing;

        // The grid, snapped to world cells with the player near the middle.
        const int baseX = static_cast<int>(std::floor(snap.playerPos[0] / kSpacing)) - kGrid / 2;
        const int baseY = static_cast<int>(std::floor(snap.playerPos[1] / kSpacing)) - kGrid / 2;
        m.grid_origin[0] = baseX * kSpacing;
        m.grid_origin[1] = baseY * kSpacing;
        const float ease = 1.0f - std::exp(-(dt > 0.0f ? dt : 1.0f / 60.0f) / kEaseSeconds);

        for (int iy = 0; iy < kGrid; iy++) {
            for (int ix = 0; ix < kGrid; ix++) {
                Cell& c = g_cells[Key(baseX + ix, baseY + iy)];
                c.lastSeen = g_clock;
                const float x = (baseX + ix + 0.5f) * kSpacing, y = (baseY + iy + 0.5f) * kSpacing;

                if (!c.haveGround) {
                    float z;
                    c.haveGround = terrain::HeightAt(x, y, z);
                    c.groundZ = c.haveGround ? z : snap.playerPos[2];
                }

                const float pos[3] = { x, y, c.groundZ };
                const wind::Sample w = wind::At(pos);
                const float target[3] = { w.dirX * w.strength, w.dirY * w.strength, w.gust };

                for (int k = 0; k < 3; k++) {
                    c.value[k] = c.fresh ? target[k] : c.value[k] + (target[k] - c.value[k]) * ease;
                }

                c.fresh = false;
                float* cell = m.wind[iy * kGrid + ix];
                cell[0] = c.value[0];
                cell[1] = c.value[1];
                cell[2] = c.value[2];
            }
        }

        // Forget the cells left behind (the grid moves with the player).
        if (g_cells.size() > kGrid * kGrid * 4) {
            for (auto it = g_cells.begin(); it != g_cells.end();) {
                const float age = g_clock - it->second.lastSeen;
                it = age > 5.0f || age < 0.0f ? g_cells.erase(it) : std::next(it);
            }
        }

        // The characters: nearest first, their push radius from their size.
        world::Unit units[SXC_GRASS_MAX_ACTORS];
        const int n = world::Units(snap.playerPos, kActorRange, units, SXC_GRASS_MAX_ACTORS);

        for (int i = 0; i < n && g_actorsFed < SXC_GRASS_MAX_ACTORS; i++) {
            float r = units[i].effectiveRadius * p.radiusScale * (units[i].mounted ? p.mountedScale : 1.0f);
            r = r < p.minRadius ? p.minRadius : r;
            float* a = m.actors[g_actorsFed++];
            a[0] = units[i].pos[0];
            a[1] = units[i].pos[1];
            a[2] = units[i].pos[2];
            a[3] = r;
        }

        m.actor_count = g_actorsFed;
        m.player[0] = snap.playerPos[0];
        m.player[1] = snap.playerPos[1];
        m.player[2] = snap.playerPos[2];
        m.actor_range = kActorRange + 5.0f;
        api->set_motion(&m);
    }

    void Stop() {
        const sxc_grass_api* api = Api();

        if (api && g_running) {
            sxc_grass_motion m = {};
            m.struct_size = sizeof(m);
            api->set_motion(&m);
            g_running = false;
        }

        // Step 7d: the client stops asking (this module's answers may go with the mod).
        if (api && g_providerSet && HasDensity(api)) {
            api->set_density_limits(nullptr);
            api->set_density_provider(nullptr, nullptr);
        }

        if (api && HasEdges(api)) {
            api->set_edges(nullptr);
        }

        g_providerSet = false;

        g_cells.clear();
    }

    bool Available() {
        const sxc_grass_api* api = Api();
        return api && api->available();
    }

    const char* Status() {
        const sxc_grass_api* api = Api();
        return api ? api->status() : "the client has no sxc.grass (update the client)";
    }

    const char* TableStatus() { return g_tableStatus.c_str(); }
    const char* DensityStatus() { return g_densityStatus.c_str(); }

    bool HasPlacement() {
        const sxc_grass_api* api = Api();
        return api && api->struct_size >= offsetof(sxc_grass_api, placement) + sizeof(api->placement);
    }

    uint32_t Placement(float& minShare) {
        minShare = 0.3f;
        return HasPlacement() ? Api()->placement(&minShare) : SXC_GRASS_PLACE_STOCK;
    }

    void SetPlacement(uint32_t mode, float minShare) {
        if (HasPlacement()) {
            Api()->set_placement(mode, minShare);
        }
    }

    bool HasSlopes() {
        const sxc_grass_api* api = Api();
        return api && api->struct_size >= offsetof(sxc_grass_api, inspect) + sizeof(api->inspect);
    }

    void Slopes(float& minNormalZ, float& sink) {
        minNormalZ = 0.4f;
        sink = 0.0f;

        if (HasSlopes()) {
            Api()->slopes(&minNormalZ, &sink);
        }
    }

    void SetSlopes(float minNormalZ, float sink) {
        if (HasSlopes()) {
            Api()->set_slopes(minNormalZ, sink);
        }
    }

    bool Inspect(float radius, sxc_grass_inspect& out) {
        out = sxc_grass_inspect{};
        out.struct_size = sizeof(out);
        return HasSlopes() && Api()->inspect(radius, &out);
    }
    unsigned ActorsFed() { return g_actorsFed; }
}
