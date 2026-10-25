#pragma once

// The mod's rows in the client's frame profiler (sxc.profiler; the Client module's "Profiler" panel):
// each step of the frame's update on its own. Nothing on a client without the service.
namespace ia::prof {
    enum Step {
        kWorld,      // the world snapshot
        kSky,        // the clock, the ephemeris, the bodies
        kClimate,
        kGround,     // the materials and the cover table
        kWind,
        kGrass,
        kRegional,
        kFields,
        kFogField,   // the fog's field, far map, wake
        kFogDraw,    // what the fog hands the client per frame
        kFogBuild,   // inside it: the field's layers built
        kCover,
        kLights,
        kStepCount
    };

    void Enter(Step step);
    void Leave(Step step);

    struct Scope {
        Step step;
        explicit Scope(Step s) : step(s) { Enter(step); }
        ~Scope() { Leave(step); }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    };
}
