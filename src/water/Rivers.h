// Rivers (kb: rivers): RiverPath and RiverNode, the rivers of the player's map handed to the client
// (sxc.water's set_rivers), which draws them into the flow field our water follows. Sent again when a
// table changes or the map does.
#pragma once

#include <cstdint>

namespace ia::world {
    struct Snapshot;
}

namespace ia::rivers {
    struct Stats {
        uint32_t rivers = 0, nodes = 0;   // sent for this map
        bool client = false;              // the client takes rivers
    };

    // Once per frame.
    void Update(const world::Snapshot& snap);

    const Stats& Current();

    // The flow at world (x, y) as the client draws it (yd / s) and how much river is there; false outside
    // its field or on an older client.
    bool FlowAt(float x, float y, float& flowX, float& flowY, float& weight);

    // The liquid's surface height under (x, y, z) (WMO or terrain); false where there is none.
    bool SurfaceAt(const float pos[3], float& height);
}
