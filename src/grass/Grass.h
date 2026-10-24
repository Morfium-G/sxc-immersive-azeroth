// Grass in the wind and parting around characters (port plan step 7b; WXL's features/GrassMotion and
// GrassDoodads). The client draws the grass instanced and bends each blade in its vertex shader
// (sxc.grass); this side hands it, every frame, an 8 x 8 grid of wind samples around the player (the
// full wind model: weather, gusts, shelter, lee; at each cell's ground, eased so changes blend), the
// characters near the player and the motion's settings, and sends GroundEffectDoodadWind (the per-model
// overrides) when it changes. Density (step 7d; WXL's features/GrassDensity): this side answers the
// client's GroundEffectDoodadDensity questions (per plant model and place) and hands it the player's limits.
#pragma once

#include "SXC/Api/sxc_grass.h"
#include "World.h"

namespace ia::grass {
    // The settings (the Grass panel), saved in [ImmersiveAzerothGrass] (WTF\SXC.ini; the overlay's Settings
    // view shows them too). WXL's defaults.
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

        // Density (step 7d): the player's limits on GroundEffectDoodadDensity (WXL's defaults).
        bool density = true;
        float maxMultiplier = 8.0f;  // plants drawn per placed plant, at most (1..8: the tables are written for 8)
        float maxRadius = 512.0f;    // yd around the player, at most (up to 1024)
        float maxSpread = 3.0f;      // yd a copy moves from its plant, at most

        // Soft edges (step 7d, a test): plants shrink toward a border with bare ground and some spill onto it.
        bool edges = true;
        float edgeBand = 1.5f;       // yd inside the border over which plants shrink
        float edgeShrink = 0.5f;     // size at the border
        float edgeSpill = 2.0f;      // yd spilled plants go onto the bare ground, at most
        float edgeShare = 0.6f;      // spilled plants per plant near the border
        bool edgeSameTexture = true; // only onto bare ground of the same terrain texture
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
    const char* DensityStatus(); // GroundEffectDoodadDensity as last loaded

    // How the client picks a cell's ground effect (SXC_GRASS_PLACE_*; a client setting, kept; stock on an
    // older client), and the least painted share for SXC_GRASS_PLACE_PAINTED.
    bool HasPlacement();
    uint32_t Placement(float& minShare);
    void SetPlacement(uint32_t mode, float minShare);

    // Plants on slopes (client settings, kept): the steepest ground (least normal z) and the sink factor;
    // and the check of the placed plants near the player against the terrain.
    bool HasSlopes();
    void Slopes(float& minNormalZ, float& sink);
    void SetSlopes(float minNormalZ, float sink);
    bool Inspect(float radius, sxc_grass_inspect& out);
    unsigned ActorsFed();       // characters handed over last frame
    const sxc_grass_api* Api(); // null on an older client
}
