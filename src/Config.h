// The mod's settings ([ImmersiveAzeroth] in WTF\SXC.ini, shown in the overlay's Settings view).
// Declared at WXL_STAGE_READY; read every frame, so the Settings view applies at once.
#pragma once

#include <memory>
#include "SXC/Api/sxc_settings.hpp"

namespace ia {
    struct Config {
        sxc::settings::Bool enabled{ "ImmersiveAzeroth", "Enabled", true, "Enabled",
                                     "Immersive Azeroth's features. Off: the client's own sky, nothing else changes either." };
        sxc::settings::Bool skyBodies{ "ImmersiveAzeroth", "SkyBodies", true, "Sky bodies",
                                       "Draw the sun, the moons and custom bodies from the SkyBody tables (off: the client's own)." };
        sxc::settings::Float skyDayLength{ "ImmersiveAzeroth", "SkyDayLength", 1.0f, 0.0f, 2.0f, "Seasonal day length",
                                           "x the places' AreaClimate DayLength: 0 = the stock day length all year." };
        sxc::settings::Int loreYear{ "ImmersiveAzeroth", "LoreYear", 27, -1000, 1000, "Lore year",
                                     "The lore clock's year where no SkyProfile row sets one (0 = the Dark Portal opens, 27 = WotLK)." };
    };

    // Null before WXL_STAGE_READY.
    Config* Cfg();
    void CreateConfig();
}
