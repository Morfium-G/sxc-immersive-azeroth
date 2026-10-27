#include "ui/Panels.h"

#include "World.h"
#include "env/CoverTable.h"
#include "env/Fields.h"
#include "env/Materials.h"
#include "env/Regional.h"
#include "imgui.h"

namespace ia::ui {
    namespace {
        float g_deposit = 0.1f;
        float g_soak = 1.0f;

        void Materials(const world::Snapshot& s) {
            terrain::LayerWeights lw;

            if (!terrain::LayerWeightsAt(s.playerPos[0], s.playerPos[1], lw)) {
                ImGui::TextDisabled("no terrain under the player (a hole, a WMO floor over nothing, not loaded)");
                return;
            }

            if (!ImGui::BeginTable("##layers", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                return;
            }

            ImGui::TableSetupColumn("layer");
            ImGui::TableSetupColumn("painted");
            ImGui::TableSetupColumn("texture");
            ImGui::TableSetupColumn("flags");
            ImGui::TableSetupColumn("effect / type");
            ImGui::TableSetupColumn("material");
            ImGui::TableHeadersRow();

            for (int l = 0; l < lw.layers; l++) {
                const terrain::Surface& sf = lw.surface[l];
                const uint32_t material = materials::Select(sf.area, s.mapId, sf.texture, sf.groundEffect, sf.terrainType);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d%s", l, l == lw.dominant ? " (dominant)" : "");
                ImGui::TableNextColumn();
                ImGui::Text("%.2f", lw.weight[l]);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(sf.texture ? sf.texture : "");
                ImGui::TableNextColumn();
                {
                    // MCLY: 0x400 reflective (a cube map). MTXF: 0x1 no specular, bits 4-7 retail's
                    // texture scale (1 << n), which 3.3.5 ignores: a converted map may carry it.
                    const uint32_t mcly = lw.layerFlags[l], mtxf = lw.textureFlags[l], adt = lw.adtTextureFlags[l];
                    const uint32_t scale = (mtxf >> 4) & 0xFu;
                    ImGui::Text("MCLY 0x%X  MTXF 0x%X", mcly, mtxf);

                    if (adt != mtxf) {
                        ImGui::SameLine();
                        ImGui::TextDisabled("(ADT 0x%X, TerrainTextureParams)", adt);
                    }

                    if (scale) {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "scale x%u", 1u << scale);
                    }

                    if (mcly & 0x400) {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "reflective");
                    }
                }
                ImGui::TableNextColumn();
                ImGui::Text("%u / %d", sf.groundEffect, sf.terrainType);
                ImGui::TableNextColumn();

                if (material) {
                    ImGui::Text("%u %s", material, materials::Name(material));
                } else {
                    ImGui::TextDisabled("none");
                }
            }

            ImGui::EndTable();
        }
    }

    void DrawGround(void*) {
        const world::Snapshot& s = world::Current();

        if (!s.inWorld) {
            ImGui::TextDisabled("not in the world");
            return;
        }

        if (!world::HasPointQueries()) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "This client has no sxc.world point queries: the fields stay empty (update the client).");
            return;
        }

        ImGui::TextDisabled("Materials: %s", materials::Status());
        ImGui::TextDisabled("SurfaceCover: %s", covertable::Status());
        const fields::Stats st = fields::GetStats();
        ImGui::Text("grid %u / %u cells known, %u under water, %u hot ground; fill %.2f ms, shore %.2f ms, step %.2f ms", st.filled, st.cells, st.water,
                    st.hotGround, st.fillMs, st.transformMs, st.tickMs);

        const fields::MoistureDetail m = fields::Moisture(s.playerPos);

        if (ImGui::CollapsingHeader("Moisture here", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (!m.known) {
                ImGui::TextDisabled("not known yet");
            } else {
                ImGui::Text("moisture %.2f (rest %.2f, heading to %.2f, %+.4f /s)", m.value, m.rest, m.equilibrium, m.changePerSecond);
                ImGui::Text("ground: absorbency %.2f, open sky %.0f%%%s", m.absorbency, m.open * 100.0f, m.submerged ? ", under liquid" : "");
                ImGui::Text("temperature: ground %.1f degC, air %.1f degC, humidity %.2f", m.temperature, m.airTemperature, m.humidity);

                if (m.waterDistance >= 0.0f) {
                    ImGui::Text("liquid %u (%s) %.0f yd away, %.1f yd below; shore %.2f, wets x%.2f", m.liquid, terrain::LiquidNameOf(m.liquid), m.waterDistance,
                                m.heightAboveWater, m.shore, m.liquidMoisture);

                    if (m.liquidHot) {
                        ImGui::Text("  hot: %.0f degC, its heat reaches %.0f%% here", m.liquidTemperature, m.heat * 100.0f);
                    }
                } else {
                    ImGui::TextDisabled("no liquid within the grid");
                }

                if (m.hotShare > 0.0f || m.groundHeatDistance >= 0.0f) {
                    ImGui::Text("hot ground: %.0f%% painted here (%.0f degC); nearest %.0f yd, reach %.2f (%.0f degC)", m.hotShare * 100.0f, m.hotTemperature,
                                m.groundHeatDistance, m.groundHeat, m.groundHeatTemperature);
                }

                ImGui::Text("zone %u \"%s\": share %.2f, soak mix %.2f, rain here %.2f", m.zone, world::AreaName(m.zone), m.zoneShare, m.zoneSoakMix, m.rainShare);
            }
        }

        if (ImGui::CollapsingHeader("Snow here")) {
            const fields::SnowDetail d = fields::Snow(s.playerPos);

            if (!d.known) {
                ImGui::TextDisabled("not known yet");
            } else {
                ImGui::Text("fallen %.3f yd (heading to %.3f): the zones' %.3f, held %.0f%%", d.fallen, d.target, d.zoneSnow, d.hold * 100.0f);
                ImGui::Text("painted %.2f yd, %.0f%% kept (cap %.0f%%; melt columns %.2f / %.0f degC)", d.painted, d.keep * 100.0f, d.cap * 100.0f, d.keptShare,
                            d.goneTemperature);
                ImGui::Text("temperature %.1f degC (sun %+.1f), the zone's %.1f; local heat %.2f, melt water %.2f", d.temperature, d.sun, d.zoneTemperature,
                            d.heatReach, d.meltWet);
            }

            ImGui::TextDisabled("published to the cover %u times; %s", st.snowPublished, fields::SnowActive() ? "snow somewhere" : "no snow anywhere");
        }

        if (ImGui::CollapsingHeader("The ground's layers here")) {
            Materials(s);
        }

        if (ImGui::CollapsingHeader("Zones (this session)")) {
            const regional::ZoneMap& zm = regional::Map();
            ImGui::Text("zone map: %u / %d chunks known", zm.known, zm.size * zm.size);

            if (ImGui::BeginTable("##zones", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("zone");
                ImGui::TableSetupColumn("rain soak");
                ImGui::TableSetupColumn("fallen snow");
                ImGui::TableSetupColumn("painted kept");
                ImGui::TableSetupColumn("degC");
                ImGui::TableSetupColumn("seen");
                ImGui::TableHeadersRow();
                const double now = regional::Now();

                for (const regional::Zone& z : regional::Zones()) {
                    const bool here = z.map == s.mapId && z.id == regional::PlayerZone();
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%s%u %s (map %d)", here ? "> " : "", z.id, world::AreaName(z.id), z.map);
                    ImGui::TableNextColumn();
                    ImGui::Text("%.2f", z.rainSoak);
                    ImGui::TableNextColumn();
                    ImGui::Text("%.3f yd", z.snowDepth);
                    ImGui::TableNextColumn();
                    ImGui::Text("%.0f%%", z.paintedKeep * 100.0f);
                    ImGui::TableNextColumn();
                    ImGui::Text("%.1f", z.temperature);
                    ImGui::TableNextColumn();

                    if (here) {
                        ImGui::TextUnformatted(z.catchUp > 0.0f ? "here (catching up)" : "here");
                    } else {
                        ImGui::Text("%.0f s ago", now - z.lastSeen);
                    }
                }

                ImGui::EndTable();
            }
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
        ImGui::SameLine();

        if (ImGui::Button("Refill the grid")) {
            fields::Refill();
        }
    }
}
