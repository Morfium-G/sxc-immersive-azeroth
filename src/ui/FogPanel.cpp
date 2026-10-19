#include "ui/Panels.h"

#include "World.h"
#include "fog/Fog.h"
#include "imgui.h"

namespace ia::ui {
    namespace {
        float g_amount = 1.0f;
        float g_radius = 40.0f;
        float g_thickness = 4.0f;
        bool g_pool = false;
    }

    void DrawFog(void*) {
        if (!fog::Available()) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", fog::Status());
            ImGui::TextDisabled("Volumetric fog needs the Vulkan renderer (-vulkan). D3D9 keeps the client's own fog.");
            return;
        }

        ImGui::TextDisabled("%s", fog::Status());
        fog::Look& look = fog::Settings();
        ImGui::Checkbox("Draw the fog", &look.enabled);
        ImGui::SameLine();
        ImGui::Checkbox("magenta (where is it?)", &look.debug);

        ImGui::SeparatorText("Test fog");
        ImGui::TextWrapped("Until the fog's rules are ported (mist at night, water fog, swamps...), fog comes from here: deposited around you, "
                           "burning off by itself.");
        ImGui::SliderFloat("amount", &g_amount, 0.05f, 1.0f, "%.2f");
        ImGui::SliderFloat("radius (yd)", &g_radius, 4.0f, 120.0f, "%.0f");
        ImGui::SliderFloat(g_pool ? "pool depth (yd)" : "layer height (yd)", &g_thickness, 0.5f, 20.0f, "%.1f");
        ImGui::Checkbox("as a pool (flat top, fills hollows)", &g_pool);

        if (ImGui::Button("Deposit")) {
            fog::Deposit(g_amount, g_radius, g_thickness, g_pool);
        }

        ImGui::SameLine();

        if (ImGui::Button("Clear")) {
            fog::Clear();
        }

        ImGui::SameLine();
        ImGui::Text("%u cells with fog", fog::CellsWithFog());
        ImGui::SliderFloat("burns off in (s, half-life; 0 stays)", &fog::BurnOffSeconds(), 0.0f, 600.0f, "%.0f");

        ImGui::SeparatorText("Look (not saved)");
        ImGui::SliderFloat("density x", &look.densityScale, 0.0f, 5.0f, "%.2f");
        ImGui::SliderFloat("patchiness", &look.noiseAmount, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("patch size (yd per tile)", &look.noiseTile, 8.0f, 256.0f, "%.0f");
        ImGui::SliderFloat("drift with the wind x", &look.drift, 0.0f, 4.0f, "%.2f");
        ImGui::SliderFloat("reach (yd)", &look.farDistance, 50.0f, 800.0f, "%.0f");
        ImGui::SetItemTooltip("How far the volume reaches; the sky gets this much fog too. Beyond it, the client's own distance fog.");

        static const char* const kModes[] = { "the place's fog colour", "own colour" };
        ImGui::Combo("colour", &look.colourMode, kModes, IM_ARRAYSIZE(kModes));

        if (look.colourMode == 1) {
            ImGui::ColorEdit3("##own", look.colour);
        }

        ImGui::SliderFloat("brightness", &look.brightness, 0.0f, 3.0f, "%.2f");

        ImGui::SeparatorText("Haze");
        ImGui::SliderFloat("haze density (per yd)", &look.hazeDensity, 0.0f, 0.02f, "%.4f");
        ImGui::SetItemTooltip("Everywhere, thinning upward from just below your feet. 0.002 is a light haze, 0.01 thick.");
        ImGui::SliderFloat("haze height (yd)", &look.hazeThickness, 5.0f, 200.0f, "%.0f");
    }
}
