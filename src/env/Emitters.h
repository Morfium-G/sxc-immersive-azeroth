// Emitters (DoodadEmitterProperties; DoodadSourceAssignment's EmitterID): heat or cold, moisture and
// drafts on models. The client places them where the rows say, exactly like the rows' lights (sxc.lights
// emitters(): attachment point, bone, particle emitter or origin, the offset, following the model), on
// any renderer; this side reads them once a frame and gives them their meaning. The world fields take
// the temperature and moisture ones as local sources (env/Fields: they warm, cool, melt, dry and wet
// what's around them, and the fog follows), the wind the drafts (env/Wind).
#pragma once

#include <cstdint>
#include <vector>

namespace ia::emitters {
    enum Type : int { kTemperature = 0, kMoisture = 1, kDraft = 2 };

    struct Emitter {
        int type = kTemperature;
        float pos[3] = {};
        float dir[3] = { 0.0f, 0.0f, -1.0f }; // world, unit: the row's direction through the model
        float strength = 0.0f;   // temperature: the air's degC right at the source; moisture: 0..1 at the source; draft: wind strength at the source
        float radius = 8.0f;     // yd, scaled with the model
        float falloff = 1.0f;
        float cosSpread = -1.0f; // cos of its spread around `dir` (-1: every way; a draft then blows outward)
        uint32_t row = 0, id = 0;
    };

    // Once per frame, before the wind and the fields: last frame's placed emitters with their properties.
    void Update();

    // This frame's emitters (the client's range around the camera).
    const std::vector<Emitter>& All();

    // How much an emitter reaches a point `distance` yd away, 0..1: the lights' window, (1 - (d/r)^2)^2
    // to the falloff.
    float Reach(const Emitter& e, float distance);

    // How much of an emitter's cone a point `d` (from the emitter; `distance` its length) is in, 0..1: full
    // inside the spread, fading over its last tenth; everywhere within a yard of the source and for one
    // that spreads every way.
    float Cone(const Emitter& e, const float d[3], float distance);

    // The drafts at a point, as a wind vector (x, y; wind strength units, 1 = a strong wind).
    void DraftAt(const float pos[3], float& x, float& y);

    struct Stats {
        unsigned placed = 0;        // emitters the client placed
        unsigned temperature = 0, moisture = 0, draft = 0;
        unsigned unknown = 0;       // placed with an EmitterID that has no DoodadEmitterProperties row
    };
    Stats GetStats();
    const char* TableStatus();

    // The world left: nothing placed.
    void Reset();
}
