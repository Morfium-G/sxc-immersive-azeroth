// One snapshot per frame of where the player is and what the weather does (sxc.world). Every feature
// reads this instead of asking the client itself.
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
}
