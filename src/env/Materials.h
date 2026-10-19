// Ground materials: what the ground is made of, and where (WXL: orchestration docs/r&d/immersion/
// world-fields.md, "Kind 1").
//
//  - GroundMaterialSelector.cdbc: WHERE a material lies. Scoped: the most specific row wins, place
//    first (the terrain cell's area, its parents, the map, global), then within a place TexturePath,
//    GroundEffectID, TerrainType, everything. Liquid rows (LiquidType set) name a liquid's material.
//  - GroundMaterial.cdbc: WHAT a material is. A field of -1 takes the value from the Parent material.
// Feature tables (SurfaceCover, ...) are keyed by material and inherit along the same Parent chain.
#pragma once

#include <cstdint>

namespace ia::materials {
    struct Values {
        float restMoisture = 0.3f;    // 0 bone dry .. 1 soaked: what the texture already shows
        float stiffness = 0.0f;       // resistance to deformation: 0 gives way completely .. 1 rigid
        float absorbency = 0.5f;      // how much outside moisture reaches it: rock ~0.1 .. sand/soil ~0.9
        float temperature = 0.0f;     // degC of the material itself (magma ~1000); only if hasTemperature
        uint32_t flags = 0;           // reserved
        bool hasTemperature = false;  // a row in the chain sets Temperature
        bool restMoistureSet = false; // a row in the chain sets RestMoisture (else the default above)
    };

    // Rereads both tables when they changed. Once per frame.
    void Update();

    // The material of a terrain layer: its chunk's area, the map, the layer's texture path (may be
    // null), ground effect and that effect's TerrainType (-1 none). 0 = no material. Cached.
    uint32_t Select(uint32_t areaId, int mapId, const char* texturePath, uint32_t groundEffectId, int terrainType);

    // The material of a liquid (LiquidType.dbc ID) by place. 0 = no row (callers fall back to built-in
    // values per liquid category).
    uint32_t SelectLiquid(uint32_t liquidType, uint32_t areaId, int mapId);

    // A material's values, -1 fields taken along its Parent chain; defaults where no row sets them.
    Values Get(uint32_t materialId);

    uint32_t Parent(uint32_t materialId); // 0 = none or unknown
    bool Exists(uint32_t materialId);
    const char* Name(uint32_t materialId); // "" for none / unknown

    unsigned MaterialCount();
    unsigned SelectorCount();
    const char* Status();
    // Changes whenever either table does.
    uint32_t Generation();

    // Walks a Parent chain safely (stops at 0, unknown IDs and cycles): fn(id) for the material, its
    // parent, ... until fn returns false.
    template <class Fn>
    void ForChain(uint32_t materialId, Fn fn) {
        for (int depth = 0; materialId && depth < 16; depth++) {
            if (!fn(materialId)) {
                return;
            }

            materialId = Parent(materialId);
        }
    }
}
