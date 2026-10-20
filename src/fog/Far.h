// The far map (WXL: features/FogFar; orchestration docs/r&d/immersion/ground-fog-design.md, "Phase 3
// step 5", part 4): the ground fog beyond the near field (fog/Field, +-128 yd), so it doesn't end in a
// wall. One cell per terrain chunk (33 yd), 64 x 64 chunks (+-1 km) laid out like the regional zone
// map (regional::Map): per chunk its zone's typical fog (fog/Field's zone memory: mist, area fog, haze)
// with that zone's own density, thickness and colour, on the lowest ground among the chunk and its
// neighbours. Where a chunk overlaps the near field, the near field's real fog stands in by the share
// it covers (no seam at the handover). No near detail: hollows, pools, water fog, steam and units stay
// in the near field.
#pragma once

#include <cstdint>

namespace ia::fog::farmap {
    // A kind's colour at a place (fog/Fog's colours over the stock fog colour).
    using ColourFn = void (*)(int kind, uint32_t area, float out[3]);

    // The map as sxc.fog takes it (sxc_fog_far).
    struct Map {
        int size = 0;
        float cellSize = 0.0f;
        int firstI = 0, firstJ = 0;
        const float* layer = nullptr;  // per cell: density per yd, floor (world z), thickness (yd), 0
        const float* colour = nullptr; // per cell: rgb, 0
    };

    // Each frame: samples some chunks' ground, nearest the player first; rebuilds twice a second, at
    // once when the window moved, and soon after new chunks got their ground. True when `out` holds a
    // new map to send; `any` says whether any chunk has fog.
    bool Update(ColourFn colour, const float player[2], Map& out, bool& any);

    // Chunks with known ground, and with fog, at the last build (the panel).
    struct Stats {
        unsigned ground = 0, foggy = 0, builds = 0;
        double buildMs = 0.0;
    };
    Stats GetStats();

    void Reset();
}
