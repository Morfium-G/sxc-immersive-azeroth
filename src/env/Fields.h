// World fields: named values over the world near the player (WXL: orchestration docs/r&d/immersion/
// world-fields.md, "Kind 2"). Consumers ask by name once (Find) and then sample by handle.
//
//  - "temperature" (degC): the place's climate (climate/Climate), on the grid warmed near hot liquids
//    (magma) and hot ground (painted materials with a Temperature, e.g. lava streams).
//  - "moisture" (0 bone dry .. 1 soaked): the ground, on a 2 yd grid around the player. It rests at an
//    equilibrium -- the material's RestMoisture, wetter near a wet liquid (Absorbency x how close and
//    how low; magma doesn't wet), drier in hot dry air (and near magma) -- and follows its zone's rain
//    soak (env/Regional) by its Absorbency and open sky. Submerged ground takes the liquid's moisture.
//    What a liquid does: its GroundMaterialSelector row (LiquidType -> material: RestMoisture,
//    Temperature), else built in by category (water, ocean, slime: wet; magma: dry, 1000 degC).
//  - "snow" (yd): snow lying on the spot, fallen + painted. Fallen snow follows its zone's record,
//    held back where the spot is warmer than its zone (heat nearby, a sunny slope) and absent under
//    roofs and liquids. Painted snow (SurfaceCover rows whose CoverMaterial is snow) melts toward its
//    cap by the spot's own temperature (air, hot ground and liquids, the sun on its slope), down to
//    nothing on heat sources, and grows back when it's colder; its melt water wets the ground
//    (env/Snow.h has the rules).
#pragma once

#include <cstdint>
#include "World.h"

namespace ia::fields {
    // A field's handle by name ("temperature", "moisture", "snow"); -1 if unknown.
    int Find(const char* name);
    int Count();
    const char* Name(int field);

    // The field's value at a world position. False where it isn't known (outside the grid, or ground
    // that isn't loaded yet). Temperature is always known (the climate beyond the grid).
    bool Sample(int field, const float pos[3], float& out);

    // Moisture details at a position, for the panel.
    struct MoistureDetail {
        bool known = false;
        float rest = 0.0f, absorbency = 0.0f; // the ground's materials (blended by painted strength)
        float open = 1.0f;                    // 1 open sky .. 0 roofed (rain reaches it)
        bool submerged = false;
        float waterDistance = -1.0f;          // yd to the nearest submerged cell (-1 = none in range)
        float heightAboveWater = 0.0f;        // yd above that water's surface
        float shore = 0.0f;                   // 0..1 how much the water nearby counts
        uint32_t liquid = 0;                  // that liquid's LiquidType (the one here when submerged)
        float liquidMoisture = 0.0f;          // what it does to the ground's moisture (magma 0, water 1)
        bool liquidHot = false;               // it has a temperature of its own...
        float liquidTemperature = 0.0f;       // ...this one
        float heat = 0.0f;                    // 0..1 how much its heat reaches here
        float hotShare = 0.0f, hotTemperature = 0.0f; // painted strength of hot materials here, and their degC
        float groundHeat = 0.0f, groundHeatTemperature = 0.0f, groundHeatDistance = -1.0f; // the nearest hot ground: reach, degC, yd
        float airTemperature = 0.0f;          // the place's climate
        float temperature = 0.0f, humidity = 0.0f; // the ground's (air + heat), the air's humidity
        float equilibrium = 0.0f, value = 0.0f;
        // The zone this spot belongs to, the share of the player's zone around it (rain falls by it),
        // the zones' soak mix it follows, the rain it got last step, and how fast it changes now.
        uint32_t zone = 0;
        float zoneShare = 0.0f, zoneSoakMix = 0.0f, rainShare = 0.0f, changePerSecond = 0.0f;
    };
    MoistureDetail Moisture(const float pos[3]);

    // Snow details at a position, for the panel.
    struct SnowDetail {
        bool known = false;
        float fallen = 0.0f, target = 0.0f;  // yd now, yd it's heading to
        float zoneSnow = 0.0f;               // the zones' fallen snow around it (blended across borders)
        float hold = 1.0f;                   // 0..1 how much of it this spot holds (warmer than its zone: less)
        float painted = 0.0f;                // yd of painted snow cover here (0 = none)
        float keep = 1.0f, cap = 1.0f;       // its share left now, and where it's heading
        float keptShare = 0.0f, goneTemperature = 0.0f; // the rows' melt columns
        float temperature = 0.0f;            // effective: ground surface + sun
        float sun = 0.0f;                    // the sun's part of it
        float zoneTemperature = 0.0f;        // what the zone's snow sees
        float heatReach = 0.0f;              // 0..1 local heat (melts pits)
        float meltWet = 0.0f;                // moisture its melt water adds (0..1 soak)
    };
    SnowDetail Snow(const float pos[3]);

    // For GPU consumers (the cover, terrain wetness): moisture above the material's rest (0..255 =
    // 0..1), one byte per cell, world-aligned and toroidal: cell (i, j) (i = floor(x / cellSize)) lives
    // at [mod(j, size) * size + mod(i, size)], valid for i in [firstI, firstI + size) (same for j).
    // `version` changes whenever the data does. Beyond the grid: per terrain chunk (env/Regional's zone
    // map, same layout), the excess of the zone it belongs to, scaled to match the grid's outer ring.
    struct WetGrid {
        int size = 0;
        float cellSize = 0.0f;
        int firstI = 0, firstJ = 0;
        const uint8_t* excess = nullptr;
        const uint8_t* normal = nullptr; // per cell 2 bytes: the ground's normal x, y as 0..255 (128 = 0)
        uint32_t version = 0;
        float farExcess = 0.0f;          // beyond everything known: the player's zone's value

        int farSize = 0;
        float farCellSize = 0.0f;
        int farFirstI = 0, farFirstJ = 0;
        const uint8_t* farBytes = nullptr;
        const float* farValues = nullptr;
    };
    const WetGrid& Wet();

    // For the cover: fallen snow (yd) and painted snow's kept share (0..1) at a world position, from
    // the last published values: the near grid, fading into the zones' values (per terrain chunk)
    // within its last yards, the player's zone's beyond. Cheap enough per cover cell.
    void SnowAt(float x, float y, float& fallen, float& keep);
    // Changes whenever the published snow values do (they're published when they moved by about a
    // centimetre, or the grid moved). Active: anything differs from "no fallen snow, everything kept".
    uint32_t SnowVersion();
    bool SnowActive();

    // For the cover (sxc.cover's maps, cover/Cover): per cell 4 floats -- fallen snow (yd), painted snow's
    // kept share (0..1), moisture above the ground's rest (0..1), the water surface (world z; kNoWater:
    // none within 12 yd). Near: the grid's layout, every cell filled (cells not known yet take the zones'
    // values, and the grid fades into them over its last yards); far: per terrain chunk (env/Regional's
    // zone map), the zones' values. Rebuilt every step (4 a second); `version` changes with it.
    constexpr float kNoWater = -100000.0f;

    struct CoverMaps {
        int size = 0;
        float cellSize = 0.0f;
        int firstI = 0, firstJ = 0;
        const float* cells = nullptr;
        int farSize = 0;
        float farCellSize = 0.0f;
        int farFirstI = 0, farFirstJ = 0;
        const float* farCells = nullptr;
        float outside[3] = { 0.0f, 1.0f, 0.0f }; // beyond the far map: the player's zone
        uint32_t version = 0;
    };
    const CoverMaps& CoverGround();

    struct Stats {
        unsigned filled = 0, cells = 0, water = 0, hotGround = 0, snowPublished = 0;
        double fillMs = 0.0, transformMs = 0.0, tickMs = 0.0;
        float rainSoak = 0.0f;
    };
    Stats GetStats();

    // Once per frame, after climate::Update(), wind::Update() and regional::Update().
    void Update(float dt, const world::Snapshot& snap);

    // Forget everything (the world left; materials or climate changed: refill).
    void Reset();

    // Testing: sample every cell again (starting from its zone's state, as if just arrived).
    void Refill();
}
