#include "env/Shelter.h"

#include <cmath>
#include <cstdint>
#include <iterator>
#include <unordered_map>
#include "World.h"

namespace ia::shelter {
    namespace {
        constexpr float kCell = 4.0f;          // yards, horizontal and vertical
        constexpr float kRoofSearch = 40.0f;   // how far up a roof still counts
        constexpr float kRecheckAfter = 10.0f; // seconds; WMOs stream in after the terrain
        constexpr float kEvictRange = 120.0f;  // yards from the player
        constexpr unsigned kRayBudget = 64;    // traces per frame

        // The lee ray starts this high above the point and runs `reach` yards upwind, rising at
        // `angleDeg`, so ordinary slopes pass while walls, cliffs and steep rock block it.
        constexpr float kLeeHeight = 1.5f;
        constexpr int kSectors = 8; // 45 degree wind sectors cached separately
        constexpr float kTwoPi = 6.2831853f;

        // The roof cache stores 0/1; the lee cache the hit fraction along the ray (1 = no hit), so the
        // strength can change without tracing again.
        struct Entry {
            float open;
            float tracedAt;
            float x, y;
        };

        std::unordered_map<uint64_t, Entry> g_roof;
        std::unordered_map<uint64_t, Entry> g_lee[kSectors];
        LeeParams g_leeParams;
        bool g_enabled = true;
        bool g_leeEnabled = true;
        float g_time = 0.0f;
        unsigned g_rays = 0, g_deferred = 0;

        int32_t Cell(float v) { return static_cast<int32_t>(std::floor(v / kCell)); }

        uint64_t Key(int32_t cx, int32_t cy, int32_t cz) {
            // 21 bits per axis, offset to unsigned: plenty for +-4M yards at 4 yd cells.
            const uint64_t ux = static_cast<uint64_t>(cx + (1 << 20)) & 0x1FFFFF;
            const uint64_t uy = static_cast<uint64_t>(cy + (1 << 20)) & 0x1FFFFF;
            const uint64_t uz = static_cast<uint64_t>(cz + (1 << 20)) & 0x1FFFFF;
            return (ux << 42) | (uy << 21) | uz;
        }

        float LeeFactor(float hitFraction) {
            const float t = hitFraction < 0.0f ? 0.0f : (hitFraction > 1.0f ? 1.0f : hitFraction);
            return 1.0f - g_leeParams.strength * (1.0f - t);
        }

        // The ray's start height for a cell: the queried height, but never below the terrain at the
        // cell's centre. On a steep slope the terrain there can be higher than the spot that asked; a
        // ray from underground would "hit a roof" (the terrain from below), and exposed cliffs would
        // read as sheltered. The terrain can't be a roof.
        float StartHeight(float x, float y, float z) {
            float ground = z;
            return terrain::HeightAt(x, y, ground) && ground > z ? ground : z;
        }

        // A cached answer, or null when it must be traced (missing or stale).
        const Entry* Fresh(const std::unordered_map<uint64_t, Entry>& map, uint64_t key, const Entry*& old) {
            auto it = map.find(key);
            old = it != map.end() ? &it->second : nullptr;
            return old && g_time - old->tracedAt <= kRecheckAfter ? old : nullptr;
        }
    }

    void BeginFrame(const float center[3], float dt) {
        g_time += dt > 0.0f && dt < 1.0f ? dt : 0.0f;
        g_rays = g_deferred = 0;
        const float r2 = kEvictRange * kEvictRange;

        auto evict = [&](std::unordered_map<uint64_t, Entry>& map) {
            for (auto it = map.begin(); it != map.end();) {
                const float dx = it->second.x - center[0], dy = it->second.y - center[1];
                it = dx * dx + dy * dy > r2 ? map.erase(it) : std::next(it);
            }
        };

        evict(g_roof);

        for (auto& map : g_lee) {
            evict(map);
        }
    }

    float Openness(const float pos[3]) {
        if (!g_enabled) {
            return 1.0f;
        }

        const int32_t cx = Cell(pos[0]), cy = Cell(pos[1]), cz = Cell(pos[2]);
        const uint64_t key = Key(cx, cy, cz);
        const Entry* old = nullptr;

        if (const Entry* e = Fresh(g_roof, key, old)) {
            return e->open;
        }

        if (g_rays >= kRayBudget) {
            g_deferred++;
            return old ? old->open : 1.0f; // keep the old answer, or assume open
        }

        g_rays++;

        // From the cell's centre column, so every query in the cell shares one answer; half a yard
        // up, so a query at floor level doesn't hit the floor it stands on.
        const float x = (cx + 0.5f) * kCell, y = (cy + 0.5f) * kCell;
        const float z = StartHeight(x, y, pos[2]);
        const float from[3] = { x, y, z + 0.5f };
        const float to[3] = { x, y, z + kRoofSearch };
        float fraction;
        const float open = world::Trace(from, to, fraction) ? 0.0f : 1.0f;
        g_roof[key] = Entry{ open, g_time, x, y };
        return open;
    }

    float Lee(const float pos[3], float windDirX, float windDirY) {
        if (!g_enabled || !g_leeEnabled) {
            return 1.0f;
        }

        // The sector of the direction the wind comes FROM, so a slowly drifting wind reuses its rays.
        float angle = std::atan2(-windDirY, -windDirX); // -pi..pi

        if (angle < 0.0f) {
            angle += kTwoPi;
        }

        const int sector = static_cast<int>(angle / (kTwoPi / kSectors) + 0.5f) % kSectors;
        const int32_t cx = Cell(pos[0]), cy = Cell(pos[1]), cz = Cell(pos[2]);
        const uint64_t key = Key(cx, cy, cz);
        auto& map = g_lee[sector];
        const Entry* old = nullptr;

        if (const Entry* e = Fresh(map, key, old)) {
            return LeeFactor(e->open);
        }

        if (g_rays >= kRayBudget) {
            g_deferred++;
            return old ? LeeFactor(old->open) : 1.0f;
        }

        g_rays++;

        // Along the sector's centre direction, so every query in the cell and sector agrees. A hit
        // at fraction t: right behind the obstacle (t ~ 0) loses most of the wind, an obstacle at the
        // far end of the reach barely matters.
        const float sectorAngle = sector * (kTwoPi / kSectors);
        const float ux = std::cos(sectorAngle), uy = std::sin(sectorAngle);
        const float reach = g_leeParams.reach;
        const float rise = reach * std::tan(g_leeParams.angleDeg * 3.14159265f / 180.0f);
        const float x = (cx + 0.5f) * kCell, y = (cy + 0.5f) * kCell;
        const float base = StartHeight(x, y, pos[2]);
        const float from[3] = { x, y, base + kLeeHeight };
        const float to[3] = { x + ux * reach, y + uy * reach, base + kLeeHeight + rise };
        float fraction = 1.0f;

        if (!world::Trace(from, to, fraction)) {
            fraction = 1.0f;
        }

        map[key] = Entry{ fraction, g_time, x, y };
        return LeeFactor(fraction);
    }

    LeeParams GetLeeParams() { return g_leeParams; }

    void SetLeeParams(const LeeParams& params) {
        const bool shapeChanged = params.reach != g_leeParams.reach || params.angleDeg != g_leeParams.angleDeg;
        g_leeParams = params;

        if (shapeChanged) {
            for (auto& map : g_lee) {
                map.clear();
            }
        }
    }

    void SetEnabled(bool enabled) { g_enabled = enabled; }
    bool Enabled() { return g_enabled; }
    void SetLeeEnabled(bool enabled) { g_leeEnabled = enabled; }
    bool LeeEnabled() { return g_leeEnabled; }

    void Clear() {
        g_roof.clear();

        for (auto& map : g_lee) {
            map.clear();
        }
    }

    Stats GetStats() {
        Stats s;
        s.entries = static_cast<unsigned>(g_roof.size());

        for (const auto& map : g_lee) {
            s.entries += static_cast<unsigned>(map.size());
        }

        s.raysThisFrame = g_rays;
        s.deferredThisFrame = g_deferred;
        return s;
    }
}
