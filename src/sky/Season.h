// The season where the player is: the climate's own season curve (AreaClimate: warmest around
// 20 July, shifted by the place's SeasonOffset months), so seasons, temperature and the sky's day
// length all agree.
#pragma once

#include <cmath>
#include "climate/Climate.h"

namespace ia::sky::season {
    struct Season {
        const char* name = "summer"; // "spring", "summer", "autumn", "winter"
        float phase = 0.0f;          // 0 = midsummer, 0.25 = mid-autumn, 0.5 = midwinter, 0.75 = mid-spring
        float warmth = 1.0f;         // 1 midsummer .. -1 midwinter
    };

    // At a year fraction (0 = 1 January), with the season shift of the place's climate.
    inline Season At(float yearFraction, float offsetMonths) {
        constexpr float kPeak = 0.55f, kPi = 3.14159265f;
        float p = yearFraction - kPeak - offsetMonths / 12.0f;
        p -= std::floor(p);
        Season s;
        s.phase = p;
        s.warmth = std::cos(2.0f * kPi * p);
        s.name = p < 0.125f || p >= 0.875f ? "summer" : (p < 0.375f ? "autumn" : (p < 0.625f ? "winter" : "spring"));
        return s;
    }

    inline Season Here(float yearFraction) { return At(yearFraction, climate::Here().seasonOffset); }
}
