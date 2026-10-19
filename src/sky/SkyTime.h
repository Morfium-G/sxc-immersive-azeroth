// SkyTime.cdbc: per place, the sky's time pinned (always night, always noon) or shifted (time zones),
// and the place's date pinned to a day of the year. Scoped: Global < Map < Area (zone) < Area
// (sub-area), field by field.
#pragma once

#include <cstdint>
#include "World.h"

namespace ia::sky::skytime {
    const char* Status();
    const char* Describe(); // the last rule found where the player is, in words (panel)

    struct Rule {
        bool pinned = false;
        float pinMinute = 0.0f;     // 0..1440 when pinned
        float offsetMinutes = 0.0f; // added to the clock when not pinned
        float blendSeconds = 10.0f; // time to move to a new rule
        int pinnedDay = 0;          // 1..365: the place's date is pinned to this day of the year; 0 = the realm's
        int scopeType = -1;         // the most specific row that set anything (-1 none)
        uint32_t scopeId = 0;
    };

    // The rule where the player is (cached per area and map); remembered for Describe().
    const Rule& Here(const world::Snapshot& snap);
    // The rule for a place, without remembering it.
    const Rule& Resolve(const world::Snapshot& snap);
}
