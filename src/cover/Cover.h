// The covers as the client draws them (sxc.cover: our terrain shader, Vulkan only). The client asks per
// terrain chunk which cover each texture layer carries (the resolver: SurfaceCover through the ground
// materials, env/CoverTable) and shows it wherever the layer is painted; fallen snow lies on top, from
// the world fields' snow (env/Fields), and the ground darkens and shines where it's wet. WXL drew the
// cover as a mesh of its own over the terrain (features/SurfaceCover*); this is its look, in the
// terrain's own draw (port plan, step 5b).
#pragma once

#include <cstdint>
#include <vector>
#include "World.h"
#include "env/CoverTable.h"

namespace ia::cover {
    // The look (the panel; not saved).
    struct Look {
        bool enabled = true;
        bool fallenSnow = true;
        bool depth = true;           // the terrain raised onto the covers (tessellation, where the GPU has it)
        float tessDetail = 0.015f;   // yd per segment per yd of distance (smaller: finer, more GPU)
        float tessReach = 250.0f;    // yd: subdivided within this
        int tessMost = 24;           // segments per triangle edge at most (fine enough for footprints near the camera)
        bool trails = true;          // feet press the cover down (step 5d; with depth)
        float trailPersist = 600.0f; // s: the coarse layer keeps a trail this long
        float trailSlur = 0.6f;      // yd of edge softening per yd of cover depth (deep snow: slurred trails)
        float trailRim = 1.0f;       // x the rows' rims
        float trailReach = 45.0f;    // yd: units this close to the player leave trails
        int trailCarve = 1;          // where a foot carves: 0 through the cover, 1 near contact, 2 up to trailLift (SXC_COVER_CARVE_*)
        float trailLift = 0.3f;      // yd above the unit: mode 2's height
        int debug = 0;               // 0 off, 1 what lies where, 2 wetness, 3 entries, 4 the maps' reach, 5 the height, 6 trails
        float brightness = 0.93f;    // a plain cover's colour
        float wetDarkening = 0.45f;  // how much darker soaked ground gets (WXL's terrain wetness strength)
        float thinDepth = 0.08f;     // yd: fallen snow hides the ground below from this depth
        float depthScale = 1.0f;     // testing multipliers on what the table says
        float breakupScale = 1.0f;
        float driftScale = 1.0f;
    };
    Look& LookSettings();

    // One entry the client holds (the panel).
    struct Entry {
        uint32_t material = 0;   // the ground material whose row it is
        bool snow = false;       // its cover material is snow
        covertable::Values values;
        unsigned uses = 0;       // layers resolved to it (painted + fallen-snow looks)
    };
    const std::vector<Entry>& Entries();

    // Once per frame, after fields::Update: the tables, the maps and the look to the client.
    void Update(const world::Snapshot& snap);

    // The mod is off or the world was left: the client stops drawing covers.
    void Stop();

    bool Available();      // the client draws covers
    const char* Status();  // the client's line
    unsigned Resolved();   // chunks the client asked about since the tables last changed
    int TextureState(int texture); // sxc.cover's texture_state (0 when unavailable)
}
