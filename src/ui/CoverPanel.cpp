#include "ui/Panels.h"

#include "World.h"
#include "cover/Cover.h"
#include "env/CoverTable.h"
#include "env/Fields.h"
#include "env/Materials.h"
#include "env/Regional.h"
#include "imgui.h"

namespace ia::ui {
    namespace {
        float g_deposit = 0.1f;
        float g_soak = 1.0f;

        void Entries() {
            const std::vector<cover::Entry>& entries = cover::Entries();

            if (entries.empty()) {
                ImGui::TextDisabled("none yet (the client asks as it draws chunks)");
                return;
            }

            if (!ImGui::BeginTable("##entries", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                return;
            }

            ImGui::TableSetupColumn("#");
            ImGui::TableSetupColumn("material");
            ImGui::TableSetupColumn("depth");
            ImGui::TableSetupColumn("texture");
            ImGui::TableSetupColumn("look");
            ImGui::TableSetupColumn("uses");
            ImGui::TableHeadersRow();

            for (size_t k = 0; k < entries.size(); k++) {
                const cover::Entry& e = entries[k];
                const covertable::Values& v = e.values;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%u", static_cast<unsigned>(k + 1));
                ImGui::TableNextColumn();
                ImGui::Text("%u %s%s", e.material, materials::Name(e.material), e.snow ? " (snow)" : "");
                ImGui::TableNextColumn();

                if (v.depth > 0.0f) {
                    ImGui::Text("%.2f yd", v.depth);
                } else {
                    ImGui::TextDisabled("fallen snow's look");
                }

                ImGui::TableNextColumn();

                if (v.coverTexture > 0) {
                    static const char* const kStates[] = { "not loaded", "loading", "loaded", "loaded (_s)" };
                    const int state = cover::TextureState(v.coverTexture);
                    ImGui::Text("%d %s: %s%s", v.coverTexture, covertable::CoverTexturePath(v.coverTexture), kStates[state & 3],
                                (state & 4) ? "" : (state & 3) >= 2 ? ", no slot (drawn plain)" : "");
                } else {
                    ImGui::TextDisabled("plain");
                }

                ImGui::TableNextColumn();
                ImGui::Text("slope %.0f/%.0f, drift %.2f, breakup %.2f, tint %.2f, wet %.2f%s", v.maxSlope, v.slopeFade, v.driftNoise, v.edgeBreakup, v.tintStrength,
                            v.wetness, covertable::MoistureGated(v.moistureMin) ? ", needs moisture" : "");
                ImGui::TableNextColumn();
                ImGui::Text("%u", e.uses);
            }

            ImGui::EndTable();
        }
    }

    void DrawCover(void*) {
        const world::Snapshot& s = world::Current();
        cover::Look& look = cover::LookSettings();

        if (!cover::Available()) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "The client doesn't draw covers: %s", cover::Status());
            return;
        }

        ImGui::TextWrapped("%s", cover::Status());
        ImGui::TextDisabled("SurfaceCover: %s", covertable::Status());
        ImGui::TextDisabled("chunks resolved since the tables changed: %u", cover::Resolved());

        ImGui::SeparatorText("Look (not saved)");
        ImGui::Checkbox("Covers", &look.enabled);
        ImGui::SameLine();
        ImGui::Checkbox("Fallen snow", &look.fallenSnow);
        ImGui::SameLine();
        ImGui::Checkbox("Depth", &look.depth);
        ImGui::SetItemTooltip("The terrain raised onto the covers (tessellation, where the GPU has it). Off: the flat look.");

        if (look.depth) {
            ImGui::SliderFloat("Detail (yd per segment per yd of distance)", &look.tessDetail, 0.004f, 0.06f, "%.3f");
            ImGui::SetItemTooltip("Smaller: finer near the camera, more GPU. 0.015: a 0.15 yd segment at 10 yd.");
            ImGui::SliderFloat("Subdivided within", &look.tessReach, 30.0f, 600.0f, "%.0f yd");
            ImGui::SliderInt("Segments per edge at most", &look.tessMost, 1, 32);
        }

        ImGui::SetNextItemWidth(200.0f);
        ImGui::Combo("Debug view", &look.debug, "off\0what lies where\0wetness\0entries\0the maps' reach\0the height\0");
        ImGui::SetItemTooltip("What lies where: red bare, green painted cover, white fallen snow, blue under the water line.\n"
                              "The maps' reach: green the near grid (2 yd), blue per chunk, grey the player's zone beyond.\n"
                              "The height: black flat .. white 0.5 yd above the terrain; red where the chunk isn't raised.");
        ImGui::SliderFloat("Plain cover colour", &look.brightness, 0.3f, 1.0f, "%.2f");
        ImGui::SliderFloat("Wet darkening", &look.wetDarkening, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Fallen snow hides the ground from", &look.thinDepth, 0.005f, 0.5f, "%.3f yd");
        ImGui::SliderFloat("x depth", &look.depthScale, 0.0f, 3.0f, "%.2f");
        ImGui::SliderFloat("x breakup", &look.breakupScale, 0.0f, 3.0f, "%.2f");
        ImGui::SliderFloat("x drift", &look.driftScale, 0.0f, 3.0f, "%.2f");

        if (ImGui::Button("Reset look")) {
            look = cover::Look{};
        }

        if (s.inWorld) {
            ImGui::SeparatorText("Here");
            float fallen = 0.0f, keep = 1.0f;
            fields::SnowAt(s.playerPos[0], s.playerPos[1], fallen, keep);
            const fields::MoistureDetail m = fields::Moisture(s.playerPos);
            ImGui::Text("fallen snow %.3f yd, painted snow kept %.0f%%", fallen, keep * 100.0f);

            if (m.known) {
                ImGui::Text("moisture %.2f (rest %.2f: %.2f above)%s", m.value, m.rest, m.value - m.rest > 0.0f ? m.value - m.rest : 0.0f, m.submerged ? ", under water" : "");
            }
        }

        if (ImGui::CollapsingHeader("Entries the client holds")) {
            Entries();
        }

        ImGui::SeparatorText("Testing (not saved)");
        ImGui::SetNextItemWidth(120.0f);
        ImGui::SliderFloat("##deposit", &g_deposit, 0.01f, 0.6f, "%.2f yd");
        ImGui::SameLine();

        if (ImGui::Button("Deposit snow")) {
            regional::DepositSnow(g_deposit);
        }

        ImGui::SetItemTooltip("Adds fallen snow to the player's zone; it then melts or stays by the zone's climate.");
        ImGui::SameLine();

        if (ImGui::Button("Clear snow")) {
            regional::ClearSnow();
        }

        ImGui::SetNextItemWidth(120.0f);
        ImGui::SliderFloat("##soak", &g_soak, 0.0f, 1.0f, "%.2f");
        ImGui::SameLine();

        if (ImGui::Button("Set rain soak")) {
            regional::SetRainSoak(g_soak);
        }

        ImGui::SetItemTooltip("The player's zone's rain soak; the ground follows and dries from there.");
    }
}
