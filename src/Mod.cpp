// Immersive Azeroth: the world feels alive. Simulation and data-driven systems (sky, climate, fog,
// covers, lights, wind) for our client, as a WarcraftXL mod over the client's sxc.* services.
//
// This first version only proves the shape: it loads as a mod, registers its tables with the data
// layer, has a module in the overlay with an overview, and a setting.

#include <cstdio>
#include "SXC/Api/sxc_data.hpp"
#include "SXC/Api/sxc_settings.hpp"
#include "SXC/Api/sxc_ui.hpp"
#include "Tables.h"
#include "imgui.h"
#include "wxl/sdk.hpp"

namespace {
    constexpr const char* kTag = "immersive-azeroth";

    // The tables, in the overview's order.
    const char* const kTables[] = {
        "SkyProfile", "SkyBodySet", "SkyBodySetMember", "SkyBody", "SkyPath", "SkyPathKey", "SkyTime",
        "AreaClimate", "WindProfile", "FogSettings",
        "GroundMaterial", "GroundMaterialSelector", "SurfaceCover",
        "GroundEffectDoodadWind", "GroundEffectDoodadDensity",
        "DoodadLightProperties", "DoodadLightAssignment",
    };

    void DrawOverview(void*) {
        ImGui::TextWrapped("Immersive Azeroth is being ported from WXL. Its tables are registered; the features follow.");
        ImGui::SeparatorText("Tables");

        if (ImGui::BeginTable("##tables", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("table");
            ImGui::TableSetupColumn("rows");
            ImGui::TableSetupColumn("state");
            ImGui::TableHeadersRow();

            for (const char* name : kTables) {
                const sxc::data::Table table = sxc::data::Table::Get(name);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(name);
                ImGui::TableNextColumn();
                ImGui::Text("%u", table.RowCount());
                ImGui::TableNextColumn();
                const std::string_view status = table.Status();

                if (!table.Loaded()) {
                    ImGui::TextDisabled("%.*s", static_cast<int>(status.size()), status.data());
                } else {
                    ImGui::TextUnformatted(status.data(), status.data() + status.size());
                }
            }

            ImGui::EndTable();
        }

        ImGui::TextDisabled("Edit them under Data in the sidebar.");
    }

    class ImmersiveAzeroth final : public wxl::Mod {
    public:
        wxl_result OnLoad() override {
            wxl::Log(WXL_LOG_INFO, kTag, "loaded");
            return WXL_OK;
        }

        // Archives, settings and the data layer are up; the first frame hasn't run.
        void OnReady() override {
            ia::RegisterTables();

            static const sxc::settings::Bool s_enabled("ImmersiveAzeroth", "Enabled", true, "Enabled",
                                                       "Immersive Azeroth's features (none yet; the switch is there from the start).");

            const int module = sxc::ui::AddModule("Immersive Azeroth", "Simulation and data-driven systems: sky, climate, fog, covers, lights, wind.");
            sxc::ui::AddPanel(module, "Overview", DrawOverview);

            char line[160];
            std::snprintf(line, sizeof(line), "ready: tables registered, overlay module %s, enabled %s",
                          module >= 0 ? "added" : "unavailable (Dear ImGui version differs?)", s_enabled.Get() ? "yes" : "no");
            wxl::Log(WXL_LOG_INFO, kTag, line);
        }
    };
}

WXL_MOD(ImmersiveAzeroth, "seyri.immersive-azeroth", "Immersive Azeroth", 1)
