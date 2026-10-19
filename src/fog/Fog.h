// The fog as the client draws it (sxc.fog: a froxel volume, Vulkan only), fed from the fog field
// (fog/Field): per cell, every kind's light layer (density, floor, thickness, patchiness) and heavy pool
// (a flat top over the ground), coloured by the kinds' colours (FogSettings' colour columns over the
// place's stock fog colour). WXL's renderer (features/GroundFog) built its textures the same way.
#pragma once

#include "World.h"

namespace ia::fog {
    // The look (the panel; not saved).
    struct Look {
        bool enabled = true;
        bool debug = false;          // magenta
        float brightness = 1.0f;     // x every colour
        float densityScale = 1.0f;
        float noiseAmount = 0.6f;    // x the kinds' patchiness
        float noiseTile = 64.0f;     // yd
        float drift = 1.0f;          // x the wind
        float farDistance = 250.0f;  // yd
        float poolSoftness = 0.6f;   // the pools' soft top, share of their depth
        int floorReach = 2;          // cells: the light layer starts on the lowest ground this far around
        float hazeDensity = 0.0f;    // an extra haze everywhere (per yd at the ground; the haze kind is in the field)
        float hazeThickness = 30.0f; // yd
        float stepLength = 0.5f;     // yd: the step near the camera (smaller: smoother thin layers, more GPU)
        bool froxels = false;        // the froxel volume instead of a march per pixel (cheaper, coarse)
        int maxSteps = 160;          // per pixel: the most steps a ray takes
    };
    Look& LookSettings();

    // Once per frame, after fog::Update (the field).
    void Draw(const world::Snapshot& snap);

    // The mod is off or the world was left: the client stops drawing it.
    void Stop();

    bool Available();     // the client draws volumetric fog (Vulkan)
    const char* Status(); // what it did last frame, or why not
    unsigned Uploads();
}
