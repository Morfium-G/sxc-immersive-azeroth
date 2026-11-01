// Ground marks: what heat and cold leave on the ground near the player and that lasts a while. Burnt where
// the ground's temperature (env/Fields: the air moved by hot liquids, hot ground and emitters) is above
// BurnAt; frozen where it's below FreezeAt and well below the place's air (a cold source, not a cold
// zone). A mark grows while its source stays (a creature carrying a heat emitter leaves a burnt trail)
// and fades by itself after: the grass grows back, the frost thaws. On the world fields' grid (2 yd, 128
// cells, following the player); sent to the client as sxc.cover's marks (SXC_MARK_*), with what each does
// to plants (sxc.grass's set_marks: burnt grass blackens, then burns away; frozen grass turns pale blue
// and stiff). The life channel (blight / bloom) is reserved: nothing sets it yet.
#pragma once

#include "World.h"

namespace ia::marks {
    struct Settings {
        bool enabled = true;
        // Burn: heat above this (degC) marks the ground, fully in burnSeconds at the threshold (faster
        // hotter); it fades over regrowSeconds once the heat is gone.
        float burnAt = 90.0f;
        float burnSeconds = 20.0f;
        float regrowSeconds = 900.0f;
        float sootSeconds = 8.0f;     // the black fades over this once the heat is gone
        // Frost: colder than this (degC) and freezeBelowAir under the place's air.
        float freezeAt = -5.0f;
        float freezeBelowAir = 8.0f;
        float freezeSeconds = 15.0f;
        float thawSeconds = 120.0f;   // at freezeAt; faster the warmer it is
        // What they do to plants.
        float burnTint = 0.9f;        // how black burnt plants get (0..1)
        float burnSize = 0.0f;        // size of a fully burnt plant (0: gone)
        float frostTint = 0.55f;      // how pale blue frozen plants get
        float frostWind = 0.15f;      // x the wind on fully frozen plants (stiff)
    };
    Settings& Config();

    // Once per frame, after fields::Update.
    void Update(float dt, const world::Snapshot& snap);

    // The world left / the mod off: no marks (the client's are cleared).
    void Stop();

    struct Stats {
        unsigned burnt = 0, frozen = 0;   // cells with any
        float mostBurnt = 0.0f, mostFrozen = 0.0f;
        double ms = 0.0;                  // the last step
        bool sent = false;                // the client takes them (sxc.cover and sxc.grass have marks)
    };
    Stats GetStats();

    // The marks at a position (SXC_MARK_* channel), 0 outside the grid.
    float At(const float pos[3], int channel);

    // Testing: marks a disc around a point (value 0..1 added) / clears everything.
    void Deposit(const float pos[3], int channel, float value, float radius);
    void Clear();
}
