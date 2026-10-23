#include "ui/Panels.h"

#include "grass/Grass.h"
#include "imgui.h"

namespace ia::ui {
    void DrawGrass(void*) {
        grass::Settings& s = grass::Config();

        if (!grass::Api()) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", grass::Status());
            return;
        }

        // Short readout lines (the panel's text can't be copied).
        ImGui::TextWrapped("Grass: %s", grass::Status());
        ImGui::Text("Table: %s", grass::TableStatus());
        ImGui::Text("Characters parting the grass: %u", grass::ActorsFed());

        if (!grass::Available()) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Only the instanced grass moves (Vulkan; the Info panel's switch).");
        }

        ImGui::SeparatorText("Motion (not saved)");
        ImGui::Checkbox("Wind and parting", &s.motion);
        ImGui::SliderFloat("Sway", &s.amplitude, 0.0f, 1.5f, "%.2f yd");
        ImGui::SetItemTooltip("How far the tips move at full wind.");
        ImGui::SliderFloat("Flutter", &s.flutter, 0.0f, 0.5f, "%.2f yd");
        ImGui::SetItemTooltip("Extra per-blade shimmer, stronger in gusts.");
        ImGui::SliderFloat("Flutter speed", &s.flutterSpeed, 0.0f, 10.0f, "%.1f rad/s");
        ImGui::SliderFloat("Stiff base", &s.anchor, 0.0f, 0.9f, "%.2f");
        ImGui::SetItemTooltip("The bottom share of each blade that never moves.");

        ImGui::SeparatorText("Parting");
        ImGui::SliderFloat("Push", &s.pushStrength, 0.0f, 2.0f, "%.2f yd");
        ImGui::SetItemTooltip("How far grass leans away from characters.");
        ImGui::SliderFloat("Radius x", &s.radiusScale, 0.5f, 5.0f, "%.1f");
        ImGui::SetItemTooltip("Push radius as a multiple of a character's size.");
        ImGui::SliderFloat("Smallest radius", &s.minRadius, 0.1f, 3.0f, "%.1f yd");
        ImGui::SliderFloat("Mounted x", &s.mountedScale, 1.0f, 4.0f, "%.1f");
        ImGui::TextDisabled("Per plant model: GroundEffectDoodadWind (stiffness, no wind / no push, root and tip V).");
    }
}
