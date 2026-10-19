// Where a sky body is at any time: every path type (stock, orbit, keyframes, follow), its modifiers
// and its visibility ease, from one function. The drawing, the routes in the panel and Lua all ask
// here, so they always agree. Directions in the world's frame (+X north, +Y west, +Z up).
#pragma once

#include "sky/BodyTables.h"

namespace ia::sky::ephemeris
{
    /// A point in time, in the clocks the bodies run on.
    struct Moment
    {
        double baseMinutes = 0.0; ///< the base time (server or debug override)
        double dayFraction = 0.0; ///< the time of day the client's light curves see (daily paths)
        double baseDays = 0.0;    ///< base time in days on the server's date (multi-day cycles)
        double clientDays = 0.0;  ///< the client's own day counter + day fraction (the stock Blue Child)
        double loreYears = 0.0;   ///< lore time: years on the lore calendar (0 = the Dark Portal)
    };

    /// The place's lore clock (SkyProfile), set once per frame.
    struct LoreClock
    {
        int   year = 27;
        float wobbleYears = 0.0f;
        int   wobbleMode = 0;      ///< 0 around, 1 before, 2 after
        float yearsPerDay = 0.0f;  ///< 0 frozen
        int   startDate = 0;       ///< running: yyyymmdd of the realm date it's at `year`; 0 = 1 January this realm year
    };
    void SetLoreClock(const LoreClock& c);
    const LoreClock& CurrentLoreClock();
    /// Lore time at a base time, here: the lore year + the place's day of the year + the wobble; or,
    /// running, the lore year + years per day x days since the start date.
    double LoreYearsAt(double baseMinutes);
    /// The running clock's start: days since 1970 of the realm date it's at the lore year (the table's
    /// LoreStartDate, or 1 January of the realm year at `baseMinutes`); `date` gets it as yyyymmdd.
    double LoreStartDays(double baseMinutes, int* date = nullptr);

    // --- debug (panel; not saved): pin lore time to a value, or let it play ---
    bool   LoreOverridden();
    void   SetLoreOverride(bool on);
    double LoreOverrideYears();
    void   SetLoreOverrideYears(double years); ///< turns the override on
    bool   LorePlaying();
    void   SetLorePlaying(bool on);            ///< on: the override runs on from the current lore time
    float  LorePlayRate();                     ///< lore years per real second
    void   SetLorePlayRate(float yearsPerSecond);
    /// Once per frame, main thread: advances a playing override.
    void   Tick(float dt);

    /// Now, exactly as the client sees it (its day-night block).
    Moment Now();
    /// A base time, with the place's current rule (pin, offset) and day length applied.
    Moment At(double baseMinutes);

    struct Place
    {
        bool  ok = false;           ///< false: can't be placed (status says why)
        const char* status = "";
        float dir[3] = { 0.0f, 0.0f, 1.0f };
        float sizeDegrees = 0.0f;   ///< angular diameter, ease included
        float sizeCurve = 1.0f;     ///< its size now over its base size (the stock size curve, keys, pulse)
        float alpha = 1.0f;         ///< the ease's alpha factor
        float visibility = 1.0f;    ///< 0..1: the visibility window with its ease
        bool  windowOpen = true;
        double routeTime = 0.0;     ///< 0..1 along its path
        bool  stockExact = false;   ///< a stock body the row doesn't change: draw the client's own entry
        // Following with relative keys: how it affects its leader.
        uint32_t leaderId = 0;          ///< the leader's SkyBody ID (0: not following)
        float leaderBrightness = 1.0f;  ///< multiply the leader's brightness by this
        int   relativeOrder = 0;        ///< +1 drawn in front of the leader, -1 behind, 0 by Order
    };

    /// Where body `b` is at `m`. `northOffset` turns the map (SkyProfile).
    Place Where(const bodytables::Body& b, const Moment& m, float northOffset);

    /// Above the horizon and in its visibility window.
    bool Visible(const Place& p);

    /// How many days one look at its route should cover (1 for daily bodies, the cycle for longer ones, max 3).
    double RouteSpanDays(const bodytables::Body& b);

    /// Base minutes from `from` until body `b` next rises (rise = true) or sets, searched up to
    /// `withinDays` ahead; < 0 if it doesn't within that time.
    double MinutesUntil(const bodytables::Body& b, double fromBaseMinutes, float northOffset, bool rise, double withinDays);
}
