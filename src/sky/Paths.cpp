#include "sky/Paths.h"

#include <cmath>

namespace ia::sky::paths
{
    namespace
    {
        constexpr double kPi = 3.14159265358979323846;
        constexpr float  kDeg = static_cast<float>(kPi / 180.0);

        double Frac(double x) { return x - std::floor(x); }

        void Normalize(float v[3])
        {
            const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            if (l > 1e-6f) { v[0] /= l; v[1] /= l; v[2] /= l; }
        }
    }

    void FromCompass(float azimuthDeg, float elevationDeg, float out[3])
    {
        const float az = azimuthDeg * kDeg, el = elevationDeg * kDeg;
        out[0] = std::cos(el) * std::cos(az);
        out[1] = -std::cos(el) * std::sin(az);
        out[2] = std::sin(el);
    }

    void ToCompass(const float dir[3], float* azimuthDeg, float* elevationDeg)
    {
        const float z = dir[2] < -1.0f ? -1.0f : (dir[2] > 1.0f ? 1.0f : dir[2]);
        *elevationDeg = std::asin(z) / kDeg;
        const float az = std::atan2(-dir[1], dir[0]) / kDeg;
        *azimuthDeg = az < 0.0f ? az + 360.0f : az;
    }

    double PathTime(const bodytables::Body& b, const Times& t)
    {
        if (b.periodDays == 0.0f) return Frac(b.phaseDays);
        if (b.clock == 1) return Frac((t.loreYears + b.phaseDays) / b.periodDays);
        if (std::fabs(b.periodDays - 1.0f) < 1e-4f) return Frac(t.dayFraction + b.phaseDays);
        return Frac((t.baseDays + b.phaseDays) / b.periodDays);
    }

    void Orbit(const bodytables::Path& p, double t, float northOffset, float out[3])
    {
        // A circle on the sky, around the axis n at angular radius r. Unlifted (r = 90) it's a great
        // circle rising at RiseAzimuth, peaking at Height over RiseAzimuth + 90 and setting opposite
        // where it rose: n sits at RiseAzimuth - 90, Height down from the zenith. Lift shrinks the
        // circle around n, which raises every point of it by Lift along the meridian through n.
        const float rise = p.riseAzimuth + northOffset;
        const float lift = p.lift < -89.0f ? -89.0f : (p.lift > 89.0f ? 89.0f : p.lift);
        const float r = (90.0f - lift) * kDeg;

        float n[3], u[3], v[3];
        FromCompass(rise - 90.0f, 90.0f - p.height, n);
        if (std::fabs(p.height) < 0.01f) FromCompass(rise + 90.0f, 0.0f, u);
        else
        {
            const float zn = n[2];
            u[0] = -zn * n[0]; u[1] = -zn * n[1]; u[2] = 1.0f - zn * n[2];
            Normalize(u);
            if (p.height < 0.0f) { u[0] = -u[0]; u[1] = -u[1]; u[2] = -u[2]; }
        }
        FromCompass(rise, 0.0f, v);
        v[0] = -v[0]; v[1] = -v[1]; v[2] = -v[2];

        const float th = static_cast<float>(2.0 * kPi * (t - 0.5));
        const float cr = std::cos(r), sr = std::sin(r), ct = std::cos(th), st = std::sin(th);
        for (int i = 0; i < 3; ++i) out[i] = cr * n[i] + sr * (ct * u[i] + st * v[i]);
        Normalize(out);
    }

    void Keyframes(const std::vector<bodytables::PathKey>& keys, double t, float northOffset, float out[3], float* sizeMul)
    {
        *sizeMul = 1.0f;
        const int n = static_cast<int>(keys.size());
        if (n == 0) { FromCompass(northOffset, 0.0f, out); return; }
        if (n == 1) { FromCompass(keys[0].azimuth + northOffset, keys[0].elevation, out); *sizeMul = keys[0].size; return; }

        // Segment i -> i+1 containing t (looping: the last key runs on to the first).
        int i = n - 1;
        for (int k = 0; k < n; ++k) if (keys[k].time <= t) i = k;
        const auto at = [&](int k) -> const bodytables::PathKey& { return keys[(k % n + n) % n]; };
        // Time from key a to key b going forward (a full loop if they're the same key).
        const auto span = [&](int a, int b) {
            double d = at(b).time - at(a).time;
            while (d <= 0.0) d += 1.0;
            return d;
        };
        const double h = span(i, i + 1);
        double s = t - keys[i].time;
        if (s < 0.0) s += 1.0;
        s = h > 1e-6 ? s / h : 0.0;

        float p[4][3];
        float z[4];
        for (int k = 0; k < 4; ++k)
        {
            const bodytables::PathKey& key = at(i - 1 + k);
            FromCompass(key.azimuth + northOffset, key.elevation, p[k]);
            z[k] = key.size;
        }
        // Catmull-Rom tangents over the keys' real times, scaled to this segment (cubic Hermite).
        const double t0 = span(i - 1, i + 1), t1 = span(i, i + 2);
        const double m1 = h / t0, m2 = h / t1;
        const double s2 = s * s, s3 = s2 * s;
        const double h00 = 2 * s3 - 3 * s2 + 1, h10 = s3 - 2 * s2 + s, h01 = -2 * s3 + 3 * s2, h11 = s3 - s2;
        for (int c = 0; c < 3; ++c)
            out[c] = static_cast<float>(h00 * p[1][c] + h10 * m1 * (p[2][c] - p[0][c]) + h01 * p[2][c] + h11 * m2 * (p[3][c] - p[1][c]));
        Normalize(out);
        const double size = h00 * z[1] + h10 * m1 * (z[2] - z[0]) + h01 * z[2] + h11 * m2 * (z[3] - z[1]);
        *sizeMul = size > 0.0 ? static_cast<float>(size) : 0.0f;
    }

    Relative RelativeKeys(const std::vector<bodytables::PathKey>& keys, double t)
    {
        Relative r;
        const int n = static_cast<int>(keys.size());
        if (n == 0) return r;
        const auto pick = [](const bodytables::PathKey& k, int c) {
            return c == 0 ? k.azimuth : c == 1 ? k.elevation : c == 2 ? k.size : k.leaderBrightness;
        };
        if (n == 1)
        {
            r.right = keys[0].azimuth; r.up = keys[0].elevation; r.size = keys[0].size;
            r.leaderBrightness = keys[0].leaderBrightness; r.order = keys[0].order;
            return r;
        }
        int i = n - 1;
        for (int k = 0; k < n; ++k) if (keys[k].time <= t) i = k;
        const auto at = [&](int k) -> const bodytables::PathKey& { return keys[(k % n + n) % n]; };
        const auto span = [&](int a, int b) {
            double d = at(b).time - at(a).time;
            while (d <= 0.0) d += 1.0;
            return d;
        };
        const double h = span(i, i + 1);
        double s = t - keys[i].time;
        if (s < 0.0) s += 1.0;
        s = h > 1e-6 ? s / h : 0.0;
        const double m1 = h / span(i - 1, i + 1), m2 = h / span(i, i + 2);
        const double s2 = s * s, s3 = s2 * s;
        const double h00 = 2 * s3 - 3 * s2 + 1, h10 = s3 - 2 * s2 + s, h01 = -2 * s3 + 3 * s2, h11 = s3 - s2;
        float out[4];
        for (int c = 0; c < 4; ++c)
        {
            const double p0 = pick(at(i - 1), c), p1 = pick(at(i), c), p2 = pick(at(i + 1), c), p3 = pick(at(i + 2), c);
            out[c] = static_cast<float>(h00 * p1 + h10 * m1 * (p2 - p0) + h01 * p2 + h11 * m2 * (p3 - p1));
        }
        r.right = out[0]; r.up = out[1];
        r.size = out[2] > 0.0f ? out[2] : 0.0f;
        r.leaderBrightness = out[3] < 0.0f ? 0.0f : (out[3] > 1.0f ? 1.0f : out[3]);
        r.order = keys[i].order;
        return r;
    }

    bool HasModifiers(const bodytables::Path& p)
    {
        return p.driftDegreesPerDay != 0.0f || (p.wobbleDegrees != 0.0f && p.wobbleDays > 0.0f)
            || (p.sizePulse != 0.0f && p.sizePulseDays > 0.0f);
    }

    void ApplyModifiers(const bodytables::Path& p, double baseDays, float dir[3], float* sizeMul)
    {
        if (p.wobbleDegrees != 0.0f && p.wobbleDays > 0.0f)
        {
            // Up/down on the wobble's period, sideways on an unrelated one (x golden ratio), so the
            // body traces a slowly changing loop instead of a line.
            const double tau = baseDays / p.wobbleDays;
            const float up = p.wobbleDegrees * kDeg * static_cast<float>(std::sin(2.0 * kPi * Frac(tau)));
            const float side = 0.6f * p.wobbleDegrees * kDeg * static_cast<float>(std::sin(2.0 * kPi * Frac(tau * 1.6180339887) + 1.3));
            // The sky's local frame at the body: "up" toward the zenith, "side" along the horizon.
            float u[3] = { -dir[2] * dir[0], -dir[2] * dir[1], 1.0f - dir[2] * dir[2] };
            float e[3] = { -dir[1], dir[0], 0.0f }; // zenith x dir
            Normalize(u);
            Normalize(e);
            const float tu = std::tan(up), ts = std::tan(side);
            for (int c = 0; c < 3; ++c) dir[c] += tu * u[c] + ts * e[c];
            Normalize(dir);
        }
        if (p.driftDegreesPerDay != 0.0f)
        {
            // Compass degrees grow clockwise seen from above, the world's angle the other way.
            const double turns = Frac(p.driftDegreesPerDay * baseDays / 360.0);
            const float a = -static_cast<float>(turns * 2.0 * kPi);
            const float x = dir[0], y = dir[1];
            dir[0] = x * std::cos(a) - y * std::sin(a);
            dir[1] = x * std::sin(a) + y * std::cos(a);
        }
        if (p.sizePulse != 0.0f && p.sizePulseDays > 0.0f)
        {
            const float pulse = 1.0f + p.sizePulse * static_cast<float>(std::sin(2.0 * kPi * Frac(baseDays / p.sizePulseDays)));
            *sizeMul *= pulse > 0.0f ? pulse : 0.0f;
        }
    }

    float Visibility(const bodytables::Body& b, double baseDays, bool* open)
    {
        *open = true;
        if (b.visiblePeriodDays <= 0.0f) return 1.0f;
        const double period = b.visiblePeriodDays;
        const double x = Frac((baseDays - b.visibleOffsetDays) / period) * period; // days into the cycle
        const bool eased = b.visibleEase != 0 && b.visibleEaseSeconds > 0.0f;
        const double easeDays = eased ? b.visibleEaseSeconds / 86400.0 : 0.0;
        if (x < b.visibleDays)
        {
            if (!eased) return 1.0f;
            return static_cast<float>(x >= easeDays ? 1.0 : x / easeDays);
        }
        *open = false;
        if (!eased) return 0.0f;
        const double after = x - b.visibleDays;
        return static_cast<float>(after >= easeDays ? 0.0 : 1.0 - after / easeDays);
    }

    void ApplyEase(const bodytables::Body& b, float k, float sizeDegrees, float dir[3], float* alphaMul, float* sizeMul)
    {
        *alphaMul = 1.0f;
        *sizeMul = 1.0f;
        if (k >= 1.0f) return;
        // Popping (no ease): fully there or gone.
        if (b.visibleEase == 0) { *alphaMul = k > 0.0f ? 1.0f : 0.0f; return; }
        if (b.visibleEase & 1) *alphaMul = k;
        if (b.visibleEase & 2) *sizeMul = k;
        if (b.visibleEase & 4)
        {
            // Sink: slide down to just below the horizon (past the client's ~2 degree horizon fade).
            float az, el;
            ToCompass(dir, &az, &el);
            const float hidden = -(sizeDegrees * 0.5f + 3.0f);
            if (el > hidden) FromCompass(az, hidden + (el - hidden) * k, dir);
        }
    }
}
