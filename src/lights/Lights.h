// Point lights as the client draws them (sxc.lights: per pixel in our terrain shader for now, Vulkan
// only). The client gathers the models' own lights (torches, braziers), merges close ones, lets them
// flicker and lists them on a world grid; this side sets how. WXL's env/Lights and features/TerrainLights
// (port plan, step 6a); the tables and the editor come in 6c.
#pragma once

#include "SXC/Api/sxc_lights.h"
#include "World.h"

namespace ia::lights {
    // The settings (the panel; not saved). WXL's defaults.
    struct Settings {
        bool enabled = true;
        bool stockOff = true;        // the client's own 3 per terrain chunk off while ours are on
        int debug = 0;               // 0 off, 1 our lights only, 2 the cell's count
        float radius = 16.0f;        // yd: a light is exactly 0 here
        float brightness = 1.0f;     // x the lights' colours
        float range = 256.0f;        // yd from the camera, fading over the last third
        float cellSize = 16.0f;      // yd per grid cell
        int maxPerCell = 16;         // lights a cell holds (the strongest at its middle)
        float mergeDistance = 1.0f;  // yd: closer lights merge
        float flicker = 0.15f;       // 0..1 how much a flame dips
        float flickerSpeed = 1.0f;   // x the default pace
        bool separateIndoor = true;  // a light inside an indoor WMO group lights only indoor surfaces, one outside only outdoor ones
        bool shadows = true;         // the nearest still lights get a cube shadow map of the WMOs and the terrain
        int shadowCount = 32;        // lights with a map at most (the nearest)
        int shadowUpdates = 4;       // maps drawn per frame at most
    };
    Settings& Config();

    // Once per frame: the settings to the client.
    void Update(const world::Snapshot& snap);

    // The mod is off or the world was left: the client stops drawing our lights.
    void Stop();

    bool Available();      // the client draws our lights
    const char* Status();  // the client's line
    const char* TableStatus(); // the two tables as last sent
    bool Stats(sxc_lights_stats& out); // the client's numbers for last frame

    // The service with the light editor's queries (models near, their points, last frame's lights,
    // rows missing their attachment); null on an older client.
    const sxc_lights_api* EditorApi();
}
