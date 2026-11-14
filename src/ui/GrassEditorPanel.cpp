// The grass editor (kb: grass-editors): the ground effects painted on the player's map tile and the 8
// around it, by GroundEffectTexture ID (a zone's effects lie in a range, so they group by where they grow),
// each with its plant models from the stock DBCs; the selected model highlighted in the world and its
// GroundEffectDoodadWind row edited through the Data view's own form, or filled in one click from a preset
// (Plant, Stone, ...: flags and stiffness, kept as a setting); below it every GroundEffectDoodadDensity row
// that can touch the model (scoped: the ones that apply here in the order they're resolved, then the rest),
// with rows added for this place in one click.
#include "ui/Panels.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "SXC/Api/sxc_data.hpp"
#include "SXC/Api/sxc_settings.hpp"
#include "World.h"
#include "grass/Grass.h"
#include "imgui.h"
#include "wxl/sdk.hpp"

namespace ia::ui {
    namespace {
        const ImVec4 kHasRow(0.45f, 1.0f, 0.45f, 1.0f);
        const ImVec4 kNoModel(1.0f, 0.35f, 0.3f, 1.0f);

        // GroundEffectDoodadWind's flags (mods' Tables.cpp).
        constexpr uint32_t kNoWind = 0x1, kNoPush = 0x2, kNoMarks = 0x20;
        constexpr uint32_t kPresetBits = kNoWind | kNoPush | kNoMarks;

        // --- presets ([ImmersiveAzerothGrass] Presets: "name|flags|stiffness;...") ----------------------
        struct Preset {
            std::string name;
            uint32_t flags = 0;
            float stiffness = 0.0f;
        };

        // Seyri's starting set (2026-11-14): plants sway, part and burn; stones barely move and neither sway
        // nor burn; bones move a bit more than stones; mushrooms move slightly and burn but don't sway.
        constexpr const char* kDefaultPresets = "Plant|0|0;Stone|33|0.9;Bone|33|0.75;Mushroom|1|0.8";

        std::unique_ptr<sxc::settings::String> g_presetSetting;
        std::vector<Preset> g_presets;
        bool g_presetsRead = false;

        std::vector<Preset> ParsePresets(std::string_view text) {
            std::vector<Preset> out;
            size_t start = 0;

            while (start < text.size()) {
                size_t end = text.find(';', start);
                end = end == std::string_view::npos ? text.size() : end;
                const std::string item(text.substr(start, end - start));
                start = end + 1;
                const size_t a = item.find('|'), b = a == std::string::npos ? a : item.find('|', a + 1);

                if (a == std::string::npos || b == std::string::npos || a == 0) {
                    continue;
                }

                Preset p;
                p.name = item.substr(0, a);
                p.flags = static_cast<uint32_t>(std::strtoul(item.c_str() + a + 1, nullptr, 10)) & kPresetBits;
                p.stiffness = std::min(std::max(static_cast<float>(std::atof(item.c_str() + b + 1)), 0.0f), 1.0f);
                out.push_back(p);
            }

            return out;
        }

        void SavePresets() {
            std::string text;
            char number[48];

            for (const Preset& p : g_presets) {
                std::string name = p.name;
                name.erase(std::remove_if(name.begin(), name.end(), [](char c) { return c == '|' || c == ';'; }), name.end());
                std::snprintf(number, sizeof number, "|%u|%.3g", p.flags, p.stiffness);
                text += (text.empty() ? "" : ";") + (name.empty() ? std::string("Preset") : name) + number;
            }

            if (g_presetSetting) {
                g_presetSetting->Set(text.c_str());
            }
        }

        std::vector<Preset>& Presets() {
            if (!g_presetsRead && sxc::settings::Api()) {
                g_presetSetting = std::make_unique<sxc::settings::String>("ImmersiveAzerothGrass", "Presets", kDefaultPresets, "Grass editor: presets",
                                                                          "The grass editor's presets: name|flags|stiffness, separated by ';' "
                                                                          "(flags: 1 no wind, 2 no push, 32 no marks).");
                g_presets = ParsePresets(g_presetSetting->Get());
                g_presetsRead = true;
            }

            if (!g_presetsRead && g_presets.empty()) {
                g_presets = ParsePresets(kDefaultPresets);
            }

            return g_presets;
        }

        // --- the client and the table ----------------------------------------------------------------
        const sxc_grass_api* EditorApi() {
            const sxc_grass_api* api = grass::Api();
            return api && api->struct_size >= offsetof(sxc_grass_api, set_highlight) + sizeof(api->set_highlight) ? api : nullptr;
        }

        const sxc_data_api* DataApi() {
            const sxc_data_api* api = sxc::data::Api();
            return api && api->struct_size >= offsetof(sxc_data_api, open_row) + sizeof(api->open_row) ? api : nullptr;
        }

        sxc::data::Table Wind() {
            static sxc::data::Table t;

            if (!t) {
                t = sxc::data::Table::Get("GroundEffectDoodadWind");
            }

            return t;
        }

        sxc::data::Table Density() {
            static sxc::data::Table t;

            if (!t) {
                t = sxc::data::Table::Get("GroundEffectDoodadDensity");
            }

            return t;
        }

        // The row for a plant model, added with the automatic values when `create` (ID = the doodad's).
        sxc::data::Row RowFor(const sxc_data_api* api, uint32_t doodad, bool create) {
            sxc::data::Table t = Wind();
            sxc::data::Row r = t ? t.Find(doodad) : sxc::data::Row();

            if (r || !create || !t) {
                return r;
            }

            const int32_t index = api->add_row(t.Handle());

            if (index < 0) {
                return sxc::data::Row();
            }

            r = t.At(static_cast<uint32_t>(index));
            r.SetRaw(0, 0, doodad);
            r.SetRaw(t.Field("Flags"), 0, 0);
            r.SetFloat(t.Field("Stiffness"), 0, 0.0f);
            r.SetFloat(t.Field("RootV"), 0, -1.0f);
            r.SetFloat(t.Field("TipV"), 0, -1.0f);
            return t.Find(doodad);
        }

        void Apply(const sxc_data_api* api, uint32_t doodad, const Preset& p) {
            const sxc::data::Row r = RowFor(api, doodad, true);
            const sxc::data::Table t = Wind();

            if (!r) {
                return;
            }

            const int flags = t.Field("Flags");
            r.SetRaw(flags, 0, (r.Uint(flags) & ~kPresetBits) | p.flags);
            r.SetFloat(t.Field("Stiffness"), 0, p.stiffness);
        }

        // --- the panel's state -----------------------------------------------------------------------
        std::vector<sxc_grass_effect> g_effects;
        double g_listed = -10.0;
        uint32_t g_selected = 0;       // a GroundEffectDoodad ID
        uint32_t g_selectedEffect = 0; // the GroundEffectTexture it was picked under
        char g_filter[96] = "";
        bool g_onlyWithoutRow = false;
        bool g_highlight = true;
        float g_colour[4] = { 1.0f, 0.1f, 0.9f, 1.0f };
        char g_message[256] = "";

        std::string Lower(std::string s) {
            for (char& c : s) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }

            return s;
        }

        std::string FileName(const char* path) {
            const std::string s = path ? path : "";
            const size_t slash = s.find_last_of("\\/");
            return slash == std::string::npos ? s : s.substr(slash + 1);
        }

        void TableLine(const sxc_data_api* api, const sxc::data::Table& t, const char* label) {
            if (!t) {
                ImGui::TextColored(kNoModel, "%s isn't registered.", label);
                return;
            }

            ImGui::PushID(label);
            const bool edited = api->has_changes(t.Handle()) != WXL_FALSE;
            ImGui::Text("%s: %u rows%s", label, t.RowCount(), edited ? " (edited, not saved)" : "");
            ImGui::SameLine();
            ImGui::BeginDisabled(!api->can_undo(t.Handle()));

            if (ImGui::SmallButton("Undo")) {
                api->undo(t.Handle());
            }

            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!api->can_redo(t.Handle()));

            if (ImGui::SmallButton("Redo")) {
                api->redo(t.Handle());
            }

            ImGui::EndDisabled();
            ImGui::SameLine();

            if (ImGui::SmallButton("Save")) {
                char error[200] = "";
                if (api->save(t.Handle(), error, sizeof error)) {
                    std::snprintf(g_message, sizeof g_message, "%s saved", label);
                } else {
                    std::snprintf(g_message, sizeof g_message, "%s not saved: %s", label, error);
                }
            }

            ImGui::SameLine();

            if (ImGui::SmallButton("Reload")) {
                api->reload(t.Handle());
                std::snprintf(g_message, sizeof g_message, "%s reloaded from disk", label);
            }

            ImGui::SetItemTooltip("Reads the file again: unsaved edits are lost.");
            ImGui::PopID();
        }

        // A short summary of a row: what the presets set.
        std::string RowSummary(const sxc::data::Row& r) {
            const sxc::data::Table t = Wind();
            const uint32_t flags = r.Uint(t.Field("Flags"));
            char text[96];
            std::snprintf(text, sizeof text, "%s%s%sstiff %.2f", flags & kNoWind ? "no wind, " : "", flags & kNoPush ? "no push, " : "",
                          flags & kNoMarks ? "no marks, " : "", r.Float(t.Field("Stiffness")));

            // A preset it matches by name.
            for (const Preset& p : Presets()) {
                if ((flags & kPresetBits) == p.flags && std::abs(r.Float(t.Field("Stiffness")) - p.stiffness) < 0.005f) {
                    return p.name + " (" + text + ")";
                }
            }

            return text;
        }

        void PresetButtons(const sxc_data_api* api, uint32_t doodad) {
            std::vector<Preset>& presets = Presets();

            for (size_t i = 0; i < presets.size(); i++) {
                if (i) {
                    ImGui::SameLine();
                }

                ImGui::PushID(static_cast<int>(i));

                if (ImGui::Button(presets[i].name.c_str())) {
                    Apply(api, doodad, presets[i]);
                    std::snprintf(g_message, sizeof g_message, "%u set as %s", doodad, presets[i].name.c_str());
                }

                ImGui::SetItemTooltip("%s%s%sstiffness %.2f", presets[i].flags & kNoWind ? "no wind, " : "sways, ",
                                      presets[i].flags & kNoPush ? "no push, " : "pushed, ", presets[i].flags & kNoMarks ? "doesn't burn, " : "burns, ",
                                      presets[i].stiffness);
                ImGui::PopID();
            }
        }

        void EditPresets() {
            if (!ImGui::TreeNode("Presets")) {
                return;
            }

            std::vector<Preset>& presets = Presets();
            bool changed = false;
            int remove = -1;

            for (size_t i = 0; i < presets.size(); i++) {
                Preset& p = presets[i];
                ImGui::PushID(static_cast<int>(i));
                char name[48];
                std::snprintf(name, sizeof name, "%s", p.name.c_str());
                ImGui::SetNextItemWidth(110.0f);

                if (ImGui::InputText("##name", name, sizeof name)) {
                    p.name = name;
                }

                changed |= ImGui::IsItemDeactivatedAfterEdit();
                bool wind = !(p.flags & kNoWind), push = !(p.flags & kNoPush), burns = !(p.flags & kNoMarks);
                ImGui::SameLine();
                changed |= ImGui::Checkbox("sways", &wind);
                ImGui::SameLine();
                changed |= ImGui::Checkbox("pushed", &push);
                ImGui::SameLine();
                changed |= ImGui::Checkbox("burns", &burns);
                p.flags = (wind ? 0 : kNoWind) | (push ? 0 : kNoPush) | (burns ? 0 : kNoMarks);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(120.0f);
                ImGui::SliderFloat("##stiff", &p.stiffness, 0.0f, 1.0f, "stiff %.2f");
                changed |= ImGui::IsItemDeactivatedAfterEdit();
                ImGui::SetItemTooltip("0 flexible .. 1 rigid: scales how far the wind and characters move it.");
                ImGui::SameLine();

                if (ImGui::SmallButton("x")) {
                    remove = static_cast<int>(i);
                }

                ImGui::PopID();
            }

            if (remove >= 0) {
                presets.erase(presets.begin() + remove);
                changed = true;
            }

            if (ImGui::SmallButton("Add a preset")) {
                presets.push_back(Preset{ "New", 0, 0.0f });
                changed = true;
            }

            ImGui::SameLine();

            if (ImGui::SmallButton("Starting set")) {
                presets = ParsePresets(kDefaultPresets);
                changed = true;
            }

            if (changed) {
                SavePresets();
            }

            ImGui::TreePop();
        }

        // --- density (GroundEffectDoodadDensity: scoped; mods' grass/Grass.cpp resolves it) ---------------
        enum : uint32_t { kGlobal = 0, kMap = 1, kArea = 2 };

        // How specific a row's place is where the player stands (higher first), -1 where it doesn't apply.
        int PlaceRank(uint32_t type, int32_t id, const world::Snapshot& s) {
            if (type == kGlobal) {
                return 0;
            }

            if (type == kMap) {
                return id == s.mapId ? 1 : -1;
            }

            for (int k = 0; k < s.areaCount; k++) {
                if (s.areaChain[k] == static_cast<uint32_t>(id)) {
                    return 2 + (s.areaCount - 1 - k);
                }
            }

            return -1;
        }

        std::string PlaceName(uint32_t type, int32_t id) {
            char text[128];

            if (type == kGlobal) {
                return "everywhere";
            }

            if (type == kMap) {
                std::snprintf(text, sizeof text, "map %d", id);
            } else {
                const char* name = world::AreaName(static_cast<uint32_t>(id));
                std::snprintf(text, sizeof text, "%s (area %d)", name && name[0] ? name : "?", id);
            }

            return text;
        }

        void AddDensityRow(const sxc_data_api* api, uint32_t type, int32_t id, uint32_t doodad, uint32_t effect) {
            const sxc::data::Table t = Density();
            const int32_t index = t ? api->add_row(t.Handle()) : -1;

            if (index < 0) {
                return;
            }

            // Every value inherited (-1) until edited: the row changes nothing yet.
            const sxc::data::Row r = t.At(static_cast<uint32_t>(index));
            r.SetRaw(t.Field("ScopeType"), 0, type);
            r.SetRaw(t.Field("ScopeID"), 0, static_cast<uint32_t>(type == kGlobal ? 0 : id));
            r.SetRaw(t.Field("DoodadID"), 0, doodad);
            r.SetRaw(t.Field("GroundEffectID"), 0, effect);
            r.SetFloat(t.Field("Multiplier"), 0, -1.0f);
            r.SetFloat(t.Field("Radius"), 0, -1.0f);
            r.SetFloat(t.Field("Spread"), 0, -1.0f);
            std::snprintf(g_message, sizeof g_message, "density row %u added (%s): set its values below", r.Id(), PlaceName(type, id).c_str());
        }

        void DensityRows(const sxc_data_api* api) {
            const sxc::data::Table t = Density();
            ImGui::SeparatorText("Density (GroundEffectDoodadDensity)");

            if (!t) {
                ImGui::TextColored(kNoModel, "GroundEffectDoodadDensity isn't registered.");
                return;
            }

            const world::Snapshot& s = world::Current();
            const int fType = t.Field("ScopeType"), fId = t.Field("ScopeID"), fDoodad = t.Field("DoodadID"), fEffect = t.Field("GroundEffectID");

            // The rows that can touch this model under this effect: the model's or every model's, the effect's or
            // any. Those that apply here in the order they're read (place first, then the most specific "what";
            // the first value of each field that isn't -1 wins), then the rest.
            struct Hit {
                uint32_t index;
                int rank;   // place * 4 + what; -1: not here
            };
            std::vector<Hit> hits;

            for (uint32_t i = 0; i < t.RowCount(); i++) {
                const sxc::data::Row r = t.At(i);
                const uint32_t doodad = r.Uint(fDoodad), effect = r.Uint(fEffect);

                if ((doodad && doodad != g_selected) || (effect && effect != g_selectedEffect)) {
                    continue;
                }

                const int place = PlaceRank(r.Uint(fType), r.Int(fId), s);
                const int what = doodad && effect ? 3 : (doodad ? 2 : (effect ? 1 : 0));
                hits.push_back(Hit{ i, place < 0 ? -1 : place * 4 + what });
            }

            std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.rank > b.rank; });
            ImGui::TextWrapped("Rows for model %u under effect %u: those that apply here first, in the order they're read (the first value "
                               "that isn't -1 wins, field by field).", g_selected, g_selectedEffect);
            int order = 0;

            for (const Hit& h : hits) {
                const sxc::data::Row r = t.At(h.index);
                const uint32_t doodad = r.Uint(fDoodad), effect = r.Uint(fEffect);
                char what[64];
                std::snprintf(what, sizeof what, "%s, %s", doodad ? "this model" : "every model", effect ? "this effect" : "any effect");
                char label[200];

                if (h.rank >= 0) {
                    std::snprintf(label, sizeof label, "%d. row %u: %s; %s###d%u", ++order, r.Id(), PlaceName(r.Uint(fType), r.Int(fId)).c_str(), what,
                                  h.index);
                } else {
                    std::snprintf(label, sizeof label, "(not here) row %u: %s; %s###d%u", r.Id(), PlaceName(r.Uint(fType), r.Int(fId)).c_str(), what,
                                  h.index);
                }

                if (h.rank < 0) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                }

                const bool open = ImGui::TreeNodeEx(label, h.rank >= 0 && order == 1 ? ImGuiTreeNodeFlags_DefaultOpen : 0);

                if (h.rank < 0) {
                    ImGui::PopStyleColor();
                }

                if (open) {
                    api->draw_row_form(t.Handle(), h.index);
                    ImGui::TreePop();
                }
            }

            if (hits.empty()) {
                ImGui::TextDisabled("None: no copies for this model.");
            }

            // New rows for the places the player is in.
            static bool forEffect = false;
            ImGui::Checkbox("New rows for this effect only", &forEffect);
            ImGui::SetItemTooltip("Off: the model under any ground effect. On: only where effect %u paints it.", g_selectedEffect);
            ImGui::TextUnformatted("Add a row for this model:");
            const uint32_t effect = forEffect ? g_selectedEffect : 0;

            for (int k = 0; k < s.areaCount; k++) {
                ImGui::SameLine();
                ImGui::PushID(k);
                const char* name = world::AreaName(s.areaChain[k]);

                if (ImGui::SmallButton(name && name[0] ? name : "area")) {
                    AddDensityRow(api, kArea, static_cast<int32_t>(s.areaChain[k]), g_selected, effect);
                }

                ImGui::SetItemTooltip("%s %u", k == 0 && s.areaCount > 1 ? "Sub-area" : "Area", s.areaChain[k]);
                ImGui::PopID();
            }

            if (s.mapId >= 0) {
                ImGui::SameLine();

                if (ImGui::SmallButton("this map")) {
                    AddDensityRow(api, kMap, s.mapId, g_selected, effect);
                }
            }

            ImGui::SameLine();

            if (ImGui::SmallButton("everywhere")) {
                AddDensityRow(api, kGlobal, 0, g_selected, effect);
            }
        }

        void Selected(const sxc_grass_api* grassApi, const sxc_data_api* api) {
            if (!g_selected) {
                ImGui::TextDisabled("Select a plant model above.");
                return;
            }

            const char* path = grassApi->doodad_path(g_selected);
            const std::string shown = path ? path : "(no row in GroundEffectDoodad.dbc)";
            ImGui::SeparatorText("Selected");
            ImGui::TextWrapped("%u: %s", g_selected, shown.c_str());

            sxc_grass_doodad_info info = {};
            info.struct_size = sizeof(info);

            if (grassApi->doodad_info(g_selected, &info) && info.analyzed) {
                ImGui::TextDisabled("Automatic: %.2f yd tall%s, root V %.2f, tip V %.2f", info.height, info.flat ? " (flat: doesn't sway)" : "", info.root_v,
                                    info.tip_v);
            }

            PresetButtons(api, g_selected);
            const sxc::data::Row r = RowFor(api, g_selected, false);

            if (r) {
                api->draw_row_form(Wind().Handle(), r.Index());
            } else {
                ImGui::TextDisabled("No row: the automatic values.");
                ImGui::SameLine();

                if (ImGui::SmallButton("Add a row")) {
                    RowFor(api, g_selected, true);
                }
            }

            DensityRows(api);
        }
    }

    void DrawGrassEditor(void*) {
        const sxc_grass_api* grassApi = EditorApi();
        const sxc_data_api* api = DataApi();

        if (!grassApi || !api) {
            ImGui::TextColored(kNoModel, "This client lacks the grass editor's calls (sxc.grass effects_near, sxc.data editing): update it.");
            return;
        }

        TableLine(api, Wind(), "GroundEffectDoodadWind");
        TableLine(api, Density(), "GroundEffectDoodadDensity");

        if (g_message[0]) {
            ImGui::TextDisabled("%s", g_message);
        }

        // The list, refreshed every second (nine tiles' chunk layers).
        const double now = ImGui::GetTime();

        if (now - g_listed > 1.0) {
            g_listed = now;
            g_effects.resize(256);
            const uint32_t n = grassApi->effects_near(g_effects.data(), static_cast<uint32_t>(g_effects.size()));
            g_effects.resize(std::min<uint32_t>(n, 256));
        }

        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputTextWithHint("##filter", "filter: model or ID", g_filter, sizeof g_filter);
        ImGui::SameLine();
        ImGui::Checkbox("Only without a row", &g_onlyWithoutRow);
        ImGui::Checkbox("Highlight the selected model", &g_highlight);
        ImGui::SameLine();
        ImGui::ColorEdit4("##colour", g_colour, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
        ImGui::SetItemTooltip("Its plants in this colour (alpha: how strongly) while this panel is open.");

        if (g_highlight && g_selected) {
            grassApi->set_highlight(g_selected, g_colour);
        }

        ImGui::Text("Ground effects on this tile and the 8 around: %u", static_cast<unsigned>(g_effects.size()));
        const std::string filter = Lower(g_filter);
        const sxc::data::Table t = Wind();

        if (ImGui::BeginTable("effects", 4, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable,
                              ImVec2(0.0f, 320.0f))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Effect / model", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Weight", ImGuiTableColumnFlags_WidthFixed, 50.0f);
            ImGui::TableSetupColumn("Row", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 50.0f);
            ImGui::TableHeadersRow();

            for (const sxc_grass_effect& e : g_effects) {
                // Its models that pass the filter.
                struct Shown {
                    int slot;
                    uint32_t doodad, weight;
                    std::string file;
                    sxc::data::Row row;
                };
                std::vector<Shown> shown;

                for (int d = 0; d < 4; d++) {
                    if (!e.doodads[d]) {
                        continue;
                    }

                    const char* path = grassApi->doodad_path(e.doodads[d]);
                    Shown s{ d, e.doodads[d], e.weights[d], path ? FileName(path) : std::string(), t ? t.Find(e.doodads[d]) : sxc::data::Row() };
                    char id[16];
                    std::snprintf(id, sizeof id, "%u", s.doodad);
                    const bool passes = filter.empty() || Lower(path ? path : "").find(filter) != std::string::npos || std::string(id) == filter ||
                                        std::to_string(e.effect) == filter;

                    if (passes && !(g_onlyWithoutRow && s.row)) {
                        shown.push_back(s);
                    }
                }

                if (shown.empty()) {
                    continue;
                }

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(static_cast<int>(e.effect));
                const bool open = ImGui::TreeNodeEx("##effect", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAllColumns, "Effect %u", e.effect);
                ImGui::TableNextColumn();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%u chunks, density %u", e.chunks, e.density);
                ImGui::TableNextColumn();

                if (open) {
                    for (const Shown& s : shown) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::PushID(s.slot);   // an effect may list one model twice
                        const ImVec4 colour = s.file.empty() ? kNoModel : (s.row ? kHasRow : ImGui::GetStyleColorVec4(ImGuiCol_Text));
                        ImGui::PushStyleColor(ImGuiCol_Text, colour);

                        if (ImGui::Selectable(s.file.empty() ? "(no model)" : s.file.c_str(), g_selected == s.doodad && g_selectedEffect == e.effect,
                                              ImGuiSelectableFlags_SpanAllColumns)) {
                            g_selected = s.doodad;
                            g_selectedEffect = e.effect;
                        }

                        ImGui::PopStyleColor();
                        ImGui::TableNextColumn();
                        ImGui::Text("%u", s.weight);
                        ImGui::TableNextColumn();

                        if (s.row) {
                            ImGui::TextUnformatted(RowSummary(s.row).c_str());
                        } else {
                            ImGui::TextDisabled("automatic");
                        }

                        ImGui::TableNextColumn();
                        ImGui::Text("%u", s.doodad);
                        ImGui::PopID();
                    }

                    ImGui::TreePop();
                }

                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        Selected(grassApi, api);
        EditPresets();
    }
}
