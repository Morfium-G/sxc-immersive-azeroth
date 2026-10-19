#include "ui/Panels.h"

#include <cmath>
#include "World.h"
#include "env/Shelter.h"
#include "env/Wind.h"
#include "imgui.h"

namespace ia::ui {
    namespace {
        const char* SourceName(wind::Source source) {
            switch (source) {
                case wind::Source::Global: return "Global";
                case wind::Source::Map: return "Map";
                case wind::Source::Area: return "Area";
                default: return "built in";
            }
        }

        // Compass bearing (degrees the wind blows toward) of a world direction: 0 north (+x), 90 east (-y).
        float Bearing(float x, float y) {
            float b = std::atan2(-y, x) * 57.2957795f;
            return b < 0.0f ? b + 360.0f : b;
        }

        const char* Compass(float bearing) {
            static const char* const kNames[] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
            return kNames[static_cast<int>(std::floor(bearing / 45.0f + 0.5f)) & 7];
        }

        bool g_weatherTest = false;
        float g_weatherTestValue = 1.0f;

        // --- arrows in the world -------------------------------------------------------------------------
        bool g_showArrows = false;
        int g_gridSize = 11;     // arrows per side
        float g_spacing = 6.0f;  // yards between arrows
        float g_height = 1.0f;   // yards above the ground

        // Green (calm) .. yellow .. red (strong); a passing gust brightens toward white; the lee tints
        // toward purple by how deep in it the point is.
        ImU32 StrengthColor(float strength, float gust, float lee) {
            const float t = strength < 0.0f ? 0.0f : (strength > 1.0f ? 1.0f : strength);
            float r = t < 0.5f ? t * 2.0f : 1.0f;
            float g = t < 0.5f ? 1.0f : 2.0f - t * 2.0f;
            float b = 0.0f;
            r += (1.0f - r) * gust * 0.8f;
            g += (1.0f - g) * gust * 0.8f;
            b += gust * 0.8f;
            const float shade = 1.0f - (lee < 0.0f ? 0.0f : (lee > 1.0f ? 1.0f : lee));
            r += (0.75f - r) * shade;
            g += (0.25f - g) * shade;
            b += (1.0f - b) * shade;
            return IM_COL32(static_cast<int>(r * 255), static_cast<int>(g * 255), static_cast<int>(b * 255), 255);
        }

        // A line between two world points, if both are in front of the camera.
        void WorldLine(ImDrawList* draw, const ImVec2& size, const float a[3], const float b[3], ImU32 color) {
            float ax, ay, bx, by;

            if (world::Project(a, size.x, size.y, ax, ay) && world::Project(b, size.x, size.y, bx, by)) {
                draw->AddLine(ImVec2(ax, ay), ImVec2(bx, by), color, 2.0f);
            }
        }

        // A grid of arrows around the player, on the ground: length and colour by strength, roofed
        // spots as small cyan crosses (so roofs show their outline). Drawn on the overlay, behind its
        // windows, while this panel is open.
        void DrawArrows(const world::Snapshot& s) {
            ImDrawList* draw = ImGui::GetBackgroundDrawList(ImGui::GetMainViewport());
            const ImVec2 size = ImGui::GetIO().DisplaySize;
            const float half = (g_gridSize - 1) * 0.5f;

            for (int iy = 0; iy < g_gridSize; iy++) {
                for (int ix = 0; ix < g_gridSize; ix++) {
                    const float x = s.playerPos[0] + (ix - half) * g_spacing, y = s.playerPos[1] + (iy - half) * g_spacing;
                    float z = s.playerPos[2];
                    terrain::HeightAt(x, y, z);
                    z += g_height;
                    const float p[3] = { x, y, z };
                    const wind::Sample w = wind::At(p, g_height);

                    if (w.open < 0.5f) {
                        const float r = 0.4f;
                        const float a0[3] = { x - r, y - r, z }, a1[3] = { x + r, y + r, z }, b0[3] = { x - r, y + r, z }, b1[3] = { x + r, y - r, z };
                        WorldLine(draw, size, a0, a1, IM_COL32(0, 200, 255, 255));
                        WorldLine(draw, size, b0, b1, IM_COL32(0, 200, 255, 255));
                        continue;
                    }

                    const float len = 0.6f + w.strength * 5.0f; // yards
                    const float hx = w.dirX * len * 0.5f, hy = w.dirY * len * 0.5f;
                    const ImU32 color = StrengthColor(w.strength, w.gust, w.lee);
                    const float tail[3] = { x - hx, y - hy, z }, tip[3] = { x + hx, y + hy, z };
                    WorldLine(draw, size, tail, tip, color);

                    // The head: two short lines angled back from the tip.
                    const float headLen = 0.25f + len * 0.2f;
                    const float bx = -w.dirX * headLen, by = -w.dirY * headLen;
                    const float sx = -w.dirY * headLen * 0.5f, sy = w.dirX * headLen * 0.5f;
                    const float left[3] = { tip[0] + bx + sx, tip[1] + by + sy, z }, right[3] = { tip[0] + bx - sx, tip[1] + by - sy, z };
                    WorldLine(draw, size, tip, left, color);
                    WorldLine(draw, size, tip, right, color);
                }
            }
        }
    }

    void DrawWind(void*) {
        const world::Snapshot& s = world::Current();

        if (!s.inWorld) {
            ImGui::TextDisabled("not in the world");
            return;
        }

        if (!world::HasPointQueries()) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "This client has no sxc.world point queries: no shelter (update the client).");
        }

        ImGui::TextDisabled("WindProfile.cdbc: %s", wind::Status());
        const float bearing = wind::BearingDegrees();
        ImGui::Text("steady: ground %.2f, aloft %.2f, blowing toward %.0f deg (%s); weather used %.2f", wind::SteadyGround(), wind::SteadyAloft(), bearing,
                    Compass(bearing), wind::EffectiveWeather());

        const wind::Sample here = wind::At(s.playerPos, 1.0f);
        const float hereBearing = Bearing(here.dirX, here.dirY);
        ImGui::Text("at the player: strength %.2f (gust %.2f), toward %.0f deg (%s), open sky %.0f%%, lee %.2f", here.strength, here.gust, hereBearing,
                    Compass(hereBearing), here.open * 100.0f, here.lee);

        // A strength bar with the gust on top: what grass and particles will see.
        ImGui::ProgressBar(here.strength > 1.0f ? 1.0f : here.strength, ImVec2(-1.0f, 0.0f), here.strength > 1.0f ? "> 1 (gust)" : nullptr);

        ImGui::Checkbox("Show wind arrows", &g_showArrows);
        ImGui::SetItemTooltip("Arrows on the ground around you while this panel is open: longer and redder = stronger, white flashes = gusts, "
                              "purple = in the lee, cyan crosses = roofed over.");

        if (g_showArrows) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.0f);
            ImGui::SliderInt("##grid", &g_gridSize, 3, 25, "%d per side");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.0f);
            ImGui::SliderFloat("##spacing", &g_spacing, 2.0f, 20.0f, "%.0f yd apart");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.0f);
            ImGui::SliderFloat("##height", &g_height, 0.0f, 20.0f, "%.0f yd up");

            DrawArrows(s);
        }

        if (ImGui::CollapsingHeader("Profile here", ImGuiTreeNodeFlags_DefaultOpen) &&
            ImGui::BeginTable("##wind", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("field");
            ImGui::TableSetupColumn("in use");
            ImGui::TableSetupColumn("target");
            ImGui::TableSetupColumn("from");
            ImGui::TableHeadersRow();
            const wind::Resolved& current = wind::Current();
            const wind::Resolved& target = wind::Target();

            for (int f = 0; f < wind::FieldCount; f++) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(wind::FieldName(f));
                ImGui::TableNextColumn();
                ImGui::Text("%.2f", current.value[f]);
                ImGui::TableNextColumn();
                ImGui::Text("%.2f", target.value[f]);
                ImGui::TableNextColumn();

                if (target.source[f] == wind::Source::Global || target.source[f] == wind::Source::BuiltIn) {
                    ImGui::TextUnformatted(SourceName(target.source[f]));
                } else {
                    ImGui::Text("%s %u", SourceName(target.source[f]), target.sourceId[f]);
                }
            }

            ImGui::EndTable();
        }

        if (ImGui::CollapsingHeader("Shelter")) {
            const shelter::Stats st = shelter::GetStats();
            ImGui::Text("%u cached cells, %u rays this frame, %u deferred", st.entries, st.raysThisFrame, st.deferredThisFrame);
            bool enabled = shelter::Enabled();

            if (ImGui::Checkbox("roofs calm the wind", &enabled)) {
                shelter::SetEnabled(enabled);
            }

            bool lee = shelter::LeeEnabled();
            ImGui::SameLine();

            if (ImGui::Checkbox("lee behind walls and cliffs", &lee)) {
                shelter::SetLeeEnabled(lee);
            }

            shelter::LeeParams p = shelter::GetLeeParams();
            bool changed = ImGui::SliderFloat("lee reach (yd)", &p.reach, 2.0f, 40.0f, "%.0f");
            changed |= ImGui::SliderFloat("lee angle (deg)", &p.angleDeg, 0.0f, 45.0f, "%.0f");
            changed |= ImGui::SliderFloat("lee strength", &p.strength, 0.0f, 1.0f, "%.2f");

            if (changed) {
                shelter::SetLeeParams(p);
            }

            if (ImGui::Button("Forget the cache")) {
                shelter::Clear();
            }
        }

        ImGui::SeparatorText("Testing (not saved)");
        float multiplier = wind::StrengthMultiplier();

        if (ImGui::SliderFloat("strength x", &multiplier, 0.0f, 4.0f, "%.2f")) {
            wind::SetStrengthMultiplier(multiplier);
        }

        bool changed = ImGui::Checkbox("force the weather", &g_weatherTest);
        ImGui::SameLine();
        changed |= ImGui::SliderFloat("##weather", &g_weatherTestValue, 0.0f, 1.0f, "%.2f");

        if (changed) {
            wind::SetWeatherOverride(g_weatherTest ? g_weatherTestValue : -1.0f);
        }

        ImGui::SetItemTooltip("Only what the wind sees. For real weather (rain falling, everything following it), use the Climate panel's weather.");
    }
}
