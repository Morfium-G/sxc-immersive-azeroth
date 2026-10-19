#include "sky/Clock.h"

#include <cmath>
#include "climate/Climate.h"
#include "sky/Season.h"
#include "sky/SkyTime.h"

namespace ia::sky::clock {
    namespace {
        // The client's time as the last callback gave it.
        bool g_running = false;
        double g_serverMinutes = 0.0;
        double g_shownFraction = 0.0;
        double g_shownDays = 0.0;
        long long g_dateCorrection = 0;

        // Debug override of the base time.
        bool g_override = false;
        bool g_paused = false;
        float g_speed = 1.0f;
        double g_overrideMinutes = 0.0;
        bool g_keepRule = false; // the override keeps the place's SkyTime rule on top (testing rules)

        // The place's rule, blended: the previous rule fades into the current one over its blend time.
        skytime::Rule g_from, g_to;
        float g_blend = 1.0f; // 0 = all g_from .. 1 = all g_to
        bool g_haveRule = false;
        int g_lastMap = -2;
        double g_sky = 0.0;    // this frame's answer
        bool g_answer = false; // answer the client (an override, a rule or a day length warp is active)

        long long DaysFromCivil(int y, int m, int d) { // Howard Hinnant
            y -= m <= 2 ? 1 : 0;
            const long long era = (y >= 0 ? y : y - 399) / 400;
            const unsigned yoe = static_cast<unsigned>(y - era * 400);
            const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
            const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
            return era * 146097 + static_cast<long long>(doe) - 719468;
        }

        // -720 .. 720: the shortest way around the clock.
        double Shortest(double minutes) {
            minutes = std::fmod(minutes, 1440.0);

            if (minutes > 720.0) minutes -= 1440.0;
            if (minutes <= -720.0) minutes += 1440.0;

            return minutes;
        }

        // A pin keeps the base time's own day: the day number then only steps at the base's midnight.
        // (Nearest-day pinning flipped it by a day whenever the base passed 12 h from the pin, which
        // jumped everything on a multi-day cycle -- the Blue Child's 1.7 days.)
        double Apply(const skytime::Rule& r, double base) {
            if (r.pinned) {
                return static_cast<double>(DayOf(base)) * 1440.0 + r.pinMinute;
            }

            return base + r.offsetMinutes;
        }

        bool Active(const skytime::Rule& r) { return r.pinned || r.offsetMinutes != 0.0f; }

        bool Same(const skytime::Rule& a, const skytime::Rule& b) {
            return a.pinned == b.pinned && a.pinMinute == b.pinMinute && a.offsetMinutes == b.offsetMinutes && a.pinnedDay == b.pinnedDay;
        }

        // Seasonal day length: the stock light curves are keyed to the time of day, so instead of
        // changing them the time the client reads is warped: light time = t + c sin(2 pi t). Midnight
        // and noon stay put; with c > 0 (summer) the morning runs ahead (the stock sunrise comes
        // earlier) and the evening behind (sunset later), by up to c days at 06:00 / 18:00; winter the
        // other way. Smooth, and monotonic while 2 pi |c| < 1 (c is kept far below).
        float g_dayLengthStrength = 1.0f;
        double g_warp = 0.0; // c, days

        double LightFraction(double minutes) {
            constexpr double kTwoPi = 6.283185307179586;
            const double t = DayFractionOf(minutes);
            double l = t + g_warp * std::sin(kTwoPi * t);
            l -= std::floor(l);
            return l;
        }
    }

    void OnTime(sxc_sky_time& io, bool answer) {
        g_running = true;
        g_serverMinutes = static_cast<double>(io.server_day) * 1440.0 + static_cast<double>(io.server_day_progression) * 1440.0;

        if (io.month >= 1 && io.month <= 12 && io.month_day >= 1 && io.month_day <= 31 && io.year >= 2000 && io.year < 2200) {
            g_dateCorrection = DaysFromCivil(io.year, io.month, io.month_day) - static_cast<long long>(io.server_day);
        }

        if (g_answer && answer) {
            const double light = LightFraction(g_sky);
            io.day_progression = static_cast<float>(light);
            io.time_of_day = static_cast<int32_t>(light * 1440.0) % 1440;
            io.day = static_cast<float>(DayOf(g_sky));
        }

        g_shownFraction = io.day_progression;
        g_shownDays = static_cast<double>(io.day) + io.day_progression;
    }

    double ShownDayFraction() { return g_shownFraction; }
    double ShownClientDays() { return g_shownDays; }

    double ServerMinutes() { return g_serverMinutes; }
    double BaseMinutes() { return g_override ? g_overrideMinutes : ServerMinutes(); }
    double BaseDays() { return BaseMinutes() / 1440.0 + static_cast<double>(g_dateCorrection); }
    double DateCorrectionDays() { return static_cast<double>(g_dateCorrection); }

    double SkyMinutesAt(double baseMinutes) {
        if (g_override && !g_keepRule) {
            return baseMinutes;
        }

        const double sky = Apply(g_to, baseMinutes); // a blend in progress is ignored: it ends in seconds
        return sky < 0.0 ? 0.0 : sky;
    }

    double LightTimeAt(double skyMinutes) { return LightFraction(skyMinutes); }

    int PinnedDay() { return (!g_override || g_keepRule) ? g_to.pinnedDay : 0; }

    Date PlaceDateOf(double skyMinutes) {
        Date d = DateOf(skyMinutes);
        const int pinned = PinnedDay();

        if (pinned <= 0) {
            return d;
        }

        // A pinned day counts a 365-day year (lore years have no leap day): month and day from it.
        static const int kStart[13] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 };
        d.dayOfYear = pinned - 1;
        int m = 0;

        while (m < 11 && d.dayOfYear >= kStart[m + 1]) {
            m++;
        }

        d.month = m + 1;
        d.day = d.dayOfYear - kStart[m] + 1;
        d.yearFraction = static_cast<float>((d.dayOfYear + DayFractionOf(skyMinutes)) / 365.0);
        return d;
    }

    Date PlaceDate() { return PlaceDateOf(SkyMinutes()); }
    double SkyMinutes() { return g_answer ? g_sky : BaseMinutes(); }

    void Update(float dt, const world::Snapshot& snap) {
        if (g_override && !g_paused && dt > 0.0f) {
            g_overrideMinutes += static_cast<double>(dt) / 60.0 * static_cast<double>(g_speed); // x1 = real time

            if (g_overrideMinutes < 0.0) {
                g_overrideMinutes = 0.0;
            }
        }

        // The place's rule: a new one blends in from where the sky is; a new map (teleport, loading
        // screen) or the first frame takes it at once. Only area (zone / sub-area) rows ease in: a
        // map's or the global rule is the place's time from the moment you arrive (an eased map rule
        // sent the sun racing across the sky at login).
        static bool s_wasInWorld = false;
        const skytime::Rule& here = skytime::Here(snap);
        const bool arrived = snap.inWorld && !s_wasInWorld;
        s_wasInWorld = snap.inWorld;
        constexpr int kAreaScope = 2;

        if (!g_haveRule || arrived || snap.mapId != g_lastMap) {
            g_from = g_to = here;
            g_blend = 1.0f;
            g_haveRule = true;
        } else if (!Same(here, g_to)) {
            const bool areaChange = here.scopeType == kAreaScope || g_to.scopeType == kAreaScope;
            g_from = g_to;
            g_to = here;
            g_blend = areaChange && here.blendSeconds > 0.0f ? 0.0f : 1.0f;
        }

        g_lastMap = snap.mapId;

        if (g_blend < 1.0f && dt > 0.0f) {
            g_blend += dt / (g_to.blendSeconds > 0.01f ? g_to.blendSeconds : 0.01f);

            if (g_blend > 1.0f) {
                g_blend = 1.0f;
            }
        }

        const double base = BaseMinutes();

        if (g_override && !g_keepRule) {
            g_sky = base; // the debug override shows exactly its time: the place's rule is ignored
        } else {
            const double a = Apply(g_from, base), b = Apply(g_to, base);
            const double s = g_blend * g_blend * (3.0 - 2.0 * g_blend); // smoothstep
            g_sky = a + Shortest(b - a) * s;
        }

        if (g_sky < 0.0) {
            g_sky = 0.0;
        }

        // The day length for the season where the player is (the climate's season curve and its
        // DayLength column, blended across borders).
        g_warp = 0.0;

        if (snap.inWorld && g_dayLengthStrength > 0.0f) {
            const season::Season s = season::Here(PlaceDateOf(g_sky).yearFraction);
            const double c = static_cast<double>(g_dayLengthStrength) * climate::Here().dayLength / 24.0 * s.warmth;
            constexpr double kMax = 0.12; // ~2.9 h; monotonic up to 1 / 2 pi = 0.159
            g_warp = c > kMax ? kMax : (c < -kMax ? -kMax : c);
        }

        g_answer = g_running && (g_override || g_warp != 0.0 || Active(g_to) || (g_blend < 1.0f && Active(g_from)));
    }

    long long DaysSince1970(int year, int month, int day) { return DaysFromCivil(year, month, day); }

    Date DateOf(double minutes) {
        // Days since 1970 -> civil date (Howard Hinnant's days_from_civil, inverted).
        Date d;
        const long long days = static_cast<long long>(std::floor(minutes / 1440.0)) + g_dateCorrection;
        const long long z = days + 719468;
        const long long era = (z >= 0 ? z : z - 146096) / 146097;
        const unsigned doe = static_cast<unsigned>(z - era * 146097);
        const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        const unsigned mp = (5 * doy + 2) / 153;
        d.day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
        d.month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
        d.year = static_cast<int>(yoe + era * 400 + (d.month <= 2 ? 1 : 0));
        static const int kStart[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
        const bool leap = (d.year % 4 == 0 && d.year % 100 != 0) || d.year % 400 == 0;
        d.dayOfYear = kStart[d.month - 1] + d.day - 1 + (leap && d.month > 2 ? 1 : 0);
        const double frac = DayFractionOf(minutes);
        d.yearFraction = static_cast<float>((d.dayOfYear + frac) / (leap ? 366.0 : 365.0));
        const int m = static_cast<int>(frac * 1440.0) % 1440;
        d.hour = m / 60;
        d.minute = m % 60;
        return d;
    }

    bool RuleActive() { return (!g_override || g_keepRule) && (Active(g_to) || (g_blend < 1.0f && Active(g_from))); }
    float DayLengthStrength() { return g_dayLengthStrength; }
    void SetDayLengthStrength(float s) { g_dayLengthStrength = s < 0.0f ? 0.0f : (s > 2.0f ? 2.0f : s); }
    float DayLengthShiftHours() { return static_cast<float>(g_warp * 24.0); }
    double LightTime() { return LightFraction(SkyMinutes()); }
    bool KeepRule() { return g_keepRule; }
    void SetKeepRule(bool keep) { g_keepRule = keep; }
    float ShiftMinutes() { return static_cast<float>(SkyMinutes() - BaseMinutes()); }
    bool Overridden() { return g_override; }

    void SetOverride(bool on) {
        if (on && !g_override) {
            g_overrideMinutes = ServerMinutes();
        }

        g_override = on && g_running;
    }

    void SetBaseMinutes(double minutes) {
        SetOverride(true);
        g_overrideMinutes = minutes < 0.0 ? 0.0 : minutes;
    }

    bool Paused() { return g_paused; }
    void SetPaused(bool paused) { g_paused = paused; }
    float Speed() { return g_speed; }
    void SetSpeed(float speed) { g_speed = speed < 0.0f ? 0.0f : speed; }
    bool Running() { return g_running; }
}
