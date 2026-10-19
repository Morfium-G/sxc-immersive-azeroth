#include "sky/Ephemeris.h"

#include "sky/Clock.h"
#include "sky/Paths.h"
#include "sky/SkyApi.h"

#include <cmath>
#include <cstdint>

namespace ia::sky::ephemeris
{
    namespace
    {
        constexpr double kPi = 3.14159265358979323846;
        constexpr float  kDeg = static_cast<float>(kPi / 180.0);
        constexpr float  kDistance = SXC_SKY_DISTANCE; // SetPlanets: bodies sit 12 units from the camera
        constexpr float  kDefaultScale = 1.0f;          // a custom body without Size: the stock sun's 4.77 deg

        // The stock curves, as DayNight::SetPlanets has them (WoW/Source/WorldClient/DayNight.cpp):
        // looping {time, value} keys. Angles in radians: theta from the zenith, phi the client's azimuth.
        struct Key { float t, v; };
        constexpr Key kSunTheta[5]   = { {0.22916667f, 1.7453293f}, {0.49652779f, 0.087266468f}, {0.5f, 0.087266468f}, {0.50347221f, 0.087266468f}, {0.89583331f, 1.7453293f} };
        constexpr Key kSunPhi[3]     = { {0.22916667f, 0.78539819f}, {0.5f, 0.78539819f}, {0.89583331f, 0.78539819f} };
        constexpr Key kMoonTheta[5]  = { {0.0f, 0.61086524f}, {0.0034722222f, 0.61086524f}, {0.16666667f, 1.7453293f}, {0.91666669f, 1.7453293f}, {0.99652779f, 0.61086524f} };
        constexpr Key kMoonPhi[3]    = { {0.0f, 0.78539819f}, {0.16666667f, 0.78539819f}, {0.91666669f, 0.78539819f} };
        constexpr Key kMoon2Theta[5] = { {0.0f, 0.61086524f}, {0.0034722222f, 0.61086524f}, {0.16666667f, 1.7453293f}, {0.91666669f, 1.7453293f}, {0.99652779f, 0.61086524f} };
        constexpr Key kMoon2Phi[3]   = { {0.0f, 2.3561945f}, {0.16666667f, 2.6179938f}, {0.91666669f, 2.8797934f} };
        constexpr Key kSunScale[4]   = { {0.25f, 2.0f}, {0.28125f, 1.0f}, {0.84375f, 1.0f}, {0.875f, 2.0f} };
        constexpr Key kMoonScale[4]  = { {0.041666672f, 1.0f}, {0.16666667f, 1.5f}, {0.91666669f, 1.5f}, {0.99930561f, 1.0f} };

        struct Table { const Key* keys; uint32_t count; };
        constexpr Table kTSunTheta   = { kSunTheta, 5 };
        constexpr Table kTSunPhi     = { kSunPhi, 3 };
        constexpr Table kTMoonTheta  = { kMoonTheta, 5 };
        constexpr Table kTMoonPhi    = { kMoonPhi, 3 };
        constexpr Table kTMoon2Theta = { kMoon2Theta, 5 };
        constexpr Table kTMoon2Phi   = { kMoon2Phi, 3 };
        constexpr Table kTSunScale   = { kSunScale, 4 };
        constexpr Table kTMoonScale  = { kMoonScale, 4 };

        // DayNight::InterpTable: t clamped to 0..1, linear between looping keys.
        float Interp(const Table& tab, float t)
        {
            const Key* keys = tab.keys;
            const uint32_t count = tab.count;
            const float value = t >= 0.0f ? (t < 1.0f ? t : 1.0f) : 0.0f;
            uint32_t index = 0;
            while (index < count && value > keys[index].t) ++index;
            uint32_t prev;
            if (index == count) { index = 0; prev = count - 1; }
            else prev = index ? index - 1 : count - 1;
            float range = keys[index].t - keys[prev].t;
            if (std::fabs(range) < 0.001f) return keys[prev].v;
            if (range < 0.0f) range += 1.0f;
            float offset = value - keys[prev].t;
            if (offset < 0.0f) offset += 1.0f;
            const float f = offset / range;
            return keys[prev].v + f * (keys[index].v - keys[prev].v);
        }

        void Polar(float theta, float phi, float out[3])
        {
            out[0] = std::cos(phi) * std::sin(theta);
            out[1] = std::sin(theta) * std::sin(phi);
            out[2] = std::cos(theta);
        }

        double Frac(double x) { return x - std::floor(x); }

        // The stock body's base size this frame (the client's DNPlanet base scale).
        float BaseScale(sxc_sky_role role)
        {
            const float s = Stock(role).base_scale;
            return s > 0.0f ? s : 1.0f;
        }

        float ScaleToDegrees(float scale) { return 2.0f * std::atan(0.5f * scale / kDistance) / kDeg; }

        // A stock body's direction and its size curve factor (x base scale = the entry's size).
        bool StockPlace(bodytables::StockRole role, const Moment& m, float dir[3], float* curve, double* routeTime, float* baseScale)
        {
            const float t = static_cast<float>(m.dayFraction);
            switch (role)
            {
            case bodytables::StockRole::Sun:
                Polar(Interp(kTSunTheta, t), Interp(kTSunPhi, t), dir);
                *curve = Interp(kTSunScale, t);
                *routeTime = t;
                *baseScale = BaseScale(SXC_SKY_SUN);
                return true;
            case bodytables::StockRole::WhiteLady:
                Polar(Interp(kTMoonTheta, t), Interp(kTMoonPhi, t), dir);
                *curve = Interp(kTMoonScale, t);
                *routeTime = t;
                *baseScale = BaseScale(SXC_SKY_WHITE_LADY);
                return true;
            case bodytables::StockRole::BlueChild:
            {
                float period = Stock(SXC_SKY_BLUE_CHILD).period_days;
                if (!(period > 0.0f)) period = 1.7f;
                const float c = static_cast<float>(Frac(m.clientDays / period));
                Polar(Interp(kTMoon2Theta, c), Interp(kTMoon2Phi, c), dir);
                *curve = Interp(kTMoonScale, c);
                *routeTime = c;
                *baseScale = BaseScale(SXC_SKY_BLUE_CHILD);
                return true;
            }
            default:
                return false;
            }
        }

        Place WhereDepth(const bodytables::Body& b, const Moment& m, float north, int depth);

        // The sky's own frame at a direction L, as someone looking at it sees it: `up` toward the
        // zenith, `right` to their right (facing north: east). Never flips as L moves, unlike the
        // leader's direction of travel (the stock White Lady turns 180 degrees at her highest point).
        void SkyFrame(const float L[3], float up[3], float right[3])
        {
            right[0] = L[1]; right[1] = -L[0]; right[2] = 0.0f; // L x zenith
            float len = std::sqrt(right[0] * right[0] + right[1] * right[1]);
            if (len < 1e-5f) { right[0] = 0.0f; right[1] = -1.0f; len = 1.0f; } // at the zenith: east
            right[0] /= len; right[1] /= len;
            up[0] = right[1] * L[2] - right[2] * L[1]; // right x L
            up[1] = right[2] * L[0] - right[0] * L[2];
            up[2] = right[0] * L[1] - right[1] * L[0];
        }

        // Returns the size to use for the follower when its relative keys size it from the leader (< 0: not).
        float Follow(const bodytables::Body& b, const bodytables::Path& p, const Moment& m, float north, int depth, Place& out)
        {
            const bodytables::Body* leader = bodytables::FindBody(p.followBody);
            if (!leader || depth > 3) { out.status = !leader ? "follow: FollowBody not found" : "follow: chain too long"; return -1.0f; }
            const Place now = WhereDepth(*leader, m, north, depth + 1);
            if (!now.ok) { out.status = "follow: its leader can't be placed"; return -1.0f; }

            const float* L = now.dir;
            float up[3], right[3];
            SkyFrame(L, up, right);
            out.leaderId = leader->id;

            // Relative keys: the follower's offset, size, front/behind and the leader's brightness over
            // its own cycle (the lore moons: the Blue Child around the White Lady).
            const std::vector<bodytables::PathKey>& keys = bodytables::PathKeys(p.id);
            if (!keys.empty())
            {
                out.routeTime = paths::PathTime(b, { m.dayFraction, m.baseDays, m.loreYears });
                const paths::Relative r = paths::RelativeKeys(keys, out.routeTime);
                // Offsets are degrees at the leader's base size and grow / shrink with it (the stock
                // White Lady is x1.5 near the horizon, x1 at her highest), so the pair keeps its look.
                const float sx = r.right * now.sizeCurve, sy = r.up * now.sizeCurve;
                float o[3];
                for (int c = 0; c < 3; ++c) o[c] = sx * right[c] + sy * up[c];
                const float d = std::sqrt(sx * sx + sy * sy) * kDeg;
                const float len = std::sqrt(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]);
                for (int c = 0; c < 3; ++c) out.dir[c] = len > 1e-6f ? std::cos(d) * L[c] + std::sin(d) * o[c] / len : L[c];
                out.leaderBrightness = r.leaderBrightness;
                out.relativeOrder = r.order;
                out.ok = true;
                return (b.size > 0.0f ? b.size : now.sizeDegrees) * r.size;
            }

            float sep = p.separationMin;
            if (p.separationDays > 0.0f)
                sep += (p.separationMax - p.separationMin) * static_cast<float>(0.5 - 0.5 * std::cos(2.0 * kPi * Frac(m.baseDays / p.separationDays)));
            // SeparationAngle: 0 below the leader, 90 to its right, 180 above, 270 to its left.
            const float a = p.separationAngle * kDeg, s = sep * kDeg;
            for (int c = 0; c < 3; ++c)
            {
                const float o = -std::cos(a) * up[c] + std::sin(a) * right[c];
                out.dir[c] = std::cos(s) * L[c] + std::sin(s) * o;
            }
            out.routeTime = now.routeTime;
            out.ok = true;
            return -1.0f;
        }

        Place WhereDepth(const bodytables::Body& b, const Moment& m, float north, int depth)
        {
            Place out;
            out.visibility = paths::Visibility(b, m.baseDays, &out.windowOpen);

            const bodytables::Path* path = bodytables::FindPath(b.pathId);
            const bodytables::PathType type = path ? path->type : bodytables::PathType::Stock;
            float pathSize = 1.0f;
            float curve = 1.0f, baseScale = kDefaultScale;
            float relativeSize = -1.0f; // a follower sized by its relative keys
            bool stockRole = b.role != bodytables::StockRole::None;

            switch (type)
            {
            case bodytables::PathType::Stock:
                if (!StockPlace(b.role, m, out.dir, &curve, &out.routeTime, &baseScale)) { out.status = "the stock path needs a StockRole"; return out; }
                out.ok = true;
                break;
            case bodytables::PathType::Orbit:
                out.routeTime = paths::PathTime(b, { m.dayFraction, m.baseDays, m.loreYears });
                paths::Orbit(*path, out.routeTime, north, out.dir);
                out.ok = true;
                break;
            case bodytables::PathType::Keyframes:
                if (bodytables::PathKeys(path->id).empty()) { out.status = "keyframe path without SkyPathKey rows"; return out; }
                out.routeTime = paths::PathTime(b, { m.dayFraction, m.baseDays, m.loreYears });
                paths::Keyframes(bodytables::PathKeys(path->id), out.routeTime, north, out.dir, &pathSize);
                out.ok = true;
                break;
            case bodytables::PathType::Follow:
                relativeSize = Follow(b, *path, m, north, depth, out);
                if (!out.ok) return out;
                break;
            default:
                out.status = "unknown path type";
                return out;
            }

            // A stock role off its stock path keeps its stock size (no curve).
            if (stockRole && type != bodytables::PathType::Stock)
            {
                float unused[3]; double rt;
                StockPlace(b.role, m, unused, &curve, &rt, &baseScale);
                curve = 1.0f;
            }
            const bool modified = path && paths::HasModifiers(*path);
            if (modified) paths::ApplyModifiers(*path, m.baseDays, out.dir, &pathSize);

            float size = relativeSize >= 0.0f ? relativeSize : (b.size > 0.0f ? b.size * curve : ScaleToDegrees(curve * baseScale));
            size *= pathSize;
            out.sizeCurve = (relativeSize >= 0.0f ? 1.0f : curve) * pathSize;
            float sizeMul = 1.0f;
            paths::ApplyEase(b, out.visibility, size, out.dir, &out.alpha, &sizeMul);
            out.sizeDegrees = size * sizeMul;
            out.stockExact = type == bodytables::PathType::Stock && !modified && b.size <= 0.0f && out.visibility >= 1.0f;
            float az, el;
            paths::ToCompass(out.dir, &az, &el);
            out.status = out.visibility <= 0.0f ? "not in its visibility window" : (el > 0.0f ? "up" : "below the horizon");
            return out;
        }
    }

    namespace
    {
        LoreClock g_lore;
        bool      g_loreOverride = false;
        double    g_loreOverrideYears = 27.0;
        bool      g_lorePlay = false;
        float     g_lorePlayRate = 10.0f;
    }

    void SetLoreClock(const LoreClock& c) { g_lore = c; }
    const LoreClock& CurrentLoreClock() { return g_lore; }
    bool   LoreOverridden() { return g_loreOverride; }
    void   SetLoreOverride(bool on)
    {
        if (on && !g_loreOverride) g_loreOverrideYears = LoreYearsAt(clock::BaseMinutes());
        g_loreOverride = on;
        if (!on) g_lorePlay = false;
    }
    double LoreOverrideYears() { return g_loreOverrideYears; }
    void   SetLoreOverrideYears(double years) { g_loreOverrideYears = years; g_loreOverride = true; }
    bool   LorePlaying() { return g_lorePlay; }
    void   SetLorePlaying(bool on) { if (on) SetLoreOverride(true); g_lorePlay = on; }
    float  LorePlayRate() { return g_lorePlayRate; }
    void   SetLorePlayRate(float yearsPerSecond) { g_lorePlayRate = yearsPerSecond; }

    void Tick(float dt)
    {
        if (g_lorePlay && g_loreOverride && dt > 0.0f && dt < 1.0f) // a long hitch (loading screen) doesn't jump
            g_loreOverrideYears += static_cast<double>(dt) * g_lorePlayRate;
    }

    double LoreStartDays(double baseMinutes, int* date)
    {
        const int v = g_lore.startDate;
        int y = v / 10000, m = (v / 100) % 100, d = v % 100;
        if (v <= 0 || m < 1 || m > 12 || d < 1 || d > 31) { y = clock::DateOf(baseMinutes).year; m = 1; d = 1; }
        if (date) *date = y * 10000 + m * 100 + d;
        return static_cast<double>(clock::DaysSince1970(y, m, d));
    }

    double LoreYearsAt(double baseMinutes)
    {
        if (g_loreOverride) return g_loreOverrideYears;
        const double year = static_cast<double>(g_lore.year);
        // Running: continuous from the start date (at the lore year then), on the server's date.
        if (g_lore.yearsPerDay > 0.0f)
            return year + (baseMinutes / 1440.0 + clock::DateCorrectionDays() - LoreStartDays(baseMinutes)) * g_lore.yearsPerDay;
        // Frozen: the lore year, the place's day of the year (pinned or the realm's), and the wobble:
        // a smooth swing over the place's year, back where it started at New Year.
        const double yf = clock::PlaceDateOf(clock::SkyMinutesAt(baseMinutes)).yearFraction;
        const double a = g_lore.wobbleYears, w = 2.0 * kPi * yf;
        double wobble = 0.0;
        switch (g_lore.wobbleMode)
        {
        case 1:  wobble = -a * 0.5 * (1.0 - std::cos(w)); break; // before: dips to -a at midyear
        case 2:  wobble = a * 0.5 * (1.0 - std::cos(w)); break;  // after: rises to +a
        default: wobble = a * std::sin(w); break;                // around: -a .. +a
        }
        return year + yf + wobble;
    }

    Moment Now()
    {
        Moment m;
        m.baseMinutes = clock::BaseMinutes();
        m.baseDays = clock::BaseDays();
        m.dayFraction = clock::ShownDayFraction();
        m.clientDays = clock::ShownClientDays();
        m.loreYears = LoreYearsAt(m.baseMinutes);
        return m;
    }

    Moment At(double baseMinutes)
    {
        Moment m;
        m.baseMinutes = baseMinutes;
        m.baseDays = baseMinutes / 1440.0 + clock::DateCorrectionDays();
        const double sky = clock::SkyMinutesAt(baseMinutes);
        m.dayFraction = clock::LightTimeAt(sky);
        m.clientDays = std::floor(sky / 1440.0) + m.dayFraction;
        m.loreYears = LoreYearsAt(baseMinutes);
        return m;
    }

    Place Where(const bodytables::Body& b, const Moment& m, float northOffset) { return WhereDepth(b, m, northOffset, 0); }

    bool Visible(const Place& p) { return p.ok && p.visibility > 0.0f && p.dir[2] > 0.0f; }

    double RouteSpanDays(const bodytables::Body& b)
    {
        const bodytables::Path* path = bodytables::FindPath(b.pathId);
        double days = 1.0;
        if (b.clock == 1) return days; // lore time hardly moves within days: its route is a single spot
        if ((!path || path->type == bodytables::PathType::Stock) && b.role == bodytables::StockRole::BlueChild) days = 1.7;
        else if (path && path->type != bodytables::PathType::Stock && b.periodDays > 1.0f) days = b.periodDays;
        return days > 3.0 ? 3.0 : days;
    }

    double MinutesUntil(const bodytables::Body& b, double from, float north, bool rise, double withinDays)
    {
        // Coarse 10-minute steps for the change (a body must stay up or down longer than that to be
        // seen: fine for anything slower than a few trips an hour), then halve down to ~1 second.
        constexpr double kStep = 10.0;
        const auto vis = [&](double t) { return Visible(Where(b, At(t), north)); };
        bool prev = vis(from);
        const double end = from + withinDays * 1440.0;
        for (double t = from + kStep; t <= end; t += kStep)
        {
            const bool now = vis(t);
            if (now != prev && now == rise)
            {
                double lo = t - kStep, hi = t;
                for (int i = 0; i < 10; ++i)
                {
                    const double mid = 0.5 * (lo + hi);
                    (vis(mid) == rise ? hi : lo) = mid;
                }
                return hi - from;
            }
            prev = now;
        }
        return -1.0;
    }
}
