#include "fog/Far.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <unordered_map>
#include <vector>
#include "World.h"
#include "env/Regional.h"
#include "fog/Field.h"

namespace ia::fog::farmap {
    namespace {
        constexpr int kGroundPerFrame = 96;   // chunks whose ground is sampled per frame (5 heights each)
        constexpr double kRetryMs = 250.0;    // a chunk whose terrain isn't loaded is asked again after this
        constexpr double kRebuildMs = 500.0;  // rebuilds at least twice a second (the zones' fog changes slowly) ...
        constexpr double kNewGroundMs = 60.0; // ... and this soon after new chunks got their ground (flying fast)
        constexpr float kNone = 1.0e-5f;
        constexpr int kFarKinds[] = { kMist, kAreaFog, kHaze }; // the zone-wide kinds

        struct Chunk {
            int i = 0x7FFFFFFF, j = 0;
            bool groundKnown = false;
            float ground = 0.0f;
            double triedAt = -1.0e9;
        };

        std::vector<Chunk> g_chunks;
        int g_size = 0;
        int g_firstI = 0x7FFFFFFF, g_firstJ = 0;
        double g_lastBuild = -1.0e9;
        bool g_newGround = false;
        unsigned g_zonesKnown = 0;
        // Offsets from the player's chunk, nearest first: the ground is asked for in this order, so the
        // terrain loading in ahead gets its far fog within a frame or two. (A sweep over the whole
        // window, mostly chunks not loaded, took up to a second to reach them: flying fast left a band
        // without fog between the near field and the far fog, which then popped in.)
        std::vector<std::pair<int, int>> g_order;
        std::vector<float> g_layer, g_colour;
        Stats g_stats;

        double NowMs() {
            using namespace std::chrono;
            return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
        }

        int Mod(int v) {
            const int m = v % g_size;
            return m < 0 ? m + g_size : m;
        }

        // A chunk's fog: its light layer's density per yd at the floor, thickness, colour.
        struct ZoneLook {
            float density = 0.0f, thickness = 1.0f, rgb[3] = {};
        };

        // Where a chunk overlaps the near field, its ready cells' real fog, averaged (all kinds, amount x
        // density as the far map counts them), replaces the zone's typical fog by the share of the chunk
        // they cover: at the handover the far map then shows what the near field shows (the zone's
        // typical fog there made a seam). Colours per kind and area cached per build.
        void BlendNear(int i, int j, float chunk, ColourFn colour, std::unordered_map<uint64_t, std::array<float, 3>>& colours, ZoneLook& look) {
            const Grid& g = Current();

            if (!g.amount[0] || !g.groundKnown || !g.area) {
                return;
            }

            constexpr int N = kGridSize;
            const int c0 = static_cast<int>(std::floor(i * chunk / kCellSize)), c1 = static_cast<int>(std::floor((i + 1) * chunk / kCellSize));
            const int r0 = static_cast<int>(std::floor(j * chunk / kCellSize)), r1 = static_cast<int>(std::floor((j + 1) * chunk / kCellSize));

            if (c1 <= g.firstI || c0 >= g.firstI + N || r1 <= g.firstJ || r0 >= g.firstJ + N) {
                return; // no overlap
            }

            auto wrap = [](int v) {
                const int m = v % N;
                return m < 0 ? m + N : m;
            };

            // Every 2nd cell each way: plenty for a 33 yd average, a quarter of the work.
            int total = 0, count = 0;
            float sumD = 0.0f, sumW = 0.0f, rgb[3] = {};

            for (int r = r0; r < r1; r += 2) {
                for (int c = c0; c < c1; c += 2) {
                    ++total;

                    if (c < g.firstI || c >= g.firstI + N || r < g.firstJ || r >= g.firstJ + N) {
                        continue;
                    }

                    const int index = wrap(r) * N + wrap(c);

                    if (!g.groundKnown[index] || (g.fresh && g.fresh[index])) {
                        continue;
                    }

                    ++count;

                    for (int k = 0; k < kKindCount; ++k) {
                        if (!Settings(k).enabled) {
                            continue;
                        }

                        const KindSettings s = SettingsAt(k, index);
                        const float d = g.amount[k][index] * s.density;

                        if (d <= 0.0f) {
                            continue;
                        }

                        sumD += d;
                        sumW += d * s.thickness;
                        const uint64_t key = (static_cast<uint64_t>(g.area[index]) << 8) | static_cast<uint64_t>(k);
                        auto found = colours.find(key);

                        if (found == colours.end()) {
                            std::array<float, 3> cl{};
                            colour(k, g.area[index], cl.data());
                            found = colours.emplace(key, cl).first;
                        }

                        for (int ch = 0; ch < 3; ++ch) {
                            rgb[ch] += d * found->second[ch];
                        }
                    }
                }
            }

            if (!count || !total) {
                return;
            }

            const float cover = static_cast<float>(count) / total;
            const float nearD = sumD / count;
            const float dz = look.density * (1.0f - cover), dn = nearD * cover;
            const float d = dz + dn;

            if (d <= 0.0f) {
                look.density = 0.0f;
                return;
            }

            const float nearT = sumD > 0.0f ? sumW / sumD : look.thickness;
            look.thickness = (dz * look.thickness + dn * nearT) / d;

            for (int ch = 0; ch < 3; ++ch) {
                const float nearC = sumD > 0.0f ? rgb[ch] / sumD : look.rgb[ch];
                look.rgb[ch] = (dz * look.rgb[ch] + dn * nearC) / d;
            }

            look.density = d;
        }

        // A chunk's lowest ground: its centre and four points halfway to its corners (the layer lies on
        // the low parts, so it doesn't float over dips). Fog lies on water, as near.
        bool LowestGround(int i, int j, float cell, float& out) {
            static const float kAt[5][2] = { { 0.5f, 0.5f }, { 0.25f, 0.25f }, { 0.75f, 0.25f }, { 0.25f, 0.75f }, { 0.75f, 0.75f } };
            bool any = false;

            for (const auto& p : kAt) {
                const float x = (i + p[0]) * cell, y = (j + p[1]) * cell;
                float z, liquid = 0.0f;
                uint32_t type = 0;

                if (!terrain::HeightAt(x, y, z)) {
                    continue;
                }

                if (terrain::LiquidAt(x, y, liquid, type) && liquid > z) {
                    z = liquid;
                }

                out = any ? std::fmin(out, z) : z;
                any = true;
            }

            return any;
        }
    }

    bool Update(ColourFn colour, const float player[2], Map& out, bool& any) {
        any = false;
        const regional::ZoneMap& map = regional::Map();

        if (!map.zone || map.size <= 0) {
            return false;
        }

        if (g_size != map.size) {
            g_size = map.size;
            g_chunks.assign(static_cast<size_t>(g_size) * g_size, Chunk{});
            g_layer.assign(static_cast<size_t>(g_size) * g_size * 4, 0.0f);
            g_colour.assign(static_cast<size_t>(g_size) * g_size * 4, 0.0f);
            g_order.clear();

            for (int dj = -g_size / 2; dj < g_size / 2; ++dj) {
                for (int di = -g_size / 2; di < g_size / 2; ++di) {
                    g_order.emplace_back(di, dj);
                }
            }

            std::stable_sort(g_order.begin(), g_order.end(), [](const std::pair<int, int>& a, const std::pair<int, int>& b) {
                return a.first * a.first + a.second * a.second < b.first * b.first + b.second * b.second;
            });
            g_lastBuild = -1.0e9;
        }

        // Ground, nearest first: chunks that now hold another world chunk, or whose terrain wasn't
        // loaded when last asked.
        const double now = NowMs();
        const int pi = static_cast<int>(std::floor(player[0] / map.cellSize)), pj = static_cast<int>(std::floor(player[1] / map.cellSize));
        int sampled = 0;

        for (const auto& o : g_order) {
            if (sampled >= kGroundPerFrame) {
                break;
            }

            const int i = pi + o.first, j = pj + o.second;

            if (i < map.firstI || i >= map.firstI + g_size || j < map.firstJ || j >= map.firstJ + g_size) {
                continue;
            }

            Chunk& c = g_chunks[Mod(j) * g_size + Mod(i)];

            if (c.i == i && c.j == j && (c.groundKnown || now - c.triedAt < kRetryMs)) {
                continue;
            }

            c.i = i;
            c.j = j;
            c.triedAt = now;
            c.groundKnown = LowestGround(i, j, map.cellSize, c.ground);
            g_newGround = g_newGround || c.groundKnown;
            ++sampled;
        }

        // New zones on the zone map count like new ground.
        if (map.known != g_zonesKnown) {
            g_newGround = g_newGround || map.known > g_zonesKnown;
            g_zonesKnown = map.known;
        }

        const bool moved = map.firstI != g_firstI || map.firstJ != g_firstJ;
        const double since = now - g_lastBuild;

        if (!moved && since < kRebuildMs && !(g_newGround && since >= kNewGroundMs)) {
            return false;
        }

        g_newGround = false;

        g_firstI = map.firstI;
        g_firstJ = map.firstJ;
        g_lastBuild = now;
        const int N = g_size;

        // Per zone (a handful around): its fog's light-layer density, thickness and colour.
        std::unordered_map<uint32_t, ZoneLook> zones;
        std::unordered_map<uint64_t, std::array<float, 3>> colours;

        auto lookOf = [&](uint32_t zone) -> const ZoneLook& {
            auto found = zones.find(zone);

            if (found != zones.end()) {
                return found->second;
            }

            ZoneLook l;
            float weighted = 0.0f, mix[3] = {};

            for (int k : kFarKinds) {
                const KindSettings s = ZoneSettings(k, zone);

                if (!s.enabled) {
                    continue;
                }

                const float d = ZoneAmount(zone, k) * s.density;

                if (d <= 0.0f) {
                    continue;
                }

                float c[3];
                colour(k, zone, c);
                l.density += d;
                weighted += d * s.thickness;

                for (int ch = 0; ch < 3; ++ch) {
                    mix[ch] += d * c[ch];
                }
            }

            if (l.density > 0.0f) {
                l.thickness = weighted / l.density;

                for (int ch = 0; ch < 3; ++ch) {
                    l.rgb[ch] = mix[ch] / l.density;
                }
            }

            return zones.emplace(zone, l).first->second;
        };

        static std::vector<ZoneLook> looks;
        looks.assign(static_cast<size_t>(N) * N, ZoneLook{});
        const auto start = NowMs();
        g_stats.ground = g_stats.foggy = 0;

        for (int r = 0; r < N; ++r) {
            for (int c = 0; c < N; ++c) {
                const int i = g_firstI + c, j = g_firstJ + r; // world chunk
                const int slot = Mod(j) * N + Mod(i);
                const Chunk& ch = g_chunks[slot];

                if (ch.i != i || ch.j != j || !ch.groundKnown) {
                    continue;
                }

                ++g_stats.ground;
                const uint32_t zone = map.zone[slot];
                ZoneLook l = zone ? lookOf(zone) : ZoneLook{};
                BlendNear(i, j, map.cellSize, colour, colours, l);

                if (l.density > kNone) {
                    looks[slot] = l;
                    ++g_stats.foggy;
                    any = true;
                }
            }
        }

        for (int r = 0; r < N; ++r) {
            for (int c = 0; c < N; ++c) {
                const int slot = r * N + c; // the zone map's own layout
                const ZoneLook& l = looks[slot];
                // The layer lies on the lowest ground among the chunk and its 8 neighbours (~100 yd),
                // like the near field's fog floor: level in valleys, cliffs rising out of it. On each
                // chunk's own lowest ground the floor jumped tens of yards between chunks on cliffs.
                float low = 1.0e9f;
                const ZoneLook* colourFrom = &l;

                for (int dr = -1; dr <= 1; ++dr) {
                    for (int dc = -1; dc <= 1; ++dc) {
                        const int n = Mod(r + dr) * N + Mod(c + dc);

                        if (g_chunks[n].groundKnown) {
                            low = std::fmin(low, g_chunks[n].ground);
                        }

                        // Chunks without fog take a foggy neighbour's colour and thickness (the filter
                        // fades the density into them).
                        if (colourFrom->density <= kNone && looks[n].density > kNone) {
                            colourFrom = &looks[n];
                        }
                    }
                }

                float* layer = &g_layer[slot * 4];
                float* rgb = &g_colour[slot * 4];
                layer[0] = l.density;
                layer[1] = low < 1.0e8f ? low : g_chunks[slot].ground;
                layer[2] = colourFrom->thickness;
                layer[3] = 0.0f;

                for (int ch = 0; ch < 3; ++ch) {
                    rgb[ch] = colourFrom->rgb[ch];
                }

                rgb[3] = 0.0f;
            }
        }

        g_stats.builds++;
        g_stats.buildMs = NowMs() - start;
        out.size = N;
        out.cellSize = map.cellSize;
        out.firstI = g_firstI;
        out.firstJ = g_firstJ;
        out.layer = g_layer.data();
        out.colour = g_colour.data();
        return true;
    }

    Stats GetStats() { return g_stats; }

    void Reset() {
        g_chunks.clear();
        g_size = 0;
        g_firstI = 0x7FFFFFFF;
        g_lastBuild = -1.0e9;
        g_newGround = false;
        g_zonesKnown = 0;
        g_stats = Stats{};
    }
}
