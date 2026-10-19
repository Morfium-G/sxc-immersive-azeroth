// The fog, as the client draws it (sxc.fog: a froxel volume, Vulkan only). For now a test field: fog
// deposited around the player from the Fog panel, burning off by a half-life, lying on the ground (or
// on water), plus a haze. WXL's fog field (the kinds and their rules, FogSettings) moves in next and
// feeds the same field.
#pragma once

#include <cstdint>
#include "World.h"

namespace ia::fog {
    // The look (the panel; not saved yet).
    struct Look {
        bool enabled = true;
        bool debug = false;          // magenta
        int colourMode = 0;          // 0 the place's stock fog colour, 1 own
        float colour[3] = { 0.75f, 0.78f, 0.82f };
        float brightness = 1.0f;
        float densityScale = 1.0f;
        float noiseAmount = 0.6f;
        float noiseTile = 64.0f;     // yd
        float drift = 1.0f;          // x the wind
        float farDistance = 250.0f;  // yd
        float hazeDensity = 0.0f;    // per yd at the ground (0.002 = a light haze)
        float hazeThickness = 30.0f; // yd
    };
    Look& Settings();

    // Testing: fog around the player. `amount` 0..1 at the centre, fading to 0 at `radius` yd; as a
    // layer `thickness` yd high, or as a flat pool `thickness` yd deep.
    void Deposit(float amount, float radius, float thickness, bool pool);
    void Clear();
    float& BurnOffSeconds(); // the half-life the test fog burns off with (0 = it stays)

    // Once per frame, after the world snapshot and the wind.
    void Update(float dt, const world::Snapshot& snap);

    // The mod is off or the world was left: the client stops drawing it (the test fog is forgotten).
    void Stop();

    bool Available();      // the client draws volumetric fog (Vulkan)
    const char* Status();  // what it did last frame, or why not
    unsigned CellsWithFog();
}
