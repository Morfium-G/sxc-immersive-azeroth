#include "fog/Fog.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <vector>
#include "SXC/Api/sxc_fog.h"
#include "env/Wind.h"
#include "wxl/sdk.hpp"

namespace ia::fog {
    namespace {
        constexpr int kSize = 128;     // cells per side
        constexpr float kCell = 2.0f;  // yd: +-128 yd
        constexpr int kRecentre = 8;   // cells the player moves before the grid follows
        constexpr float kDensity = 0.08f; // extinction per yd of full test fog
        constexpr float kStep = 0.25f; // seconds between steps (burn-off, upload)

        struct Cell {
            int i = std::numeric_limits<int>::min(), j = 0; // the world cell this slot holds
            float amount = 0.0f;       // 0..1
            float thickness = 4.0f;    // yd
            bool pool = false;
            float poolTop = 0.0f;      // world z
            bool groundKnown = false;
            float ground = 0.0f;       // world z: the terrain or the water on it, whichever is higher
        };

        Look g_look;
        std::vector<Cell> g_cells(kSize * kSize);
        std::vector<float> g_light(kSize * kSize * 4), g_pool(kSize * kSize * 4);
        int g_firstI = 0, g_firstJ = 0;
        bool g_haveGrid = false;
        int g_map = -1;
        float g_burnOff = 120.0f;
        float g_stepTime = 0.0f;
        bool g_dirty = true;
        bool g_sentEmpty = false;
        unsigned g_withFog = 0;
        float g_player[3] = {};

        const sxc_fog_api* Api() {
            static const sxc_fog_api* s_api =
                wxl::Host() ? static_cast<const sxc_fog_api*>(wxl::Host()->get_service(SXC_FOG_SERVICE, SXC_FOG_SERVICE_VERSION)) : nullptr;
            return s_api;
        }

        int Mod(int v) {
            const int m = v % kSize;
            return m < 0 ? m + kSize : m;
        }

        int FloorCell(float v) { return static_cast<int>(std::floor(v / kCell)); }

        // The cell for world cell (i, j), reset when its slot held another place.
        Cell& At(int i, int j) {
            Cell& c = g_cells[Mod(j) * kSize + Mod(i)];

            if (c.i != i || c.j != j) {
                c = Cell{};
                c.i = i;
                c.j = j;
            }

            return c;
        }

        bool InGrid(int i, int j) {
            return i >= g_firstI && i < g_firstI + kSize && j >= g_firstJ && j < g_firstJ + kSize;
        }

        // The surface fog lies on: the terrain, or the water over it.
        void Ground(Cell& c) {
            if (c.groundKnown) {
                return;
            }

            const float x = (c.i + 0.5f) * kCell, y = (c.j + 0.5f) * kCell;
            float z = 0.0f, water = 0.0f;
            uint32_t liquid = 0;

            if (!terrain::HeightAt(x, y, z)) {
                c.ground = g_player[2] - 1.0f; // a hole or not loaded: the player's ground, asked again later
                return;
            }

            if (terrain::LiquidAt(x, y, water, liquid) && water > z) {
                z = water;
            }

            c.ground = z;
            c.groundKnown = true;
        }

        void Step(float dt) {
            const float keep = g_burnOff > 0.0f ? std::exp2(-dt / g_burnOff) : 1.0f;
            unsigned withFog = 0;

            for (int b = 0; b < kSize; b++) {
                for (int a = 0; a < kSize; a++) {
                    const int i = g_firstI + a, j = g_firstJ + b;
                    const int s = Mod(j) * kSize + Mod(i);
                    Cell& c = g_cells[s];
                    float* light = &g_light[s * 4];
                    float* pool = &g_pool[s * 4];
                    light[0] = light[1] = light[2] = light[3] = 0.0f;
                    pool[0] = pool[1] = pool[2] = pool[3] = 0.0f;

                    if (c.i != i || c.j != j || c.amount <= 0.0f) {
                        continue;
                    }

                    c.amount *= keep;

                    if (c.amount < 0.002f) {
                        c.amount = 0.0f;
                        continue;
                    }

                    Ground(c);
                    withFog++;

                    if (c.pool) {
                        pool[0] = kDensity * c.amount;
                        pool[1] = c.poolTop;
                        pool[2] = 0.6f * c.thickness * c.amount + 0.1f;
                    } else {
                        light[0] = kDensity * c.amount;
                        light[1] = c.ground;
                        light[2] = c.thickness * (0.4f + 0.6f * c.amount);
                        light[3] = 1.0f;
                    }
                }
            }

            g_withFog = withFog;
            g_dirty = g_dirty || withFog > 0;
        }

        void Send() {
            const sxc_fog_api* api = Api();

            if (!api) {
                return;
            }

            // The field: only while there's fog (and once more when it's gone).
            if (g_dirty && (g_withFog > 0 || !g_sentEmpty)) {
                sxc_fog_field field = {};
                field.struct_size = sizeof(field);
                field.size = kSize;
                field.cell_size = kCell;
                field.first_i = g_firstI;
                field.first_j = g_firstJ;
                field.light = g_light.data();
                field.pool = g_pool.data();
                api->set_field(g_withFog > 0 ? &field : nullptr);
                g_sentEmpty = g_withFog == 0;
            }

            g_dirty = false;

            // The look, every frame. The patches drift with the steady wind (not the gusts: they'd
            // shake the whole pattern); the client sums the drift up frame by frame.
            const float bearing = wind::BearingDegrees() * 3.14159265f / 180.0f;
            const float steady = wind::SteadyGround();
            sxc_fog_look look = {};
            look.struct_size = sizeof(look);
            look.enabled = g_look.enabled ? WXL_TRUE : WXL_FALSE;
            look.colour_mode = g_look.colourMode;
            look.colour[0] = g_look.colour[0];
            look.colour[1] = g_look.colour[1];
            look.colour[2] = g_look.colour[2];
            look.brightness = g_look.brightness;
            look.density_scale = g_look.densityScale;
            look.noise_amount = g_look.noiseAmount;
            look.noise_tile = g_look.noiseTile;
            const float speed = 3.0f * steady * g_look.drift; // yd per second
            look.drift[0] = std::cos(bearing) * speed;        // bearing clockwise from north (+x): east is -y
            look.drift[1] = -std::sin(bearing) * speed;
            look.far_distance = g_look.farDistance;
            look.haze_density = g_look.hazeDensity;
            look.haze_base = g_player[2] - 2.0f;
            look.haze_thickness = g_look.hazeThickness;
            look.debug = g_look.debug ? 1 : 0;
            api->set_look(&look);
        }
    }

    Look& Settings() { return g_look; }
    float& BurnOffSeconds() { return g_burnOff; }

    void Deposit(float amount, float radius, float thickness, bool pool) {
        if (!g_haveGrid || radius <= 0.0f) {
            return;
        }

        // A pool is flat: its top over the lowest ground under the deposit's middle.
        const int pi = FloorCell(g_player[0]), pj = FloorCell(g_player[1]);
        Cell& centre = At(pi, pj);
        Ground(centre);
        const float top = centre.ground + thickness;
        const int reach = static_cast<int>(std::ceil(radius / kCell));

        for (int dj = -reach; dj <= reach; dj++) {
            for (int di = -reach; di <= reach; di++) {
                const int i = pi + di, j = pj + dj;

                if (!InGrid(i, j)) {
                    continue;
                }

                const float d = std::sqrt(static_cast<float>(di * di + dj * dj)) * kCell / radius;

                if (d >= 1.0f) {
                    continue;
                }

                const float t = 1.0f - d;
                Cell& c = At(i, j);
                c.amount = std::min(1.0f, c.amount + amount * t * t * (3.0f - 2.0f * t));
                c.thickness = thickness;
                c.pool = pool;
                c.poolTop = top;
            }
        }

        g_dirty = true;
        Step(0.0f);
    }

    void Clear() {
        for (Cell& c : g_cells) {
            c.amount = 0.0f;
        }

        g_dirty = true;
        Step(0.0f);
    }

    void Update(float dt, const world::Snapshot& snap) {
        if (!snap.inWorld) {
            return;
        }

        g_player[0] = snap.playerPos[0];
        g_player[1] = snap.playerPos[1];
        g_player[2] = snap.playerPos[2];

        if (snap.mapId != g_map) {
            std::fill(g_cells.begin(), g_cells.end(), Cell{});
            g_map = snap.mapId;
            g_haveGrid = false;
            g_dirty = true;
        }

        const int fi = FloorCell(snap.playerPos[0]) - kSize / 2, fj = FloorCell(snap.playerPos[1]) - kSize / 2;

        if (!g_haveGrid || std::abs(fi - g_firstI) >= kRecentre || std::abs(fj - g_firstJ) >= kRecentre) {
            g_firstI = fi;
            g_firstJ = fj;
            g_haveGrid = true;
            g_dirty = true;
        }

        g_stepTime += dt;

        if (g_stepTime >= kStep) {
            Step(g_stepTime);
            g_stepTime = 0.0f;
        }

        Send();
    }

    void Stop() {
        if (const sxc_fog_api* api = Api()) {
            sxc_fog_look look = {};
            look.struct_size = sizeof(look);
            look.enabled = WXL_FALSE;
            api->set_look(&look);
            api->set_field(nullptr);
        }

        std::fill(g_cells.begin(), g_cells.end(), Cell{});
        g_haveGrid = false;
        g_withFog = 0;
        g_sentEmpty = true;
    }

    bool Available() {
        return Api() && Api()->available();
    }

    const char* Status() {
        return Api() ? Api()->status() : "the client has no sxc.fog (an older client)";
    }

    unsigned CellsWithFog() { return g_withFog; }
}
