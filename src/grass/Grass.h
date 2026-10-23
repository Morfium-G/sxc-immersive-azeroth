// Grass in the wind and parting around characters (port plan step 7b; WXL's features/GrassMotion and
// GrassDoodads). The client draws the grass instanced and bends each blade in its vertex shader
// (sxc.grass); this side hands it, every frame, an 8 x 8 grid of wind samples around the player (the
// full wind model: weather, gusts, shelter, lee; at each cell's ground, eased so changes blend), the
// characters near the player and the motion's settings, and sends GroundEffectDoodadWind (the per-model
// overrides) when it changes.
#pragma once

#include "SXC/Api/sxc_grass.h"
#include "World.h"

namespace ia::grass {
    // The settings (the Grass panel; not saved). WXL's defaults.
    struct Settings {
        bool motion = true;
        float amplitude = 0.35f;     // yd the tips move at full wind
        float flutter = 0.08f;       // yd of extra per-blade shimmer
        float flutterSpeed = 3.0f;   // radians a second
        float anchor = 0.15f;        // the bottom share of a blade that never moves
        float pushStrength = 0.5f;   // yd the blades lean away from a character
        float radiusScale = 2.0f;    // push radius = a character's radius x this
        float minRadius = 0.5f;      // yd: the smallest push radius
        float mountedScale = 2.0f;   // the push radius of a mounted character x this
    };
    Settings& Config();

    constexpr float kActorRange = 40.0f;   // yd around the player: the characters that part the grass

    // Once per frame (after the wind).
    void Update(float dt, const world::Snapshot& snap);

    // The mod is off or the world was left: the grass stands still.
    void Stop();

    bool Available();           // the client draws the grass instanced (and so moves it)
    const char* Status();       // the client's line
    const char* TableStatus();  // GroundEffectDoodadWind as last sent
    unsigned ActorsFed();       // characters handed over last frame
    const sxc_grass_api* Api(); // null on an older client
}
