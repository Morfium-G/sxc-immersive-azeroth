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

        ImGui::SeparatorText("Placement (the client's setting, kept)");

        if (grass::HasPlacement()) {
            float minShare = 0.3f;
            int mode = static_cast<int>(grass::Placement(minShare));
            static float minDraft = minShare;
            static bool minEditing = false;
            const char* const modes[] = { "Map file (stock)", "Dominant from paint", "Mixed by paint" };

            if (ImGui::Combo("Ground effect from", &mode, modes, 3)) {
                grass::SetPlacement(static_cast<uint32_t>(mode), minShare);
            }

            ImGui::SetItemTooltip("Map file: each cell takes the layer the map marks dominant (a 0%% base marked dominant leaves it bare).\n"
                                  "Dominant from paint: each cell takes the layer with the largest painted share.\n"
                                  "Mixed by paint: every layer's plants by how much of it is painted where they fall.\n"
                                  "Changing it places all the grass again (a moment's hitch).");

            if (mode == SXC_GRASS_PLACE_PAINTED) {
                if (!minEditing) {
                    minDraft = minShare;
                }

                ImGui::SliderFloat("Least paint", &minDraft, 0.0f, 1.0f, "%.2f");
                minEditing = ImGui::IsItemActive();

                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    grass::SetPlacement(static_cast<uint32_t>(mode), minDraft);
                }

                ImGui::SetItemTooltip("No plants of a layer where less of it than this is painted (a trace of grass on rock). Applies when let go.");
            }
        } else {
            ImGui::TextDisabled("Ground effect placement: the client is too old.");
        }

        // Plants on slopes: stock stands every plant upright on ground up to ~66 degrees steep.
        if (grass::HasSlopes()) {
            float steepest = 0.4f, sink = 0.0f;
            grass::Slopes(steepest, sink);
            static float steepDraft = steepest, sinkDraft = sink;
            static bool slopeEditing = false;

            if (!slopeEditing) {
                steepDraft = steepest;
                sinkDraft = sink;
            }

            bool active = false;
            ImGui::SliderFloat("Steepest ground", &steepDraft, 0.1f, 1.0f, "%.2f");
            active |= ImGui::IsItemActive();
            const bool steepDone = ImGui::IsItemDeactivatedAfterEdit();
            ImGui::SetItemTooltip("The least up part of the ground's normal a plant may stand on.\nStock 0.40 (about 66 degrees); 0.71: 45; 0.87: 30. Applies when let go.");
            ImGui::SliderFloat("Sink on slopes", &sinkDraft, 0.0f, 1.5f, "%.2f");
            active |= ImGui::IsItemActive();
            const bool sinkDone = ImGui::IsItemDeactivatedAfterEdit();
            ImGui::SetItemTooltip("Lowers upright plants on a slope by the drop under their downhill edge, times this (stock 0). Applies when let go.");
            slopeEditing = active;

            if (steepDone || sinkDone) {
                grass::SetSlopes(steepDraft, sinkDraft);
            }

            // The check: what hangs over the ground, and where.
            static sxc_grass_inspect found = {};
            static bool checked = false;
            static float checkRadius = 40.0f;
            ImGui::SetNextItemWidth(120.0f);
            ImGui::SliderFloat("##checkradius", &checkRadius, 10.0f, 150.0f, "%.0f yd");
            ImGui::SameLine();

            if (ImGui::Button("Check the grass around me")) {
                checked = grass::Inspect(checkRadius, found);
            }

            if (checked) {
                ImGui::Text("%u plants, mean slope %.0f deg (%u lie on the slope)", found.plants, found.slope, found.aligned);
                ImGui::Text("Origin off the ground: %u (worst %+.2f yd)", found.off_ground, found.off_max);
                ImGui::Text("Hanging over the ground: %u (worst %.2f yd, their mean slope %.0f deg)", found.hanging, found.hang_max, found.hang_slope);

                if (found.hang_max > 0.0f) {
                    ImGui::TextWrapped("Worst: %.1f, %.1f, %.1f on %.0f deg, size %.2f, doodad %u (%s)", found.worst[0], found.worst[1], found.worst[2], found.worst_slope,
                                       found.worst_scale, found.worst_doodad, found.worst_path ? found.worst_path : "");
                }
            }
        }

        ImGui::SeparatorText("Density (not saved)");
        ImGui::Text("Table: %s", grass::DensityStatus());
        ImGui::Checkbox("Denser near you", &s.density);
        ImGui::SetItemTooltip("Extra plants near you, as GroundEffectDoodadDensity says; these are your limits on it.");
        ImGui::SliderFloat("Most density", &s.maxMultiplier, 1.0f, 8.0f, "x %.1f");
        ImGui::SetItemTooltip("Plants drawn per placed plant, at most (1: never denser).");
        ImGui::SliderFloat("Most radius", &s.maxRadius, 0.0f, 1024.0f, "%.0f yd");
        ImGui::SetItemTooltip("How far around you grass gets denser, at most (full density within half of it).");
        ImGui::SliderFloat("Most spread", &s.maxSpread, 0.0f, 6.0f, "%.1f yd");
        ImGui::SetItemTooltip("How far an extra plant may move from its plant, at most (it stays on the plant's terrain triangle).");

        // Every change places all the grass anew: the sliders apply when let go.
        ImGui::SeparatorText("Soft edges (a test, not saved)");
        static grass::Settings draft = s;
        static bool editing = false;

        if (!editing) {
            draft = s;
        }

        bool active = false;
        auto applied = [&]() {
            active |= ImGui::IsItemActive();

            if (ImGui::IsItemDeactivatedAfterEdit()) {
                s.edgeBand = draft.edgeBand;
                s.edgeShrink = draft.edgeShrink;
                s.edgeSpill = draft.edgeSpill;
                s.edgeShare = draft.edgeShare;
            }
        };

        ImGui::Checkbox("Soft edges", &s.edges);
        ImGui::SetItemTooltip("Where grass meets bare ground, the plants near the border shrink and a few spill onto it.");
        ImGui::SliderFloat("Shrink band", &draft.edgeBand, 0.0f, 4.0f, "%.1f yd");
        applied();
        ImGui::SetItemTooltip("How far inside the border plants shrink (0: none).");
        ImGui::SliderFloat("Size at border", &draft.edgeShrink, 0.1f, 1.0f, "%.2f");
        applied();
        ImGui::SliderFloat("Spill", &draft.edgeSpill, 0.0f, 8.0f, "%.1f yd");
        applied();
        ImGui::SetItemTooltip("How far plants spill onto the bare ground (0: none). They get smaller the farther out.");
        ImGui::SliderFloat("Spill share", &draft.edgeShare, 0.0f, 4.0f, "%.2f");
        applied();
        ImGui::SetItemTooltip("Spilled plants per plant near the border.");
        editing = active;
        ImGui::Checkbox("Same texture only", &s.edgeSameTexture);
        ImGui::SetItemTooltip("Spill only onto bare ground painted with the same terrain texture (off: any bare ground).");
    }
}
