// The sky clock: the one time the sky, the lighting and everything else in the world's look runs on.
//
// Base time: the server's game time, or the debug override (scrub, pause, speed up). On top, the
// place's rule from SkyTime.cdbc (pinned or shifted time), blended at borders, and the seasonal day
// length warp. The client hands its time to sxc.sky's provider every frame (OnTime) and the sky,
// lights, fog colours, M2 skyboxes, day/night ambience and day/night spells take what we answer. The
// on-screen clock, GetGameTime and the calendar stay on server time.
#pragma once

#include "SXC/Api/sxc_sky.h"
#include "World.h"

namespace ia::sky::clock {
    // The provider's time callback: notes the client's time and, with `answer`, answers the sky's.
    void OnTime(sxc_sky_time& io, bool answer);

    // Once per frame, main thread, after the world snapshot: advances the override, applies the
    // place's rule and the day length.
    void Update(float dt, const world::Snapshot& snap);

    // Time as minutes since day 0 (1 Jan 1970, the client's own day counter).
    double ServerMinutes(); // the server's game time, always
    double BaseMinutes();   // the server's, or the debug override's
    double SkyMinutes();    // what the world shows: the base with the place's rule
    // The base time in days since 1970-01-01 of the server's date. The client's own day counter is
    // local midnight based and a day behind east of UTC; this one is the same on every client, so
    // multi-day cycles (visibility, long paths) count on it.
    double BaseDays();
    // Days to add to the client's day counter to get the server's date (0 west of UTC).
    double DateCorrectionDays();

    // --- the same mapping at another time (predictions: Lua rise/set, routes) ---
    double SkyMinutesAt(double baseMinutes);
    double LightTimeAt(double skyMinutes);

    inline int DayOf(double minutes) { return static_cast<int>(minutes / 1440.0); }
    inline double DayFractionOf(double minutes) { const double d = minutes / 1440.0; return d - static_cast<double>(static_cast<long long>(d)); }

    struct Date {
        int year = 1970, month = 1, day = 1; // month 1..12, day 1..31
        int dayOfYear = 0;                   // 0-based
        float yearFraction = 0.0f;           // 0 = 1 January .. 1
        int hour = 0, minute = 0;
    };
    Date DateOf(double minutes);
    long long DaysSince1970(int year, int month, int day);

    // The place's date: the realm date, or the day of the year SkyTime.cdbc pins here.
    Date PlaceDate();
    Date PlaceDateOf(double skyMinutes);
    int PinnedDay(); // 1..365 when this place's date is pinned (and no debug override ignores it), else 0

    // --- seasonal day length ---
    float DayLengthStrength(); // x the places' DayLength (AreaClimate)
    void SetDayLengthStrength(float strength);
    float DayLengthShiftHours();
    // The time of day the client's light curves see now (0..1), after the day length warp.
    double LightTime();

    // --- the place's rule (panel) ---
    bool RuleActive();
    float ShiftMinutes(); // sky - base, now (blends included)

    // --- debug override (panel; not saved) ---
    bool Overridden();
    void SetOverride(bool on); // on: starts from the server's time
    void SetBaseMinutes(double minutes);
    bool KeepRule();
    void SetKeepRule(bool keep);
    bool Paused();
    void SetPaused(bool paused);
    float Speed();
    void SetSpeed(float speed);
    // The client's time arrives (the sky provider runs).
    bool Running();

    // What the client's sky shows this frame: the day fraction its light curves read, and its day
    // counter + that fraction (the stock Blue Child's clock).
    double ShownDayFraction();
    double ShownClientDays();
}
