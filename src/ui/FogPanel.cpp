#include "ui/Panels.h"

#include <cstdio>
#include "World.h"
#include "fog/Far.h"
#include "fog/Field.h"
#include "fog/Fog.h"
#include "fog/FogSettings.h"
#include "fog/Wake.h"
#include "imgui.h"

namespace ia::ui {
    namespace {
        int g_depositKind = fog::kMist;
        float g_amount = 1.0f;
        float g_radius = 40.0f;

        void Kinds() {
            const fog::Stats st = fog::GetStats();

            if (!ImGui::BeginTable("##kinds", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                return;
            }

            ImGui::TableSetupColumn("kind");
            ImGui::TableSetupColumn("on");
            ImGui::TableSetupColumn("here: amount / target");
            ImGui::TableSetupColumn("pool");
            ImGui::TableSetupColumn("cells");
            ImGui::TableSetupColumn("progress");
            ImGui::TableHeadersRow();

            for (int k = 0; k < fog::kKindCount; k++) {
                fog::KindSettings& s = fog::Settings(k);
                const fog::Explanation& e = fog::Explain(k);
                ImGui::PushID(k);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(fog::KindName(k));
                ImGui::TableNextColumn();
                ImGui::Checkbox("##on", &s.enabled);
                ImGui::TableNextColumn();
                ImGui::Text("%.2f / %.2f", e.amount, e.target);
                ImGui::TableNextColumn();
                ImGui::Text("%.2f", e.pool);
                ImGui::TableNextColumn();
                ImGui::Text("%u", st.cellsWithFog[k]);
                ImGui::TableNextColumn();
                ImGui::Text("%.0f%%", fog::Progress(k) * 100.0f);
                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        void Why() {
            const fog::RulesInputs& in = fog::Inputs();

            if (!in.valid) {
                ImGui::TextDisabled("no rules step yet");
                return;
            }

            ImGui::Text("air %.1f degC (an hour ago %.1f, cooling %.2f), water %.1f degC, humidity %.2f, fogginess %.2f", in.airNow, in.airHourAgo, in.cooling,
                        in.water, in.humidity, in.fogginess);
            ImGui::Text("night %.2f, wind %.2f, rain %.2f, snow %.2f", in.night, in.wind, in.rain, in.snow);

            for (int k = 0; k < fog::kKindCount; k++) {
                const fog::Explanation& e = fog::Explain(k);
                char label[96];
                std::snprintf(label, sizeof(label), "%s: target %.2f, amount %.2f (rules %+.4f /s, wind %+.4f /s)###why%d", fog::KindName(k), e.target, e.amount,
                              e.rules, e.wind, k);

                if (!ImGui::TreeNode(label)) {
                    continue;
                }

                for (int f = 0; f < e.count; f++) {
                    const fog::Factor& x = e.factors[f];

                    if (x.factor < 0.0f) {
                        ImGui::TextDisabled("%s: %.2f", x.name, x.input);
                    } else {
                        ImGui::Text("%s%s: %.2f -> x %.2f", x.alternative ? "(or) " : "", x.name, x.input, x.factor);
                    }
                }

                ImGui::Text("slope %.0f deg, heavy share %.2f; pool %.2f heading to %.2f (rules %+.4f, wind %+.4f, flow %+.4f /s)", e.slopeDegrees, e.heavyShare,
                            e.pool, e.poolTarget, e.poolRules, e.poolWind, e.poolFlow);
                ImGui::TreePop();
            }
        }

        void KindSettings() {
            ImGui::TextDisabled("FogSettings.cdbc: %s. The values in effect where you stand; tick \"panel\" to try others (not saved).", fogsettings::Status());

            for (int k = 0; k < fog::kKindCount; k++) {
                fog::KindSettings& s = fog::Settings(k);
                ImGui::PushID(100 + k);

                if (ImGui::TreeNode(fog::KindName(k))) {
                    ImGui::Checkbox("panel's values", &s.panelOverride);

                    if (!s.panelOverride) {
                        ImGui::BeginDisabled();
                    }

                    ImGui::SliderFloat("strength", &s.strength, 0.0f, 3.0f, "%.2f");
                    ImGui::SliderFloat("density (per yd)", &s.density, 0.0f, 0.3f, "%.3f");
                    ImGui::SliderFloat("thickness (yd)", &s.thickness, 0.25f, 40.0f, "%.1f");
                    ImGui::SliderFloat("build-up (s)", &s.buildUpSeconds, 1.0f, 3600.0f, "%.0f");
                    ImGui::SliderFloat("burn-off (s)", &s.burnOffSeconds, 1.0f, 3600.0f, "%.0f");
                    ImGui::SliderFloat("wind thins at", &s.windThin, 0.0f, 1.0f, "%.2f");
                    ImGui::SliderFloat("wind drift", &s.windDrift, 0.0f, 1.0f, "%.2f");
                    ImGui::SliderFloat("patchiness", &s.patchiness, 0.0f, 1.0f, "%.2f");
                    ImGui::SliderFloat("weight (heavy share)", &s.weight, 0.0f, 1.0f, "%.2f");

                    if (!s.panelOverride) {
                        ImGui::EndDisabled();
                    }

                    ImGui::TreePop();
                }

                ImGui::PopID();
            }

            fog::FieldSettings& f = fog::Field();
            ImGui::SliderFloat("pools run downhill x", &f.poolSpeed, 0.0f, 4.0f, "%.2f");
            ImGui::SliderFloat("steep from (deg)", &f.steepStart, 10.0f, 60.0f, "%.0f");
            ImGui::SliderFloat("steep fully (deg)", &f.steepFull, 10.0f, 80.0f, "%.0f");
            ImGui::Checkbox("blend the places' settings across borders", &f.blendPlaces);
        }

        void Zones() {
            fog::ZoneState zones[32];
            const unsigned n = fog::ZoneStates(zones, 32);

            if (!ImGui::BeginTable("##fogzones", 1 + fog::kKindCount, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                return;
            }

            ImGui::TableSetupColumn("zone");

            for (int k = 0; k < fog::kKindCount; k++) {
                ImGui::TableSetupColumn(fog::KindName(k));
            }

            ImGui::TableHeadersRow();

            for (unsigned z = 0; z < n; z++) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%u %s%s", zones[z].zone, world::AreaName(zones[z].zone), zones[z].inRange ? "" : " (away)");

                for (int k = 0; k < fog::kKindCount; k++) {
                    ImGui::TableNextColumn();
                    ImGui::Text("%.2f / %.2f", zones[z].amount[k], zones[z].target[k]);
                }
            }

            ImGui::EndTable();
        }

        void Look() {
            fog::Look& look = fog::LookSettings();
            ImGui::SliderFloat("density x", &look.densityScale, 0.0f, 5.0f, "%.2f");
            ImGui::SliderFloat("brightness", &look.brightness, 0.0f, 3.0f, "%.2f");
            ImGui::SliderFloat("patchiness x", &look.noiseAmount, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("patch size (yd per tile)", &look.noiseTile, 8.0f, 256.0f, "%.0f");
            ImGui::SliderFloat("drift with the wind x", &look.drift, 0.0f, 4.0f, "%.2f");
            ImGui::SliderFloat("pools' soft top", &look.poolSoftness, 0.0f, 1.0f, "%.2f");
            ImGui::SliderInt("floor reach (cells)", &look.floorReach, 0, 4);
            ImGui::SetItemTooltip("The light layer starts on the lowest ground this many cells around, so it doesn't drape up walls.");
            ImGui::SliderFloat("reach (yd)", &look.farDistance, 50.0f, 800.0f, "%.0f");
            ImGui::SetItemTooltip("How far the volume reaches; the sky gets this much fog too.");
            ImGui::SliderFloat("extra haze (per yd)", &look.hazeDensity, 0.0f, 0.02f, "%.4f");
            ImGui::SetItemTooltip("Everywhere, on top of the fog kinds (testing). The haze kind itself comes with rain.");
            ImGui::SliderFloat("extra haze height (yd)", &look.hazeThickness, 5.0f, 200.0f, "%.0f");
        }

        void FarAndWake() {
            fog::Look& look = fog::LookSettings();
            ImGui::Checkbox("far fog (each zone's typical fog beyond the field)", &look.farFog);
            ImGui::SetItemTooltip("Mist, area fog and haze per terrain chunk out to ~1 km, from each zone's typical fog (the Zones list). "
                                  "Beyond the reach, one long step through it to the terrain.");
            ImGui::Checkbox("near green, far blue (debug)", &look.debugNearFar);
            ImGui::SetItemTooltip("Where the fog comes from: the near field green, the far fog blue, the haze grey. Mixed = the hand-over.");
            ImGui::SliderFloat("new cells fade in (s)", &look.fadeIn, 0.0f, 5.0f, "%.1f");
            ImGui::SetItemTooltip("A near cell that just started fades in over the far fog instead of switching at once (0 = at once, the old way).");
            ImGui::SliderFloat("hand-over band (yd)", &look.handover, 4.0f, 120.0f, "%.0f");
            ImGui::SetItemTooltip("The near fog fades into the far fog over this band at the near field's edge (128 yd out). "
                                  "Wider: a softer seam, less near detail at the rim. WXL used 64.");
            const fog::farmap::Stats fs = fog::farmap::GetStats();
            ImGui::TextDisabled("far map: %u chunks with ground, %u with fog; %u builds, last %.2f ms", fs.ground, fs.foggy, fs.builds, fs.buildMs);

            fog::wake::Settings& w = fog::wake::Config();
            ImGui::Checkbox("units push the fog aside", &w.enabled);

            if (w.enabled) {
                ImGui::SliderFloat("push while moving", &w.strength, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("push while standing", &w.standing, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("wake fills back in (s)", &w.trailSeconds, 0.5f, 10.0f, "%.1f");
                ImGui::SliderFloat("wake radius (x the unit's)", &w.radiusScale, 0.5f, 4.0f, "%.2f");
                ImGui::SliderFloat("wake widens (x its radius)", &w.spread, 0.0f, 3.0f, "%.2f");
                ImGui::SliderFloat("wake height (x the unit's)", &w.heightScale, 0.5f, 3.0f, "%.2f");
                ImGui::SliderFloat("fog piled at the rim", &w.rim, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("swirl", &w.swirl, 0.0f, 3.0f, "%.2f");
            }

            const fog::wake::Stats ws = fog::wake::GetStats();
            ImGui::TextDisabled("wake: %u units, %u stretches, %.3f ms CPU; map %dx%d of %.0f yd%s", ws.units, ws.segments, ws.ms, fog::wake::kSize,
                                fog::wake::kSize, fog::wake::kTexel, ws.sent ? "" : " (not sent: no fog or no units near)");
        }
    }

    void DrawFog(void*) {
        if (!fog::Available()) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", fog::Status());
            ImGui::TextDisabled("Volumetric fog needs the Vulkan renderer (-vulkan). D3D9 keeps the client's own fog. The field runs anyway.");
        } else {
            ImGui::TextDisabled("%s", fog::Status());
        }

        fog::Look& look = fog::LookSettings();
        ImGui::Checkbox("Draw the fog", &look.enabled);
        ImGui::SameLine();
        ImGui::Checkbox("magenta (where is it?)", &look.debug);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150.0f);
        static const char* const kPlaces[] = { "off", "by zone", "by area (subzones)" };
        ImGui::Combo("coloured by place", &look.debugPlaces, kPlaces, IM_ARRAYSIZE(kPlaces));
        ImGui::SetItemTooltip("Debug: each zone or area's fog in its own colour (red, green, blue, yellow, magenta, cyan, orange, white, "
                              "in the order they're met), blended where places mix. The far fog takes its zone's colour.");
        ImGui::SetNextItemWidth(160.0f);
        ImGui::SliderFloat("fog step (yd)", &look.stepLength, 0.1f, 4.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
        ImGui::SetItemTooltip("The quality: the fog is integrated in steps this long near you (growing with the distance). Smaller = smoother thin layers, more GPU time.");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::SliderInt("max steps", &look.maxSteps, 16, 512);
        ImGui::SetItemTooltip("Per pixel: the most steps a ray takes. Rings far away: raise it; slow: lower it.");
        ImGui::SameLine();
        ImGui::Checkbox("froxels (cheap, coarse)", &look.froxels);
        ImGui::SetItemTooltip("The froxel volume instead of marching every pixel: a fraction of the cost, but blocky on thin layers (to compare).");
        const fog::Stats st = fog::GetStats();
        ImGui::SameLine();
        ImGui::TextDisabled("step %.2f ms (rules %.2f), %u uploads", st.tickMs, st.rulesMs, fog::Uploads());

        Kinds();

        if (ImGui::CollapsingHeader("Why (where you stand)", ImGuiTreeNodeFlags_DefaultOpen)) {
            Why();
        }

        if (ImGui::CollapsingHeader("The kinds' settings")) {
            KindSettings();
        }

        if (ImGui::CollapsingHeader("Zones (typical fog, this session)")) {
            Zones();
        }

        if (ImGui::CollapsingHeader("Far fog and the wake (not saved)")) {
            FarAndWake();
        }

        if (ImGui::CollapsingHeader("Look (not saved)")) {
            Look();
        }

        ImGui::SeparatorText("Testing (not saved)");
        static const char* const kKinds[] = { "mist", "water fog", "steam", "area fog", "haze" };
        ImGui::SetNextItemWidth(110.0f);
        ImGui::Combo("##kind", &g_depositKind, kKinds, IM_ARRAYSIZE(kKinds));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        ImGui::SliderFloat("##amount", &g_amount, 0.05f, 1.0f, "amount %.2f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        ImGui::SliderFloat("##radius", &g_radius, 4.0f, 128.0f, "radius %.0f yd");
        ImGui::SameLine();

        if (ImGui::Button("Deposit")) {
            fog::Deposit(g_depositKind, g_amount, g_radius);
        }

        ImGui::SetItemTooltip("Fog of this kind around you; it then burns off (or stays) by the kind's rules.");

        if (ImGui::Button("Fast-forward")) {
            fog::FastForward();
        }

        ImGui::SetItemTooltip("Every cell straight to its target, as if the conditions had held for a long time.");
        ImGui::SameLine();

        if (ImGui::Button("Clear")) {
            fog::Clear();
        }

        ImGui::SameLine();
        bool paused = fog::Paused();

        if (ImGui::Checkbox("pause", &paused)) {
            fog::SetPaused(paused);
        }
    }
}
