// Immersive Azeroth: the world feels alive. Simulation and data-driven systems (sky, climate, fog,
// covers, lights, wind) for our client, as a WarcraftXL mod over the client's sxc.* services.
//
// Now: its tables, the sky clock (debug override, pinned / shifted places, seasonal day length), the
// sky bodies, the climate, wind and shelter, the ground materials, the zones' rain and snow, the world
// fields near the player (moisture, temperature, snow), the fog, the covers in the terrain (snow, sand,
// mud, wetness, trails), point lights on the terrain, Lua functions, and panels in the overlay.

#include <cstdio>
#include <memory>
#include "Config.h"
#include "Lua.h"
#include "Profiler.h"
#include "Saved.h"
#include "SXC/Api/sxc_data.hpp"
#include "SXC/Api/sxc_ui.hpp"
#include "Tables.h"
#include "World.h"
#include "climate/Climate.h"
#include "cover/Cover.h"
#include "env/CoverTable.h"
#include "env/Emitters.h"
#include "env/Fields.h"
#include "env/Materials.h"
#include "env/Regional.h"
#include "env/Shelter.h"
#include "env/Wind.h"
#include "fog/Field.h"
#include "fog/Fog.h"
#include "fog/FogSettings.h"
#include "imgui.h"
#include "grass/Grass.h"
#include "lights/Lights.h"
#include "sky/Bodies.h"
#include "sky/BodyTables.h"
#include "sky/Clock.h"
#include "sky/Ephemeris.h"
#include "sky/SkyApi.h"
#include "ui/Panels.h"
#include "wxl/sdk.hpp"

namespace ia {
    namespace {
        std::unique_ptr<Config> g_config;
    }

    Config* Cfg() { return g_config.get(); }
    void CreateConfig() { g_config.reset(new Config()); }
}

namespace {
    constexpr const char* kTag = "immersive-azeroth";

    // The tables, in the overview's order.
    const char* const kTables[] = {
        "SkyProfile", "SkyBodySet", "SkyBodySetMember", "SkyBody", "SkyPath", "SkyPathKey", "SkyTime",
        "AreaClimate", "WindProfile", "FogSettings",
        "GroundMaterial", "GroundMaterialSelector", "SurfaceCover",
        "GroundEffectDoodadWind", "GroundEffectDoodadDensity",
        "DoodadLightProperties", "DoodadEmitterProperties", "DoodadSourceAssignment",
    };

    void DrawOverview(void*) {
        ImGui::TextWrapped("Immersive Azeroth is being ported from WXL. Here now: the sky clock and bodies, the climate, wind, the world fields "
                           "(moisture, temperature, snow), the zones' weather memory, the fog and the covers' look. Next: the covers' depth, "
                           "lights, grass.");
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

    // --- the sky provider (sxc.sky) ---------------------------------------------------------------------

    bool Active() {
        return ia::Cfg() && ia::Cfg()->enabled.Get();
    }

    void WXL_CALL SkyTime(void*, sxc_sky_time* io) {
        ia::sky::clock::OnTime(*io, Active()); // notes the client's time even while off (the panel shows it)
    }

    void WXL_CALL SkyLighting(void*, sxc_sky_lighting* io) {
        if (Active()) {
            ia::sky::bodies::OnLighting(*io);
        }
    }

    wxl_bool WXL_CALL SkyBodies(void*, int32_t pass) {
        return Active() && ia::sky::bodies::OnBodies(pass) ? WXL_TRUE : WXL_FALSE;
    }

    class ImmersiveAzeroth final : public wxl::Mod {
    public:
        wxl_result OnLoad() override {
            m_update = wxl::Subscribe<WXL_EV_Update>(&m_onUpdate);
            m_scriptReady = wxl::Subscribe<WXL_EV_ScriptVmReady>(&m_onScriptReady);
            wxl::Log(WXL_LOG_INFO, kTag, "loaded");
            return WXL_OK;
        }

        // Archives, settings and the data layer are up; the first frame hasn't run.
        void OnReady() override {
            ia::RegisterTables();
            ia::CreateConfig();

            if (const sxc_sky_api* sky = ia::sky::Api()) {
                sxc_sky_provider provider = {};
                provider.struct_size = sizeof(provider);
                provider.time = SkyTime;
                provider.lighting = SkyLighting;
                provider.bodies = SkyBodies;
                sky->set_provider(&provider);
            }

            const int module = sxc::ui::AddModule("Immersive Azeroth", "Simulation and data-driven systems: sky, climate, fog, covers, lights, wind.");
            sxc::ui::AddPanel(module, "Overview", DrawOverview);
            sxc::ui::AddPanel(module, "Sky", ia::ui::DrawSky);
            sxc::ui::AddPanel(module, "Climate", ia::ui::DrawClimate);
            sxc::ui::AddPanel(module, "Wind", ia::ui::DrawWind);
            sxc::ui::AddPanel(module, "Ground", ia::ui::DrawGround);
            sxc::ui::AddPanel(module, "Fog", ia::ui::DrawFog);
            sxc::ui::AddPanel(module, "Cover", ia::ui::DrawCover);
            sxc::ui::AddPanel(module, "Lights", ia::ui::DrawLights);
            sxc::ui::AddPanel(module, "Light editor", ia::ui::DrawLightEditor);
            sxc::ui::AddPanel(module, "Grass", ia::ui::DrawGrass);

            char line[160];
            std::snprintf(line, sizeof(line), "ready: tables registered, sky provider %s, overlay module %s", ia::sky::Api() ? "set" : "unavailable",
                          module >= 0 ? "added" : "unavailable (Dear ImGui version differs?)");
            wxl::Log(WXL_LOG_INFO, kTag, line);
            m_ready = true;
        }

    private:
        // Every frame, before the world draws.
        void Update(float dt) {
            if (!m_ready) {
                return;
            }

            ia::saved::SyncAll();   // the panels' settings to and from SXC.ini
            const ia::Config& cfg = *ia::Cfg();
            ia::sky::bodies::SetEnabled(cfg.enabled.Get() && cfg.skyBodies.Get());
            ia::sky::bodytables::SetDefaultLoreYear(cfg.loreYear.Get());
            ia::sky::clock::SetDayLengthStrength(cfg.enabled.Get() ? cfg.skyDayLength.Get() : 0.0f);

            namespace prof = ia::prof;
            prof::Enter(prof::kWorld);
            const ia::world::Snapshot& snap = ia::world::Refresh();
            prof::Leave(prof::kWorld);
            prof::Enter(prof::kSky);
            ia::sky::clock::Update(dt, snap);
            ia::sky::ephemeris::Tick(dt);
            prof::Leave(prof::kSky);
            { prof::Scope p(prof::kClimate); ia::climate::Update(snap); }
            { prof::Scope p(prof::kSky); ia::sky::bodies::Update(snap); }

            // The environment: off with the mod, forgotten when the world is left (a new character,
            // a logout: the zones' weather memory is per session).
            if (!cfg.enabled.Get() || !snap.inWorld) {
                if (m_envRunning) {
                    ia::fog::Stop();
                    ia::cover::Stop();
                    ia::lights::Stop();
                    ia::grass::Stop();
                    ia::fog::Reset();
                    ia::fields::Reset();
                    ia::regional::Reset();
                    ia::shelter::Clear();
                    ia::emitters::Reset();
                    m_envRunning = false;
                }

                return;
            }

            m_envRunning = true;
            prof::Enter(prof::kGround);
            ia::materials::Update();
            ia::covertable::Update();
            ia::emitters::Update();   // before the wind and the fields: they take them
            prof::Leave(prof::kGround);
            { prof::Scope p(prof::kWind); ia::wind::Update(dt, snap); }
            { prof::Scope p(prof::kGrass); ia::grass::Update(dt, snap); }
            { prof::Scope p(prof::kRegional); ia::regional::Update(dt, snap); }
            { prof::Scope p(prof::kFields); ia::fields::Update(dt, snap); }
            prof::Enter(prof::kFogField);
            ia::fogsettings::Update();
            ia::fog::Update(dt, snap);
            prof::Leave(prof::kFogField);
            { prof::Scope p(prof::kFogDraw); ia::fog::Draw(dt, snap); }
            { prof::Scope p(prof::kCover); ia::cover::Update(snap); }
            { prof::Scope p(prof::kLights); ia::lights::Update(snap); }
        }

        struct OnUpdate {
            ImmersiveAzeroth* self;
            void operator()(float delta) const { self->Update(delta); }
        };

        struct OnScriptReady {
            void operator()(wxl_script_context, void*) const { ia::lua::Register(); }
        };

        bool m_ready = false;
        bool m_envRunning = false;
        OnUpdate m_onUpdate{ this };
        OnScriptReady m_onScriptReady;
        wxl::Subscription m_update;
        wxl::Subscription m_scriptReady;
    };
}

WXL_MOD(ImmersiveAzeroth, "seyri.immersive-azeroth", "Immersive Azeroth")
