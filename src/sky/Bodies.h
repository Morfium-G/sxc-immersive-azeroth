// The sky's bodies: the place's body set (SkyProfile -> SkyBodySet) drawn instead of the client's sun
// and moons, through sxc.sky (in the stock bodies' slot, or after the dome for bodies that need it).
// Where each body is comes from the ephemeris, which routes and Lua share. Also the glares, a body
// driving the world light, and hiding M2 skyboxes (SkyProfile flags and the panel).
#pragma once

#include "SXC/Api/sxc_sky.h"
#include "World.h"

namespace ia::sky::bodies {
    // Once per frame, main thread, after the sky clock: the place's profile and body set.
    void Update(const world::Snapshot& snap);

    // The provider's callbacks (sxc.sky).
    void OnLighting(sxc_sky_lighting& io);
    bool OnBodies(int pass);

    void SetEnabled(bool on); // setting / panel: off = the client's own sky (skyboxes included)
    bool Enabled();

    // Per body this frame (panel).
    struct Info {
        const char* name = "";
        bool drawn = false;
        float azimuth = 0.0f;   // compass degrees: 0 north, 90 east
        float elevation = 0.0f; // degrees above the horizon
        float sizeDegrees = 0.0f;
        float visibility = 1.0f; // 0..1: the visibility window with its ease
        bool windowOpen = true;
        bool ownDraw = false;    // drawn after the dome
        const char* status = "";
    };
    constexpr int kMaxBodies = 32;
    constexpr int kRouteSlots = 96;
    int Count();
    const Info& At(int i);
    const char* SetName();
    const char* ProfileWhere();

    // --- queries (Lua; main thread). Nothing while our bodies are off: the client's sky shows then. ---
    int BodyCount();
    const char* BodyName(int i);
    struct Now {
        float azimuth = 0.0f, elevation = 0.0f, sizeDegrees = 0.0f;
        bool visible = false; // above the horizon and in its visibility window
        float lit = 1.0f;     // 0..1, phases come later
    };
    bool Query(const char* name, Now& out);
    double MinutesUntil(const char* name, bool rise);
    struct Cycle {
        double position = 0.0;
        double untilNext = 0.0;
        double period = 0.0;
        bool lore = false;
        double realDays = -1.0;
    };
    bool CycleOf(const char* name, Cycle& out);

    // --- debug (panel; not saved) ---
    extern bool g_showRoutes;
    extern bool g_debugHideMoonGlare;
    extern bool g_glaresFollow;
    extern bool g_bodiesDriveLight;
    extern bool g_debugAllOwnDraw;
    extern bool g_hideZoneSkybox;
    extern bool g_hideAllSkyboxes;
    const char* LightSource();

    // The skybox slots as the client set them this frame (panel).
    const sxc_sky_lighting& LastLighting();
}
