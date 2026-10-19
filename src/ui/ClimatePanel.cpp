#include "ui/Panels.h"

#include <cstdio>
#include "World.h"
#include "climate/Climate.h"
#include "imgui.h"

namespace ia::ui {
    namespace {
        int g_weatherChoice = 0; // 0 live, 1 fine, 2 rain, 3 snow, 4 sand
        float g_weatherIntensity = 1.0f;
    }

    void DrawClimate(void*) {
        const world::Snapshot& s = world::Current();

        if (!s.inWorld) {
            ImGui::TextDisabled("not in the world");
            return;
        }

        const climate::Clock& c = climate::Now();
        const int minutes = static_cast<int>(c.dayFraction * 1440.0f + 0.5f) % 1440;
        ImGui::Text("date %04d-%02d-%02d %02d:%02d (the place's); light time %02d:%02d; year %.2f", c.year, c.month, c.day, c.hour, c.minute,
                    minutes / 60, minutes % 60, c.yearFraction);

        // Where: the zone names and the area chain (what AreaClimate and the other scoped tables match
        // against, most specific first).
        ImGui::Text("zone \"%s\", sub-zone \"%s\", map %d, %s", s.zoneText, s.subZoneText, s.mapId, s.outdoors ? "outdoors" : "indoors");
        char line[256];
        int n = std::snprintf(line, sizeof(line), "area chain:");

        for (int i = 0; i < s.areaCount && n < static_cast<int>(sizeof(line)) - 16; i++) {
            n += std::snprintf(line + n, sizeof(line) - n, "%s %u", i ? " ->" : "", s.areaChain[i]);
        }

        if (!s.areaCount) {
            std::snprintf(line + n, sizeof(line) - n, " none");
        }

        ImGui::TextUnformatted(line);

        const uint32_t area = s.areaCount > 0 ? s.areaChain[0] : 0;
        const climate::Row& row = climate::For(area, s.mapId);
        const climate::Row here = climate::Here();
        ImGui::TextDisabled("AreaClimate.cdbc: %s", climate::Status());
        ImGui::Text("this area's row: day %.1f / night %.1f degC, season +-%.1f (offset %.1f months), humidity %.2f, fogginess %.2f, day length %.1f h",
                    row.dayTemp, row.nightTemp, row.seasonAmplitude * 0.5f, row.seasonOffset, row.humidity, row.fogginess, row.dayLength);
        ImGui::Text("blended here: day %.1f / night %.1f degC, humidity %.2f", here.dayTemp, here.nightTemp, here.humidity);
        const climate::Breakdown t = climate::Temperature(here);
        const climate::Weather& w = climate::CurrentWeather();
        static const char* const kWeather[] = { "fine", "rain", "snow", "sand" };
        ImGui::Text("temperature %.1f degC = %.1f time of day %+.1f season %+.1f weather (%s %.2f)%s", t.total, t.daily, t.season, t.weather,
                    kWeather[w.type >= 0 && w.type < 4 ? w.type : 0], w.intensity, t.test != 0.0f ? " + test offset" : "");
        const climate::Sun& sun = climate::SunNow();
        ImGui::Text("sun: daylight %.2f, direction %.2f %.2f %.2f (%s)", sun.daylight, sun.dir[0], sun.dir[1], sun.dir[2],
                    sun.fromClient ? "the world light" : "computed");

        ImGui::SeparatorText("Testing (not saved)");
        static const char* const kChoices[] = { "live weather", "fine", "rain", "snow", "sand" };
        bool changed = ImGui::Combo("weather", &g_weatherChoice, kChoices, IM_ARRAYSIZE(kChoices));
        changed |= ImGui::SliderFloat("intensity", &g_weatherIntensity, 0.0f, 1.0f, "%.2f");

        if (changed) {
            climate::SetWeatherOverride(g_weatherChoice - 1, g_weatherIntensity);
        }

        ImGui::SetItemTooltip("Only what the climate sees; the client's weather stays.");
        float offset = climate::TemperatureOffset();

        if (ImGui::SliderFloat("temperature offset (degC, every place)", &offset, -30.0f, 30.0f, "%.1f")) {
            climate::SetTemperatureOffset(offset);
        }

        ImGui::TextDisabled("The time of day follows the sky clock (Sky panel).");
    }
}
