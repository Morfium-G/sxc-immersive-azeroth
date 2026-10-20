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
        bool farFog = true;          // each zone's typical fog beyond the near field (fog/Far)
        float handover = 64.0f;      // yd: the near fog fades into the far fog over this band at the field's edge (WXL's; wider = softer seam, less near detail)
        int debugPlaces = 0;         // debug: the fog coloured by place (0 off, 1 by zone, 2 by area), blended where places mix
        bool debugNearFar = false;   // debug: the near field green, the far fog blue, the haze grey (WXL's view 4)
        float fadeIn = 1.5f;         // s: a near cell that just started fades in over the far fog
    };
    Look& LookSettings();

    // Once per frame, after fog::Update (the field): the field, the far map and the wake (fog/Wake) to
    // the client.
    void Draw(float dt, const world::Snapshot& snap);

    // The mod is off or the world was left: the client stops drawing it.
    void Stop();

    bool Available();     // the client draws volumetric fog (Vulkan)
    const char* Status(); // what it did last frame, or why not
    unsigned Uploads();
}
