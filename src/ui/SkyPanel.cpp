#include "ui/Panels.h"

#include <cmath>
#include <cstdio>
#include "Config.h"
#include "imgui.h"
#include "sky/Bodies.h"
#include "sky/BodyTables.h"
#include "sky/Clock.h"
#include "sky/Ephemeris.h"
#include "sky/Season.h"
#include "sky/SkyTime.h"

namespace ia::ui {
    namespace {
        const char* const kSpeedNames[] = { "x1 (real time)", "x10", "x60 (1 h per minute)", "x360", "x1440 (1 day per minute)", "x3600" };
        constexpr float kSpeeds[] = { 1.0f, 10.0f, 60.0f, 360.0f, 1440.0f, 3600.0f };
        int g_speedIndex = 0;

        void FormatTime(char* out, size_t size, double minutes) {
            const double frac = sky::clock::DayFractionOf(minutes);
            const int m = static_cast<int>(frac * 1440.0) % 1440;
            std::snprintf(out, size, "%02d:%02d, day %d", m / 60, m % 60, sky::clock::DayOf(minutes));
        }

        void Hm(char* out, size_t n, const char* what, double minutes) {
            if (minutes < 0.0) {
                std::snprintf(out, n, "%s: not within 3 days", what);
            } else {
                std::snprintf(out, n, "%s in %dh%02d", what, static_cast<int>(minutes) / 60, static_cast<int>(minutes) % 60);
            }
        }

        void Time() {
            namespace clk = sky::clock;

            if (!clk::Running()) {
                ImGui::TextDisabled("The client hasn't handed over its time yet (no sxc.sky?).");
            }

            bool over = clk::Overridden();

            if (ImGui::Checkbox("Override the time (debug)", &over)) {
                clk::SetOverride(over);
            }

            bool keep = clk::KeepRule();

            if (ImGui::Checkbox("Keep this place's SkyTime rule while overriding", &keep)) {
                clk::SetKeepRule(keep);
            }

            // The controls move the base time; a place with a pinned time keeps its pin.
            const double base = clk::BaseMinutes();
            const int day = clk::DayOf(base);
            float hours = static_cast<float>(clk::DayFractionOf(base) * 24.0);

            if (ImGui::SliderFloat("Time of day (h)", &hours, 0.0f, 23.99f, "%.2f")) {
                clk::SetBaseMinutes(static_cast<double>(day) * 1440.0 + static_cast<double>(hours) * 60.0);
            }

            if (ImGui::Combo("Speed", &g_speedIndex, kSpeedNames, IM_ARRAYSIZE(kSpeedNames))) {
                clk::SetSpeed(kSpeeds[g_speedIndex]);
            }

            bool paused = clk::Paused();

            if (ImGui::Checkbox("Paused", &paused)) {
                clk::SetPaused(paused);
            }

            if (ImGui::Button("-1 h")) clk::SetBaseMinutes(base - 60.0);
            ImGui::SameLine();
            if (ImGui::Button("+1 h")) clk::SetBaseMinutes(base + 60.0);
            ImGui::SameLine();
            if (ImGui::Button("+1 day")) clk::SetBaseMinutes(base + 1440.0);
            ImGui::SameLine();
            if (ImGui::Button("+30 days")) clk::SetBaseMinutes(base + 30.0 * 1440.0);
            ImGui::SameLine();
            if (ImGui::Button("Back to server time")) clk::SetOverride(false);

            char a[48], b[48];
            FormatTime(a, sizeof(a), clk::SkyMinutes());
            FormatTime(b, sizeof(b), clk::ServerMinutes());
            ImGui::Text("Sky: %s   (server: %s)", a, b);

            const sky::clock::Date d = clk::PlaceDate();
            const sky::season::Season s = sky::season::Here(d.yearFraction);
            ImGui::Text("Date: %04d-%02d-%02d, %s (%.0f%% of the way from midwinter to midsummer)", d.year, d.month, d.day, s.name,
                        (s.warmth * 0.5f + 0.5f) * 100.0f);

            float dayLength = clk::DayLengthStrength();

            if (ImGui::SliderFloat("Seasonal day length (x the table)", &dayLength, 0.0f, 2.0f, "%.2f") && Cfg()) {
                Cfg()->skyDayLength.Set(dayLength);
            }

            ImGui::SetItemTooltip("Saved as [ImmersiveAzeroth] SkyDayLength in the Settings view.");
            const float shift = clk::DayLengthShiftHours();
            const int lm = static_cast<int>(clk::LightTime() * 1440.0) % 1440;
            ImGui::Text("Today: sunrise %s %.0f min, sunset %s %.0f min (the light now looks like %02d:%02d)", shift >= 0.0f ? "earlier by" : "later by",
                        std::fabs(shift) * 60.0f, shift >= 0.0f ? "later by" : "earlier by", std::fabs(shift) * 60.0f, lm / 60, lm % 60);

            if (clk::RuleActive()) {
                ImGui::Text("This place: %s (sky %+.1f h from the base time)", sky::skytime::Describe(), clk::ShiftMinutes() / 60.0f);
            } else {
                ImGui::Text("This place: normal time (no SkyTime rule)");
            }
        }

        void Bodies() {
            bool on = sky::bodies::Enabled();

            if (ImGui::Checkbox("Draw our sky bodies (off: the client's own sky, skyboxes too)", &on) && Cfg()) {
                Cfg()->skyBodies.Set(on);
            }

            ImGui::SetItemTooltip("Saved as [ImmersiveAzeroth] SkyBodies.");

            ImGui::Checkbox("Show routes (where each body goes over its next day or cycle)", &sky::bodies::g_showRoutes);
            ImGui::Checkbox("Glares follow our sun and White Lady (off: stock places)", &sky::bodies::g_glaresFollow);
            ImGui::Checkbox("Bodies drive the world light (SkyBody Flags 8)", &sky::bodies::g_bodiesDriveLight);
            ImGui::Checkbox("Hide the client's moon glare (debug)", &sky::bodies::g_debugHideMoonGlare);
            ImGui::Checkbox("Draw every body after the dome (debug: compare)", &sky::bodies::g_debugAllOwnDraw);
            ImGui::Text("Body set: %s (%s)", sky::bodies::SetName(), sky::bodies::ProfileWhere());
            ImGui::Text("World light from: %s", sky::bodies::LightSource());

            if (ImGui::BeginTable("##bodies", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("body");
                ImGui::TableSetupColumn("where");
                ImGui::TableSetupColumn("next");
                ImGui::TableHeadersRow();

                for (int i = 0; i < sky::bodies::Count(); i++) {
                    const sky::bodies::Info& b = sky::bodies::At(i);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(b.name);
                    ImGui::TableNextColumn();

                    if (!b.drawn) {
                        ImGui::TextDisabled("%s", b.status);
                    } else {
                        char ease[48] = "";

                        if (b.visibility > 0.0f && b.visibility < 1.0f) {
                            std::snprintf(ease, sizeof(ease), ", %s %.0f%%", b.windowOpen ? "arriving" : "leaving", b.visibility * 100.0f);
                        }

                        ImGui::Text("%s, az %.0f, el %.1f, %.2f deg%s%s", b.status, b.azimuth, b.elevation, b.sizeDegrees, ease,
                                    b.ownDraw ? " [after the dome]" : "");
                    }

                    ImGui::TableNextColumn();
                    char r[40], s[40];
                    Hm(r, sizeof(r), "rises", sky::bodies::MinutesUntil(b.name, true));
                    Hm(s, sizeof(s), "sets", sky::bodies::MinutesUntil(b.name, false));
                    ImGui::Text("%s, %s", r, s);
                }

                ImGui::EndTable();
            }

            ImGui::TextDisabled("Tables: %s", sky::bodytables::Status());
        }

        void Lore() {
            namespace eph = sky::ephemeris;
            bool over = eph::LoreOverridden();

            if (ImGui::Checkbox("Override the lore time (debug)", &over)) {
                eph::SetLoreOverride(over);
            }

            const double lore = eph::LoreYearsAt(sky::clock::BaseMinutes());
            float year = static_cast<float>(lore);

            if (ImGui::SliderFloat("Lore year", &year, -500.0f, 500.0f, "%.2f")) {
                eph::SetLoreOverrideYears(year);
            }

            static const double kSteps[] = { -430.0, -10.0, -1.0, 1.0, 10.0, 430.0 };
            static const char* const kStepNames[] = { "-430 y", "-10 y", "-1 y", "+1 y", "+10 y", "+430 y" };

            for (int i = 0; i < 6; i++) {
                if (i) ImGui::SameLine();
                if (ImGui::Button(kStepNames[i])) eph::SetLoreOverrideYears(lore + kSteps[i]);
            }

            bool play = eph::LorePlaying();

            if (ImGui::Checkbox("Play lore time (debug)", &play)) {
                eph::SetLorePlaying(play);
            }

            float rate = eph::LorePlayRate();

            if (ImGui::SliderFloat("Lore years per second", &rate, 0.5f, 50.0f, "%.1f")) {
                eph::SetLorePlayRate(rate);
            }

            const eph::LoreClock& lc = eph::CurrentLoreClock();
            static const char* const kModes[] = { "around", "before", "after" };

            if (eph::LoreOverridden()) {
                ImGui::Text("Lore time: year %.2f (debug %s; this place: year %d)", lore, eph::LorePlaying() ? "play" : "override", lc.year);
            } else if (lc.yearsPerDay > 0.0f) {
                int start = 0;
                eph::LoreStartDays(sky::clock::BaseMinutes(), &start);
                ImGui::Text("Lore time: year %.2f (running: %.2f lore years per day, at year %d on %04d-%02d-%02d%s)", lore, lc.yearsPerDay, lc.year,
                            start / 10000, start / 100 % 100, start % 100, lc.startDate > 0 ? "" : ", the realm year's start: no LoreStartDate set");
            } else {
                ImGui::Text("Lore time: year %.2f (frozen at year %d, wobble %.1f years %s)", lore, lc.year, lc.wobbleYears,
                            kModes[lc.wobbleMode >= 0 && lc.wobbleMode <= 2 ? lc.wobbleMode : 0]);
            }

            for (int i = 0; i < sky::bodies::BodyCount(); i++) {
                sky::bodies::Cycle c;
                const char* name = sky::bodies::BodyName(i);

                if (!sky::bodies::CycleOf(name, c) || !c.lore) {
                    continue;
                }

                char real[48] = "";

                if (c.realDays >= 0.0) {
                    std::snprintf(real, sizeof(real), " (%.1f realm days)", c.realDays);
                }

                ImGui::Text("  %s: %.0f%% round its %.0f-year cycle, next start in %.1f years%s", name, c.position * 100.0, c.period, c.untilNext, real);
            }
        }

        void Skyboxes() {
            ImGui::Checkbox("Hide this zone's M2 skybox (debug)", &sky::bodies::g_hideZoneSkybox);
            ImGui::Checkbox("Hide all M2 skyboxes (debug)", &sky::bodies::g_hideAllSkyboxes);
            const sxc_sky_lighting& l = sky::bodies::LastLighting();

            for (int i = 0; i < 3; i++) {
                const sxc_sky_skybox& s = l.skyboxes[i];

                if (!s.used) {
                    ImGui::TextDisabled("  skybox slot %d: empty", i);
                } else {
                    ImGui::Text("  skybox slot %d: weight %.2f, sky behind it (0x2) %s, follows the time (0x1) %s%s", i, s.weight, s.combine ? "yes" : "no",
                                s.time_of_day ? "yes" : "no", s.hide ? ", HIDDEN" : "");
                }
            }

            ImGui::Text("  current skybox: %s%s", l.current_skybox ? "yes" : "none", l.hide_current ? ", HIDDEN" : "");
            ImGui::TextWrapped("Maps with Map.dbc TimeOfDayOverride keep their own time: the client applies it after ours.");
            ImGui::TextDisabled("SkyTime.cdbc: %s", sky::skytime::Status());
        }
    }

    void DrawSky(void*) {
        if (ImGui::CollapsingHeader("Time", ImGuiTreeNodeFlags_DefaultOpen)) Time();
        if (ImGui::CollapsingHeader("Bodies", ImGuiTreeNodeFlags_DefaultOpen)) Bodies();
        if (ImGui::CollapsingHeader("Lore clock")) Lore();
        if (ImGui::CollapsingHeader("Skyboxes")) Skyboxes();
    }
}
