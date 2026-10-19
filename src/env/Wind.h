// Wind: WindProfile.cdbc (Global / Map / Area scopes, -1 inherits) + the live weather + a slowly
// drifting steady wind + gusts that travel across the ground, calmed under roofs and in the lee of
// walls and cliffs (env/Shelter). WXL design: orchestration docs/r&d/immersion/environment-state.md.
//
// Directions are compass bearings the wind blows TOWARD: 0 = north (+x), 90 = east (-y).
#pragma once

#include <cstdint>
#include "World.h"

namespace ia::wind {
    // Profile fields in table order (after ID / ScopeType / ScopeID).
    enum Field {
        GroundMin, GroundMax, AloftMin, AloftMax, AloftHeight,
        WeatherInfluence, GustStrength, GustFrequency, TerrainWeight,
        LockedDirection, LockStrength,
        FieldCount
    };
    const char* FieldName(int field);

    // Where a resolved field came from, for the panel.
    enum class Source { BuiltIn, Global, Map, Area };

    struct Resolved {
        float value[FieldCount] = {};
        Source source[FieldCount] = {};
        uint32_t sourceId[FieldCount] = {}; // map or area that supplied it
        uint32_t flags = 0;                 // the most specific row's Flags (reserved)
    };

    struct Sample {
        float dirX = 1.0f, dirY = 0.0f; // unit vector in world xy
        float strength = 0.0f;          // 0..1, gusts can push past 1
        float gust = 0.0f;              // 0..1, how much of a gust passes right here
        float open = 1.0f;              // 1 open sky, 0 roofed over (already in strength)
        float lee = 1.0f;               // 1 exposed, lower in the lee of a wall or cliff (already in strength)
    };

    // Once per frame, after world::Refresh().
    void Update(float dt, const world::Snapshot& snap);

    // The wind at a world position; heightAboveGround blends ground -> aloft.
    Sample At(const float pos[3], float heightAboveGround = 0.0f);

    // --- the panel ---
    const char* Status();
    const Resolved& Target();  // the profile here, before blending
    const Resolved& Current(); // the blended profile in use
    float SteadyGround();      // the steady strength at ground level now
    float SteadyAloft();
    float BearingDegrees();    // the steady direction now
    float EffectiveWeather();  // the weather intensity the model used

    // Testing (not saved): scales every sample's strength (1 = as authored).
    void SetStrengthMultiplier(float multiplier);
    float StrengthMultiplier();
    // Testing: < 0 follows the live weather, 0..1 forces an intensity.
    void SetWeatherOverride(float intensity);
    float WeatherOverride();
}
