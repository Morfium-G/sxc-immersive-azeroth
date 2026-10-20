// The fog field (WXL: env/Fog; orchestration docs/r&d/immersion/ground-fog-design.md). One fog system
// with several kinds; per kind an amount (0 none .. 1 its full density) on a grid around the player,
// the moisture grid's footprint (128 x 128 cells of 2 yd, +-128 yd).
//
// Each kind's amount moves toward a target per cell: up at its build-up rate, down at its burn-off
// rate (half-lives). The targets come from the kind's rules (moisture, temperature, water, wind, time
// of day, the place's fogginess); the wind carries the fog, heavy fog runs downhill and pools in
// hollows, each zone remembers its typical fog. fog/Fog hands the result to the client to draw.
#pragma once

#include <cstdint>
#include "World.h"

namespace ia::fog {
    enum Kind : int {
        kMist,     // radiation mist: cool, calm, humid nights; pools in low ground
        kWaterFog, // over water warmer than the air
        kSteam,    // rain or snow on hot ground, hot springs, drying ground
        kAreaFog,  // a place's own fog (swamps, haunted forests: AreaClimate Fogginess)
        kHaze,     // rain and snow: a raised base everywhere
        kKindCount
    };
    const char* KindName(int kind);

    // Per kind, where the player is: from FogSettings.cdbc for the player's place, unless
    // `panelOverride` (the panel's values stand; testing, not saved).
    struct KindSettings {
        bool enabled = true;        // drawn and its rules run; its amounts still burn off
        bool panelOverride = false;
        float strength = 1.0f;
        float buildUpSeconds = 300.0f;
        float burnOffSeconds = 900.0f;
        float thickness = 4.0f;     // yd at full amount
        float density = 0.05f;      // extinction per yd at full amount, at the ground
        float windThin = 0.5f, windDrift = 0.5f, patchiness = 0.6f, weight = 0.0f;
    };
    KindSettings& Settings(int kind);

    constexpr int kGridSize = 128;
    constexpr float kCellSize = 2.0f;

    // The grid, for fog/Fog and the panel: world-aligned toroidal, cell (i, j) (i = floor(x / kCellSize))
    // at [mod(j, size) * size + mod(i, size)], valid for i in [firstI, firstI + size). `version`
    // changes whenever any amount does.
    struct Grid {
        int firstI = 0, firstJ = 0;
        const float* amount[kKindCount] = {};
        float maxAmount[kKindCount] = {};
        uint32_t version = 0;
        const float* ground = nullptr;      // the surface the fog lies on: terrain, liquid, or a WMO floor on it
        const uint8_t* groundKnown = nullptr;
        const uint8_t* block = nullptr;     // 0 not traced, 1 open, 2 WMO floor, 3 WMO structure (no fog)
        const float* floor[kKindCount] = {}; // water fog and steam: the height the fog came from (else null)
        const float* pool[kKindCount] = {};  // heavy fog, in amount units (up to 4 in hollows)
        const uint32_t* area = nullptr;      // per cell, its chunk's area (0 = not known yet)
        const uint8_t* fresh = nullptr;      // 1: scrolled in, not started by the rules yet
        uint32_t groundVersion = 0;
        unsigned groundMissing = 0;
    };
    const Grid& Current();

    // The rules' inputs where the player is (the panel's "why").
    struct RulesInputs {
        bool valid = false;
        float airNow = 0.0f, airHourAgo = 0.0f, water = 0.0f; // degC
        float cooling = 0.0f;                                  // 0..1 (2.5 degC an hour = 1)
        float humidity = 0.0f, fogginess = 0.0f, night = 0.0f;
        float wind = 0.0f, windDirX = 1.0f, windDirY = 0.0f;   // the steady ground wind
        int weatherType = 0;
        float precipitation = 0.0f, rain = 0.0f, snow = 0.0f;
    };
    const RulesInputs& Inputs();

    // Why a kind's fog is what it is where the player stands: the rule's factors (they multiply;
    // "alternative" ones: only the largest counts), the target, and what moved the amount per second.
    struct Factor {
        const char* name = "";
        float input = 0.0f;
        float factor = 1.0f;     // -1: an input of the factor above
        bool alternative = false;
    };
    struct Explanation {
        int count = 0;
        Factor factors[10];
        float target = 0.0f, amount = 0.0f;
        float rules = 0.0f, wind = 0.0f;
        float slopeDegrees = 0.0f, heavyShare = 0.0f;
        float pool = 0.0f, poolTarget = 0.0f, poolRules = 0.0f, poolWind = 0.0f, poolFlow = 0.0f;
        bool valid = false;
    };
    const Explanation& Explain(int kind);

    struct FieldSettings {
        float poolSpeed = 1.0f;   // how fast heavy fog runs downhill (0 = it stays put)
        float steepStart = 30.0f; // degrees: a growing share of the ground-hugging fog doesn't form ...
        float steepFull = 45.0f;  // ... and from here none
        bool blendPlaces = true;  // the places' kind settings blend across area borders like the climate
    };
    FieldSettings& Field();

    // Each zone's typical fog (the memory).
    struct ZoneState {
        uint32_t zone = 0;
        float amount[kKindCount] = {};
        float target[kKindCount] = {};
        bool inRange = false;
    };
    unsigned ZoneStates(ZoneState* out, unsigned max);
    // A zone's typical amount of a kind (0 for a zone not seen, and for the kinds that need detail:
    // water fog, steam), and that zone's settings of the kind (for the far map).
    float ZoneAmount(uint32_t zone, int kind);
    KindSettings ZoneSettings(int kind, uint32_t zone);

    // A kind's settings in a cell (storage index): its place's, blended toward the other place around.
    KindSettings SettingsAt(int kind, int index);
    // The share of a kind's fog that's heavy in a cell (its place's Weight).
    float HeavyShare(int kind, int index);
    // The other place a cell's settings blend toward, and its share (0: none).
    float BlendOf(int index, uint32_t& otherArea);
    float Progress(int kind);

    // Once per frame, after the fields. Steps the field four times a second.
    void Update(float dt, const world::Snapshot& snap);

    // --- testing (the panel; not saved) ---
    void Deposit(int kind, float amount, float radius); // around the player, smooth to 0 at `radius` yd
    void Clear();
    void FastForward(); // every cell straight to its target
    void SetPaused(bool paused);
    bool Paused();

    struct Stats {
        double tickMs = 0.0, rulesMs = 0.0;
        unsigned ticks = 0;
        unsigned cellsWithFog[kKindCount] = {};
    };
    Stats GetStats();

    void Reset();
}
