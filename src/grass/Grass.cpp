#include "grass/Grass.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
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
    unsigned ActorsFed() { return g_actorsFed; }
}
