// SurfaceCover.cdbc: which ground materials carry a cover (snow, sand, mud) and how it looks and
// behaves. Keyed by GroundMaterial ID (where a material lies is env/Materials' job). A field of -1
// takes that field from the row of the material's Parent, its parent, ... (ZOffset: exactly -1 or
// -1000 and below, since other negatives are real offsets). No material, or no row along its chain =
// no cover; Depth 0 switches it off.
//
// The fields read it now (painted snow); the cover itself (step 5) reads the rest.
#pragma once

#include <cstdint>

namespace ia::covertable {
    // Flags bits. They come from the row that decides Depth, except kFlagIgnoreSpecular, which
    // belongs to the CoverTexture named in the same row.
    enum Flag : uint32_t {
        kFlagPreventUnderwater = 0x1, // no cover below a liquid surface
        kFlagPreventOnLand = 0x2,     // cover only below a liquid surface (river and sea floors)
        kFlagUseVertexColor = 0x4,    // tint by the terrain's vertex colours
        kFlagIgnoreSpecular = 0x8,    // don't use the CoverTexture's _s specular map
    };

    struct Values {
        float depth = 0.0f;          // yd; 0 = no cover
        float maxSlope = 45.0f;      // degrees: steeper ground holds no cover...
        float slopeFade = 15.0f;     // ...thinning out over this many degrees below that
        float driftNoise = 0.35f;    // depth variation, share of the depth
        float edgeBreakup = 0.5f;    // 0 smooth .. 1 ragged, patchy edges
        float rim = 0.3f;            // trench rim height, share of the depth
        float relaxSeconds = 30.0f;  // trench back to flat
        uint32_t tintColor = 0;      // RGB
        float tintStrength = 0.0f;   // 0 = the plain cover colour .. 1 = the tint colour
        int coverTexture = 0;        // 0 = none, 1.. = CoverTexturePath(id)
        float zOffset = 0.0f;        // yd the cover's base sits above (+) or below (-) the terrain
        float wetness = 0.0f;        // 0 dry .. 1 soaked
        uint32_t flags = 0;          // Flag bits
        uint32_t material = 0;       // the ground material (0 = none)
        uint32_t coverMaterial = 0;  // what lies on top: the row's CoverMaterial, else the ground material
        float stiffness = 0.0f;      // the cover material's: 0 feet press it to the ground .. 1 rigid
        float meltKeptShare = 0.6f;  // painted snow: the share mild weather (to 15 degC) melts it down to...
        float meltGoneTemperature = 25.0f; // ...and the degC at which it's gone
        float moistureMin = 2.0f;    // the ground moisture the cover needs to form (mud); above 1 = off
    };

    inline bool MoistureGated(float moistureMin) { return moistureMin <= 1.0f; }

    // Rereads SurfaceCover when it or the materials changed. Once per frame, after materials::Update().
    void Update();

    // The cover of a terrain layer: its material (env/Materials::Select), then that material's row
    // along its Parent chain. Fields no row sets keep the defaults above. Cached.
    Values Resolve(uint32_t areaId, int mapId, const char* texturePath, uint32_t groundEffectId, int terrainType);

    // Whether a cover material is snow: the fallback snow material (what the selectors give for
    // TerrainType 3 at this place, or globally) or a child of it.
    bool IsSnow(uint32_t coverMaterial, uint32_t areaId, int mapId);

    // The look fallen snow takes on a layer without a painted cover: the layer's own row if its
    // CoverMaterial is snow (even with Depth 0), else the fallback snow's row, else built-in values.
    Values SnowLook(uint32_t areaId, int mapId, const char* texturePath, uint32_t groundEffectId, int terrainType);

    // The distinct CoverTexture paths the table names, and whether a row naming one ignores its specular map.
    int CoverTextureCount();
    const char* CoverTexturePath(int index); // 1..CoverTextureCount()
    bool CoverTextureIgnoresSpecular(int index);

    unsigned RowCount();
    const char* Status();
    uint32_t Generation();
}
