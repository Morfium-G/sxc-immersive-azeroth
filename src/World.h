// One snapshot per frame of where the player is and what the weather does (sxc.world). Every feature
// reads this instead of asking the client itself. Plus the point queries on the loaded world
// (terrain, liquids, a line trace), which answer false where nothing is loaded.
#pragma once

#include <cstdint>

namespace ia::world {
    constexpr int kMaxAreaChain = 8;

    struct Snapshot {
        bool inWorld = false;              // a player object exists
        float playerPos[3] = {};
        int mapId = -1;

        uint32_t areaChain[kMaxAreaChain] = {}; // most specific first: sub-area, zone, ...
        int areaCount = 0;

        bool outdoors = true;              // the client's verdict (gates fog, sky, outdoor light)
        int playerTerrainType = -1;        // TerrainType under the player (-1 none)

        float weatherRaw = 0.0f;           // storm intensity as the weather keeps it
        float weatherIntensity = 0.0f;     // 0..1 after the engine's dead zone
        int weatherType = 0;               // 0 fine, 1 rain, 2 snow, 3 sand

        float cameraPos[3] = {};
        const char* zoneText = "";
        const char* subZoneText = "";
    };

    // Re-reads everything. Once per frame, on the main thread.
    const Snapshot& Refresh();

    // The last snapshot Refresh() produced.
    const Snapshot& Current();

    // An area and its parents, most specific first. Returns how many were written (0 for area 0).
    int AreaChain(uint32_t area, uint32_t* out, int max);

    // The area of the terrain chunk under (x, y); false where no chunk is loaded.
    bool ChunkAreaAt(float x, float y, uint32_t& area);

    // An area's name ("" if unknown).
    const char* AreaName(uint32_t area);

    // Whether the line from -> to hits terrain or a map object (models never block); `fraction` is
    // how far along it the hit is (1 when it doesn't).
    bool Trace(const float from[3], const float to[3], float& fraction);

    // Whether the client offers the point queries below (an older client: they all answer false).
    bool HasPointQueries();

    // Where a world point is on the window, in pixels of `width` x `height`. False behind the camera.
    bool Project(const float pos[3], float width, float height, float& x, float& y);

    // A test weather the client shows (rain falls, snow drifts) as if the server sent it: type 0 fine,
    // 1 rain, 2 snow, 3 sand, intensity 0..1; type < 0 gives the server's weather back. The snapshot
    // and everything after it follow. False when the client can't (an older one).
    bool SetClientWeather(int type, float intensity);
    int ClientWeatherTest(); // its type, -1 off
}

// The terrain at a point: the drawn surface and what it's painted with. WMOs and doodads don't count.
namespace ia::terrain {
    // Height of the terrain surface at (x, y). False where no chunk is loaded or on a hole.
    bool HeightAt(float x, float y, float& z);

    // What a terrain layer is, for the scoped tables: the chunk's area, the texture path (valid until
    // the next query), the ground effect (GroundEffectTexture ID, 0 none) and its TerrainType (-1 none).
    struct Surface {
        uint32_t area = 0;
        const char* texture = nullptr;
        uint32_t groundEffect = 0;
        int terrainType = -1;
    };

    // Every layer's painted strength at (x, y) (layer 0 gets what the others leave). `serial` changes
    // whenever the chunk there is loaded anew. False where no chunk is loaded or on a hole.
    struct LayerWeights {
        uint32_t serial = 0;
        int layers = 0;
        float weight[4] = {};
        Surface surface[4];
        int dominant = -1; // the client's own dominant layer for the cell
    };
    bool LayerWeightsAt(float x, float y, LayerWeights& out);

    // The dominant layer's surface at (x, y) (bare ground: area only). False where no chunk is
    // loaded or on a hole.
    bool SurfaceAt(float x, float y, Surface& out);

    // Terrain liquid (rivers, lakes, sea; not WMO liquid) at (x, y): its surface and LiquidType ID.
    bool LiquidAt(float x, float y, float& z, uint32_t& liquidType);

    // LiquidType.dbc's Type column: 0 water, 1 ocean, 2 magma, 3 slime; -1 unknown.
    enum LiquidCategory : int { kLiquidWater = 0, kLiquidOcean = 1, kLiquidMagma = 2, kLiquidSlime = 3 };
    int LiquidCategoryOf(uint32_t liquidType);
    const char* LiquidNameOf(uint32_t liquidType);
}
