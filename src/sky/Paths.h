// Where a sky body is: its path (SkyPath) evaluated at a time, and its visibility cycle with the
// arrival / leaving ease. Pure maths on the table rows, shared by the drawing, the panel and (later)
// Lua. Directions are in the world's frame: +X north, +Y west, +Z up (compass = atan2(-y, x)).
// Design (WXL): orchestration docs/r&d/immersion/sky/phase2-celestial-bodies.md.
#pragma once

#include "sky/BodyTables.h"

namespace ia::sky::paths
{
    /// The two clocks a body runs on.
    struct Times
    {
        double dayFraction = 0.0; ///< what the place shows (pinned, shifted, day length warped): daily paths
        double baseDays = 0.0;    ///< unpinned base time in days: multi-day cycles, visibility
        double loreYears = 0.0;   ///< lore time in years: bodies on the lore clock
    };

    /// Where along its path (0..1) a body is: daily bodies (period 1) follow the place's time of day,
    /// others the base time; period 0 hangs still at PhaseDays. Bodies on the lore clock (Clock 1)
    /// count PeriodDays / PhaseDays in years of lore time.
    double PathTime(const bodytables::Body& b, const Times& t);

    /// Orbit direction at path time `t` (0 = lowest point, 0.5 = highest). `northOffset` turns the map.
    void Orbit(const bodytables::Path& p, double t, float northOffset, float out[3]);

    /// Keyframe direction at path time `t`: a smooth looping curve through the keys (Catmull-Rom over
    /// their times, on the sphere); `sizeMul` gets the keys' Size along the same curve.
    void Keyframes(const std::vector<bodytables::PathKey>& keys, double t, float northOffset, float out[3], float* sizeMul);

    /// A follow path's relative keys at path time `t`: offsets from the leader (right, up, degrees),
    /// size factor and the leader's brightness along the same smooth loop; `order` steps with the key
    /// a segment starts from (so it changes only at a key: put those where the bodies are apart).
    struct Relative { float right = 0.0f, up = 0.0f, size = 1.0f, leaderBrightness = 1.0f; int order = 0; };
    Relative RelativeKeys(const std::vector<bodytables::PathKey>& keys, double t);

    /// The path's modifiers, on the base time: drift turns the whole path around the zenith, wobble
    /// swings the body up/down and sideways, the size pulse breathes. Any path type, stock included.
    void ApplyModifiers(const bodytables::Path& p, double baseDays, float dir[3], float* sizeMul);
    bool HasModifiers(const bodytables::Path& p);

    /// The visibility cycle: 1 inside the window (after the arrival ease), 0 outside (after the
    /// leaving ease), in between while easing. `open` = inside the window.
    float Visibility(const bodytables::Body& b, double baseDays, bool* open);

    /// Applies the ease to a drawn body: alpha and size factors, and (sink) lowers `dir` below the
    /// horizon. `sizeDegrees` is the body's angular diameter.
    void ApplyEase(const bodytables::Body& b, float k, float sizeDegrees, float dir[3], float* alphaMul, float* sizeMul);

    // Compass <-> world direction.
    void FromCompass(float azimuthDeg, float elevationDeg, float out[3]);
    void ToCompass(const float dir[3], float* azimuthDeg, float* elevationDeg);
}
