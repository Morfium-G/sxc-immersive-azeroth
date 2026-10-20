#include "fog/Wake.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <unordered_map>
#include "SXC/Api/sxc_fog.h"
#include "wxl/sdk.hpp"

namespace ia::fog::wake {
    namespace {
        constexpr int N = kSize;
        constexpr int kMaxPoints = 24;       // trail points per unit (a point every kPointEvery s while moving)
        constexpr float kPointEvery = 0.15f; // s
        constexpr float kPointMinMove = 0.15f; // yd: closer than this to the last point, no new one
        constexpr float kForget = 5.0f;      // s: a unit not seen for this long loses its trail
        constexpr int kMaxUnits = 128;

        Settings g_settings;

        struct Point {
            float x, y, z, time;
        };

        struct Trail {
            Point points[kMaxPoints]; // newest first
            int count = 0;
            float lastSeen = 0.0f;
            float speed = 0.0f; // yd/s, smoothed
        };

        std::unordered_map<uint64_t, Trail> g_trails;
        world::Unit g_units[kMaxUnits];
        int g_unitCount = 0;
        float g_time = 0.0f;

        // The map, rebuilt each frame: push, rim, top, displacement; then packed for the client.
        float g_push[N * N], g_rim[N * N], g_top[N * N], g_disp[N * N * 2];
        float g_texels[N * N * 4];
        float g_originX = 0.0f, g_originY = 0.0f;
        float g_maxTop = -1.0e9f;
        bool g_sent = false;
        Stats g_stats;

        double NowMs() {
            using namespace std::chrono;
            return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
        }

        const sxc_fog_api* Api() {
            static const sxc_fog_api* s_api =
                wxl::Host() ? static_cast<const sxc_fog_api*>(wxl::Host()->get_service(SXC_FOG_SERVICE, SXC_FOG_SERVICE_VERSION)) : nullptr;
            return s_api && s_api->struct_size >= offsetof(sxc_fog_api, set_wake) + sizeof(s_api->set_wake) ? s_api : nullptr;
        }

        float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

        float Smooth(float a, float b, float v) {
            const float t = Clamp01((v - a) / (b - a));
            return t * t * (3.0f - 2.0f * t);
        }

        // One stretch of a wake: from a (newer, age ageA) to b (older), a soft capsule.
        void Capsule(const Point& a, float ageA, const Point& b, float ageB, float r0, float height, float amp) {
            const float life = g_settings.trailSeconds > 0.1f ? g_settings.trailSeconds : 0.1f;

            if (ageA >= life) {
                return;
            }

            const float rMax = r0 * (1.0f + g_settings.spread * Clamp01(ageB / life)) * 1.5f;
            // Texels touched (the map's border row stays empty, so the clamped edge reads nothing).
            const int x0 = std::max(1, static_cast<int>(std::floor((std::fmin(a.x, b.x) - rMax - g_originX) / kTexel)));
            const int x1 = std::min(N - 2, static_cast<int>(std::floor((std::fmax(a.x, b.x) + rMax - g_originX) / kTexel)));
            const int y0 = std::max(1, static_cast<int>(std::floor((std::fmin(a.y, b.y) - rMax - g_originY) / kTexel)));
            const int y1 = std::min(N - 2, static_cast<int>(std::floor((std::fmax(a.y, b.y) + rMax - g_originY) / kTexel)));

            if (x0 > x1 || y0 > y1) {
                return;
            }

            ++g_stats.segments;
            const float ex = b.x - a.x, ey = b.y - a.y;
            const float len2 = ex * ex + ey * ey;
            const float len = std::sqrt(len2);
            // The unit moved from b to a: the fog's pattern is dragged that way.
            const float mx = len > 1.0e-4f ? -ex / len : 0.0f, my = len > 1.0e-4f ? -ey / len : 0.0f;

            for (int ty = y0; ty <= y1; ++ty) {
                for (int tx = x0; tx <= x1; ++tx) {
                    const float px = g_originX + (tx + 0.5f) * kTexel, py = g_originY + (ty + 0.5f) * kTexel;
                    const float s = len2 > 1.0e-6f ? Clamp01(((px - a.x) * ex + (py - a.y) * ey) / len2) : 0.0f;
                    const float cx = a.x + ex * s, cy = a.y + ey * s;
                    const float dx = px - cx, dy = py - cy;
                    const float d = std::sqrt(dx * dx + dy * dy);
                    const float age = ageA + (ageB - ageA) * s;

                    if (age >= life) {
                        continue;
                    }

                    const float fade = 1.0f - Smooth(0.0f, life, age);
                    const float r = r0 * (1.0f + g_settings.spread * age / life);

                    if (d >= r * 1.5f) {
                        continue;
                    }

                    const int t = ty * N + tx;
                    const float push = amp * fade * (1.0f - Smooth(0.5f * r, r, d));
                    const float rim = amp * fade * g_settings.rim * Smooth(0.8f * r, 1.15f * r, d) * (1.0f - Smooth(1.15f * r, 1.5f * r, d));
                    g_push[t] = std::fmax(g_push[t], push);
                    g_rim[t] = std::fmax(g_rim[t], rim);
                    const float top = a.z + (b.z - a.z) * s + height;

                    if ((push > 0.01f || rim > 0.01f) && top > g_top[t]) {
                        g_top[t] = top;
                    }

                    g_maxTop = std::fmax(g_maxTop, top);

                    // Swirl: the pattern dragged along the path near it, and pushed out to the sides
                    // around the wake's edge (yd).
                    const float along = fade * (1.0f - Smooth(0.0f, 1.3f * r, d));
                    const float side = fade * Smooth(0.3f * r, r, d) * (1.0f - Smooth(r, 1.5f * r, d));
                    const float ox = d > 1.0e-4f ? dx / d : 0.0f, oy = d > 1.0e-4f ? dy / d : 0.0f;
                    const float k = 2.0f * g_settings.swirl * amp;
                    g_disp[t * 2] += k * (mx * along + ox * side);
                    g_disp[t * 2 + 1] += k * (my * along + oy * side);
                }
            }
        }

        void Rebuild(const world::Snapshot& snap) {
            std::fill(std::begin(g_push), std::end(g_push), 0.0f);
            std::fill(std::begin(g_rim), std::end(g_rim), 0.0f);
            std::fill(std::begin(g_top), std::end(g_top), -1.0e9f);
            std::fill(std::begin(g_disp), std::end(g_disp), 0.0f);
            g_maxTop = -1.0e9f;
            g_stats.units = g_stats.segments = 0;
            // Centred on the player, snapped to whole texels (no shimmer as the player moves).
            g_originX = (std::floor(snap.playerPos[0] / kTexel) - N / 2) * kTexel;
            g_originY = (std::floor(snap.playerPos[1] / kTexel) - N / 2) * kTexel;
            const float reach = (N / 2 - 2) * kTexel;

            for (int u = 0; u < g_unitCount; u++) {
                const world::Unit& act = g_units[u];

                if (std::fabs(act.pos[0] - snap.playerPos[0]) > reach || std::fabs(act.pos[1] - snap.playerPos[1]) > reach) {
                    continue;
                }

                auto found = g_trails.find(act.guid);

                if (found == g_trails.end()) {
                    continue;
                }

                const Trail& tr = found->second;
                ++g_stats.units;
                const float r0 = std::fmax(act.effectiveRadius * g_settings.radiusScale * (act.mounted ? 1.5f : 1.0f), 0.6f);
                float h = act.effectiveRadius * 5.0f;
                h = (h < 1.5f ? 1.5f : (h > 6.0f ? 6.0f : h)) * g_settings.heightScale + (act.mounted ? 1.2f : 0.0f);
                const float motion = Clamp01(tr.speed / 4.0f);
                const float headAmp = g_settings.standing + (g_settings.strength - g_settings.standing) * motion;

                const Point head = { act.pos[0], act.pos[1], act.pos[2], g_time };
                const Point* prev = &head;
                float prevAge = 0.0f;
                Capsule(head, 0.0f, head, 0.0f, r0, h, headAmp); // the body itself

                for (int i = 0; i < tr.count; ++i) {
                    const Point& p = tr.points[i];
                    const float age = g_time - p.time;
                    Capsule(*prev, prevAge, p, age, r0, h, g_settings.strength);
                    prev = &p;
                    prevAge = age;
                }
            }

            // Empty texels take the highest top (the filter blends a wake's top with its neighbours'; a
            // low value there would pull the height down at the wake's edge). Displacement capped at 3 yd.
            for (int t = 0; t < N * N; ++t) {
                float* texel = &g_texels[t * 4];
                float dx = g_disp[t * 2], dy = g_disp[t * 2 + 1];
                const float len = std::sqrt(dx * dx + dy * dy);

                if (len > 3.0f) {
                    dx *= 3.0f / len;
                    dy *= 3.0f / len;
                }

                texel[0] = g_push[t] - g_rim[t] * (1.0f - g_push[t]); // rim < 0: a little more fog
                texel[1] = g_top[t] < -1.0e8f ? g_maxTop : g_top[t];
                texel[2] = dx;
                texel[3] = dy;
            }
        }

        void Send(bool on) {
            const sxc_fog_api* api = Api();

            if (!api) {
                return;
            }

            if (!on) {
                if (g_sent) {
                    api->set_wake(nullptr);
                    g_sent = false;
                }

                return;
            }

            sxc_fog_wake w = {};
            w.struct_size = sizeof(w);
            w.size = N;
            w.texel = kTexel;
            w.origin[0] = g_originX;
            w.origin[1] = g_originY;
            w.texels = g_texels;
            api->set_wake(&w);
            g_sent = true;
        }
    }

    Settings& Config() { return g_settings; }

    void Update(float dt, const world::Snapshot& snap, bool fogNear) {
        if (!snap.inWorld || !g_settings.enabled) {
            g_trails.clear();
            g_stats.units = g_stats.segments = 0;
            Send(false);
            g_stats.sent = false;
            return;
        }

        const double t0 = NowMs();
        g_time += dt;
        g_unitCount = world::Units(snap.playerPos, (N / 2) * kTexel * 1.5f, g_units, kMaxUnits);

        // Trails: a new point every kPointEvery s while a unit moves; points older than the trail's
        // life drop off; units out of sight for a while are forgotten.
        const float life = g_settings.trailSeconds > 0.1f ? g_settings.trailSeconds : 0.1f;

        for (int u = 0; u < g_unitCount; u++) {
            const world::Unit& act = g_units[u];
            Trail& tr = g_trails[act.guid];
            const float seenGap = g_time - tr.lastSeen;
            tr.lastSeen = g_time;

            if (tr.count > 0) {
                const Point& last = tr.points[0];
                const float dx = act.pos[0] - last.x, dy = act.pos[1] - last.y;
                const float moved = std::sqrt(dx * dx + dy * dy);
                const float since = g_time - last.time;
                const float speedNow = since > 1.0e-3f ? moved / since : 0.0f;

                if (dt > 0.0f && seenGap < 1.0f) {
                    tr.speed += (speedNow - tr.speed) * (1.0f - std::exp(-dt / 0.25f));
                }

                if (since >= kPointEvery && moved >= kPointMinMove) {
                    std::memmove(&tr.points[1], &tr.points[0], sizeof(Point) * (kMaxPoints - 1));
                    tr.points[0] = { act.pos[0], act.pos[1], act.pos[2], g_time };

                    if (tr.count < kMaxPoints) {
                        ++tr.count;
                    }
                } else if (moved < kPointMinMove && since >= kPointEvery) {
                    tr.points[0].time = g_time - kPointEvery; // standing: the head point stays fresh, no trail grows
                }
            } else {
                tr.points[0] = { act.pos[0], act.pos[1], act.pos[2], g_time };
                tr.count = 1;
                tr.speed = 0.0f;
            }

            while (tr.count > 1 && g_time - tr.points[tr.count - 1].time > life) {
                --tr.count;
            }
        }

        for (auto it = g_trails.begin(); it != g_trails.end();) {
            it = g_time - it->second.lastSeen > kForget ? g_trails.erase(it) : std::next(it);
        }

        Rebuild(snap);
        Send(fogNear && g_stats.units > 0);
        g_stats.sent = g_sent;
        g_stats.ms = NowMs() - t0;
    }

    Stats GetStats() { return g_stats; }

    void Reset() {
        g_trails.clear();
        g_unitCount = 0;
        g_stats = Stats{};
        Send(false);
    }
}
