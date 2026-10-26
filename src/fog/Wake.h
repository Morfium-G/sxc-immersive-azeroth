// The fog wake (WXL: features/FogWake; orchestration docs/r&d/immersion/ground-fog-design.md,
// "Creatures and wind moving the fog"): units pushing the ground fog aside. The fog field's 2 yd cells
// are too coarse for a person-sized hole, so the wake is its own small map around the player, drawn on
// the CPU each frame and read by the client's fog march (sxc.fog set_wake).
//
// Every nearby unit (world::Units) and a trail of its recent positions is drawn as soft capsules. Per
// texel: how much fog is pushed out (fading with the trail's age, a slight pile-up at the rim), the
// height the push reaches (the unit's body, not the whole column), and a displacement that drags the
// fog's patches along the path and curls them to both sides (the swirl).
#pragma once

#include "World.h"

namespace ia::fog::wake {
    // The panel's (saved in SXC.ini by Saved.cpp); WXL's defaults.
    struct Settings {
        bool enabled = true;
        float strength = 0.9f;     // how much fog a moving unit pushes out at its body (0..1)
        float standing = 0.35f;    // ... while standing still (its body still displaces some)
        float trailSeconds = 3.0f; // how long the wake takes to fill back in
        float radiusScale = 2.25f; // wake radius = the unit's radius x this (min 0.6 yd); Seyri found 2..2.5 reads best
        float spread = 1.0f;       // the wake widens by this x its radius over its lifetime
        float heightScale = 1.0f;  // the wake's height = the unit's height x this
        float rim = 0.25f;         // fog piled up at the wake's rim (share of the push)
        float swirl = 1.0f;        // how far the fog's patches are dragged and curled (x ~2 yd)
    };
    Settings& Config();

    constexpr int kSize = 64;      // texels per side
    constexpr float kTexel = 1.0f; // yd per texel: the map covers +-32 yd around the player

    // Once per frame, after the fog field: trails, the map, and the map to the client (only while
    // there's fog to push: `fogNear`).
    void Update(float dt, const world::Snapshot& snap, bool fogNear);

    struct Stats {
        unsigned units = 0, segments = 0;
        double ms = 0.0;
        bool sent = false;
    };
    Stats GetStats();

    void Reset();
}
