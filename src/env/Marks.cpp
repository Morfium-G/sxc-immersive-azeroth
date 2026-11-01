#include "env/Marks.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <vector>
#include "SXC/Api/sxc_cover.h"
#include "env/Fields.h"
#include "grass/Grass.h"
#include "wxl/sdk.hpp"

namespace ia::marks {
    namespace {
        constexpr int kSize = 128;          // cells per side (the world fields' grid)
        constexpr float kCell = 2.0f;       // yd
        constexpr int kRecentre = 8;        // cells the player moves before the grid follows
        constexpr float kTick = 0.25f;      // seconds between steps
        constexpr float kHeatGone = 15.0f;  // degC under BurnAt: the heat counts as gone

        Settings g_settings;
        std::vector<float> g_cells(static_cast<size_t>(kSize) * kSize * SXC_MARK_CHANNELS, 0.0f);
        int g_firstI = std::numeric_limits<int>::min(), g_firstJ = 0;
        float g_clock = 0.0f;
        bool g_dirty = false, g_sentAny = false;
        Stats g_stats;

        const sxc_cover_api* CoverApi() {
            static const sxc_cover_api* s_api =
                wxl::Host() ? static_cast<const sxc_cover_api*>(wxl::Host()->get_service(SXC_COVER_SERVICE, SXC_COVER_SERVICE_VERSION)) : nullptr;
            return s_api && s_api->struct_size >= offsetof(sxc_cover_api, set_marks) + sizeof(s_api->set_marks) ? s_api : nullptr;
        }

        const sxc_grass_api* GrassApi() {
            const sxc_grass_api* api = grass::Api();
            return api && api->struct_size >= offsetof(sxc_grass_api, set_marks) + sizeof(api->set_marks) ? api : nullptr;
        }

        int Mod(int a) { return ((a % kSize) + kSize) % kSize; }
        int FloorCell(float v) { return static_cast<int>(std::floor(v / kCell)); }
        float* Slot(int i, int j) { return &g_cells[(static_cast<size_t>(Mod(j)) * kSize + Mod(i)) * SXC_MARK_CHANNELS]; }

        bool Inside(int i, int j) { return i >= g_firstI && i < g_firstI + kSize && j >= g_firstJ && j < g_firstJ + kSize; }

        // The window around the player; the cells that leave it forget their marks (their slots take the
        // new cells).
        void Follow(const float pos[3]) {
            const int ci = FloorCell(pos[0]) - kSize / 2, cj = FloorCell(pos[1]) - kSize / 2;

            if (g_firstI != std::numeric_limits<int>::min() && std::abs(ci - g_firstI) < kRecentre && std::abs(cj - g_firstJ) < kRecentre) {
                return;
            }

            const bool had = g_firstI != std::numeric_limits<int>::min();
            const int oldI = g_firstI, oldJ = g_firstJ;
            g_firstI = ci;
            g_firstJ = cj;

            if (!had) {
                std::fill(g_cells.begin(), g_cells.end(), 0.0f);
                g_dirty = true;
                return;
            }

            for (int i = oldI; i < oldI + kSize; i++) {
                for (int j = oldJ; j < oldJ + kSize; j++) {
                    if (!Inside(i, j)) {
                        float* c = Slot(i, j);
                        std::fill(c, c + SXC_MARK_CHANNELS, 0.0f);
                    }
                }
            }

            g_dirty = true;
        }

        float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

        // One step: each cell's marks grow under their source and fade without it.
        void Step(float step, float z) {
            const Settings& s = g_settings;
            unsigned burnt = 0, frozen = 0;
            float mostBurnt = 0.0f, mostFrozen = 0.0f;
            const float burnAt = std::fmax(s.burnAt, 1.0f);

            for (int j = g_firstJ; j < g_firstJ + kSize; j++) {
                for (int i = g_firstI; i < g_firstI + kSize; i++) {
                    float* c = Slot(i, j);
                    const float pos[3] = { (i + 0.5f) * kCell, (j + 0.5f) * kCell, z };
                    float ground = 0.0f, air = 0.0f;

                    if (!fields::GroundTemperature(pos, ground, air)) {
                        continue;   // not known here: the marks stay as they are
                    }

                    const float burn0 = c[SXC_MARK_BURN], frost0 = c[SXC_MARK_FROST], soot0 = c[SXC_MARK_SOOT];
                    float burn = burn0, frost = frost0, soot = soot0;

                    // Burn: 0..0.5 blackening, 0.5..1 burning away; below 0 growing back, fresh (-1: from
                    // nothing). Once the heat is gone (kHeatGone under the threshold: a flickering source
                    // doesn't flip it back and forth) its black moves to the soot channel, which fades over
                    // SootSeconds, and what burnt away grows back green.
                    if (ground >= s.burnAt) {
                        if (burn < 0.0f) {
                            burn = 0.5f - 0.5f * burn;   // a plant growing back catches again, as burnt as it is small
                        }

                        burn += step / std::fmax(s.burnSeconds, 0.1f) * (1.0f + (ground - s.burnAt) / burnAt);
                        soot = 0.0f;           // the burn itself shows the black while it's hot
                        frost -= step / 2.0f;  // heat thaws at once
                    } else if (ground < s.burnAt - kHeatGone) {
                        if (burn > 0.0f) {
                            soot = std::fmax(soot, std::fmin(burn, 0.5f));   // soot at 0.5 is fully black
                            burn = burn > 0.5f ? 1.0f - 2.0f * burn : 0.0f;
                        } else if (burn < 0.0f) {
                            burn = std::fmin(burn + step / std::fmax(s.regrowSeconds, 1.0f), 0.0f);
                        }

                        soot = std::fmax(soot - step * 0.5f / std::fmax(s.sootSeconds, 0.1f), 0.0f);
                    }

                    if (ground <= s.freezeAt && ground <= air - s.freezeBelowAir) {
                        frost += step / std::fmax(s.freezeSeconds, 0.1f);
                    } else if (frost > 0.0f) {
                        frost -= step / std::fmax(s.thawSeconds, 1.0f) * (1.0f + std::fmax(ground - s.freezeAt, 0.0f) / 10.0f);
                    }

                    burn = std::fmax(-1.0f, std::fmin(1.0f, burn));
                    frost = Clamp01(frost);

                    if (burn != burn0 || frost != frost0 || soot != soot0) {
                        c[SXC_MARK_BURN] = burn;
                        c[SXC_MARK_FROST] = frost;
                        c[SXC_MARK_SOOT] = soot;
                        g_dirty = true;
                    }

                    burnt += burn != 0.0f;
                    frozen += frost > 0.0f;
                    mostBurnt = std::fmax(mostBurnt, std::fabs(burn));
                    mostFrozen = std::fmax(mostFrozen, frost);
                }
            }

            g_stats.burnt = burnt;
            g_stats.frozen = frozen;
            g_stats.mostBurnt = mostBurnt;
            g_stats.mostFrozen = mostFrozen;
        }

        void SendLooks(const sxc_grass_api* api) {
            const Settings& s = g_settings;
            sxc_grass_marks m = {};
            m.struct_size = sizeof(m);
            m.on = s.enabled ? WXL_TRUE : WXL_FALSE;
            sxc_grass_mark_look& burn = m.channel[SXC_MARK_BURN];
            burn.tint[0] = 0.05f;
            burn.tint[1] = 0.04f;
            burn.tint[2] = 0.03f;
            burn.tint_strength = Clamp01(s.burnTint);
            burn.size = Clamp01(s.burnSize);
            burn.wind = 1.0f;
            sxc_grass_mark_look& frost = m.channel[SXC_MARK_FROST];
            // Above 1: the plant's colour multiplies its texture, so pale blue has to brighten it.
            frost.tint[0] = 1.25f;
            frost.tint[1] = 1.5f;
            frost.tint[2] = 1.9f;
            frost.tint_strength = Clamp01(s.frostTint);
            frost.size = 1.0f;
            frost.wind = Clamp01(s.frostWind);

            // Soot: the burn's black after the heat, fading (the plant at whatever size it has).
            sxc_grass_mark_look& soot = m.channel[SXC_MARK_SOOT];
            soot = burn;
            soot.size = 1.0f;

            for (sxc_grass_mark_look* l : { &m.channel[SXC_MARK_LIFE], &m.blight }) {
                l->size = 1.0f;
                l->wind = 1.0f;
            }

            api->set_marks(&m);
        }

        void Send() {
            const sxc_cover_api* cover = CoverApi();
            const sxc_grass_api* grassApi = GrassApi();
            g_stats.sent = cover && grassApi;

            if (grassApi) {
                SendLooks(grassApi);
            }

            if (!cover || !g_dirty) {
                return;
            }

            sxc_cover_map map = {};
            map.struct_size = sizeof(map);
            map.size = kSize;
            map.cell_size = kCell;
            map.first_i = g_firstI;
            map.first_j = g_firstJ;
            map.cells = g_cells.data();
            cover->set_marks(&map);
            g_dirty = false;
            g_sentAny = true;
        }
    }

    Settings& Config() { return g_settings; }

    void Update(float dt, const world::Snapshot& snap) {
        if (!g_settings.enabled) {
            Stop();
            return;
        }

        Follow(snap.playerPos);
        g_clock += dt;

        if (g_clock >= kTick) {
            const auto start = std::chrono::steady_clock::now();
            Step(std::fmin(g_clock, 1.0f), snap.playerPos[2]);
            g_clock = 0.0f;
            g_stats.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        }

        Send();
    }

    void Stop() {
        g_firstI = std::numeric_limits<int>::min();
        std::fill(g_cells.begin(), g_cells.end(), 0.0f);
        g_stats = Stats{};

        if (g_sentAny) {
            if (const sxc_cover_api* cover = CoverApi()) {
                cover->set_marks(nullptr);
            }

            g_sentAny = false;
        }
    }

    Stats GetStats() { return g_stats; }

    float At(const float pos[3], int channel) {
        const int i = FloorCell(pos[0]), j = FloorCell(pos[1]);

        if (channel < 0 || channel >= SXC_MARK_CHANNELS || g_firstI == std::numeric_limits<int>::min() || !Inside(i, j)) {
            return 0.0f;
        }

        return Slot(i, j)[channel];
    }

    void Deposit(const float pos[3], int channel, float value, float radius) {
        if (channel < 0 || channel >= SXC_MARK_CHANNELS || g_firstI == std::numeric_limits<int>::min()) {
            return;
        }

        const int reach = static_cast<int>(std::ceil(radius / kCell));
        const int ci = FloorCell(pos[0]), cj = FloorCell(pos[1]);

        for (int j = cj - reach; j <= cj + reach; j++) {
            for (int i = ci - reach; i <= ci + reach; i++) {
                const float dx = (i + 0.5f) * kCell - pos[0], dy = (j + 0.5f) * kCell - pos[1];
                const float d = std::sqrt(dx * dx + dy * dy);

                if (d > radius || !Inside(i, j)) {
                    continue;
                }

                float& v = Slot(i, j)[channel];
                v = channel == SXC_MARK_LIFE ? std::fmax(-1.0f, std::fmin(1.0f, v + value)) : Clamp01(v + value * (1.0f - 0.5f * d / radius));
            }
        }

        g_dirty = true;
    }

    void Clear() {
        std::fill(g_cells.begin(), g_cells.end(), 0.0f);
        g_dirty = true;
    }
}
