// The light editor (port plan 6c; WXL's debug/LightEditor): the models near the player with their
// attachment points, particle emitters, own lights and bones; their DoodadLightAssignment rows and every
// DoodadLightProperties row, edited live through the Data view's own forms (its undo, revert and row
// windows), saved back to the files or reloaded. Markers in the world while the panel is open: the
// lights as drawn (a cross in their colour, rings at their radius), the selected model's points, and a
// red mark where a row's attachment is missing (the light sits at the model's origin).
#include "ui/Panels.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "SXC/Api/sxc_data.hpp"
#include "SXC/Api/sxc_lights.h"
#include "World.h"
#include "imgui.h"
#include "lights/Lights.h"

namespace ia::ui {
    namespace {
        const ImVec4 kOurs(0.45f, 1.0f, 0.45f, 1.0f);    // has our light (an assignment row)
        const ImVec4 kStock(1.0f, 0.8f, 0.35f, 1.0f);    // only its own lights
        const ImVec4 kMissing(1.0f, 0.35f, 0.3f, 1.0f);  // a row whose attachment the model lacks

        float g_range = 25.0f;
        char g_search[96] = "";
        bool g_category[5] = { true, true, true, true, true };
        const char* const kCategories[5] = { "item", "creature", "character", "world", "other" };
        std::string g_selected;          // normalised path
        int g_markers = 1;               // 0 off, 1 the selected model's lights, 2 every light within the range
        bool g_rings = true;
        bool g_points = true;            // the selected model's attachment points, emitters, own lights
        bool g_bones = false;            // ... and its bones
        uint32_t g_focusRow = 0;         // the assignment whose attachment is highlighted
        char g_message[256] = "";

        std::string Normalize(const char* path) {
            std::string s = path ? path : "";

            for (char& c : s) {
                c = c == '/' ? '\\' : (c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
            }

            return s;
        }

        int CategoryOf(const std::string& normalized) {
            static const char* const prefixes[4] = { "item\\", "creature\\", "character\\", "world\\" };

            for (int c = 0; c < 4; c++) {
                if (normalized.compare(0, std::strlen(prefixes[c]), prefixes[c]) == 0) {
                    return c;
                }
            }

            return 4;
        }

        // The data service with the editing calls (a newer client).
        const sxc_data_api* DataApi() {
            const sxc_data_api* api = sxc::data::Api();
            return api && api->struct_size >= offsetof(sxc_data_api, open_row) + sizeof(api->open_row) ? api : nullptr;
        }

        struct Model {
            std::string path;        // as the client names it
            std::string key;         // normalised
            std::string parent;
            float pos[3] = {};       // the nearest instance's origin
            float distance = 1e9f;
            uint32_t placed = 0, bones = 0, attachments = 0, emitters = 0, ownLights = 0;
            bool animated = true;
        };

        // The models near the player, each file once (its nearest instance).
        std::vector<Model> NearbyModels(const sxc_lights_api* api, const world::Snapshot& s) {
            static std::vector<sxc_lights_model> raw(4096);
            const uint32_t n = api->models_near(s.playerPos, g_range, raw.data(), static_cast<uint32_t>(raw.size()));
            std::map<std::string, Model> byKey;

            for (uint32_t i = 0; i < n; i++) {
                const sxc_lights_model& r = raw[i];
                const std::string key = Normalize(r.path);

                if (key.empty()) {
                    continue;
                }

                Model& m = byKey[key];

                if (m.path.empty()) {
                    m.path = r.path;
                    m.key = key;
                    m.parent = r.parent_path ? r.parent_path : "";
                    std::memcpy(m.pos, r.pos, sizeof m.pos);
                    m.distance = r.distance;
                    m.bones = r.bones;
                    m.attachments = r.attachments;
                    m.emitters = r.emitters;
                    m.ownLights = r.own_lights;
                    m.animated = r.animated != WXL_FALSE;
                }

                m.placed++;
            }

            std::vector<Model> out;

            for (auto& [key, m] : byKey) {
                out.push_back(m);
            }

            std::sort(out.begin(), out.end(), [](const Model& a, const Model& b) { return a.distance < b.distance; });
            return out;
        }

        // --- the tables ----------------------------------------------------------------------------
        struct Tables {
            sxc::data::Table lights = sxc::data::Table::Get("DoodadLightProperties");
            sxc::data::Table rows = sxc::data::Table::Get("DoodadLightAssignment");
        };

        Tables& T() {
            static Tables t;

            if (!t.lights) {
                t.lights = sxc::data::Table::Get("DoodadLightProperties");
            }

            if (!t.rows) {
                t.rows = sxc::data::Table::Get("DoodadLightAssignment");
            }

            return t;
        }

        // The assignment rows of a model (ids), from an index rebuilt when the table changes.
        std::vector<uint32_t> RowsOf(const std::string& key) {
            static std::unordered_map<std::string, std::vector<uint32_t>> byKey;
            static uint32_t generation = 0;
            const sxc::data::Table& t = T().rows;

            if (t.Generation() != generation) {
                generation = t.Generation();
                byKey.clear();
                const int path = t.Field("ModelPath");

                for (uint32_t i = 0; i < t.RowCount(); i++) {
                    const sxc::data::Row r = t.At(i);
                    byKey[Normalize(std::string(r.String(path)).c_str())].push_back(r.Id());
                }
            }

            const auto it = byKey.find(key);
            return it != byKey.end() ? it->second : std::vector<uint32_t>();
        }

        std::string LightName(uint32_t id) {
            if (!id) {
                return "no light";
            }

            const sxc::data::Row r = T().lights.Find(id);
            return r ? std::to_string(id) + " " + std::string(r.String(T().lights.Field("Name"))) : std::to_string(id) + " (missing)";
        }

        // One table's line: its state, undo / redo, save, reload.
        void TableLine(const sxc_data_api* api, const char* label, const sxc::data::Table& t) {
            ImGui::PushID(label);
            const bool edited = api->has_changes(t.Handle()) != WXL_FALSE;
            ImGui::Text("%s: %u rows%s", label, t.RowCount(), edited ? " (edited, not saved)" : "");

            if (api->changed_on_disk(t.Handle())) {
                ImGui::SameLine();
                ImGui::TextColored(kMissing, "changed on disk");
            }

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
                std::snprintf(g_message, sizeof g_message, api->save(t.Handle(), error, sizeof error) ? "%s saved" : "%s not saved: %s", label, error);
            }

            ImGui::SameLine();

            if (ImGui::SmallButton("Reload")) {
                api->reload(t.Handle());
                std::snprintf(g_message, sizeof g_message, "%s reloaded from disk", label);
            }

            ImGui::SetItemTooltip("Reads the file again: unsaved edits are lost.");
            ImGui::PopID();
        }

        // --- the selected model --------------------------------------------------------------------
        const char* const kAttachTypes[4] = { "origin", "attachment point", "bone", "particle emitter" };

        // What a row of the model may hang on, as the attach index combo lists it.
        struct Choice {
            int32_t index;
            std::string label;
        };

        std::vector<Choice> ChoicesFor(uint32_t type, const std::vector<sxc_lights_point>& points, uint32_t bones) {
            std::vector<Choice> out;
            char label[64];

            if (type == SXC_LIGHTS_ATTACH_BONE) {
                for (uint32_t b = 0; b < bones; b++) {
                    std::snprintf(label, sizeof label, "bone %u", b);
                    out.push_back({ static_cast<int32_t>(b), label });
                }

                return out;
            }

            for (const sxc_lights_point& p : points) {
                if (p.kind != type) {
                    continue;
                }

                std::snprintf(label, sizeof label, type == SXC_LIGHTS_ATTACH_POINT ? "attachment ID %d (bone %u)" : "emitter %d (bone %u)", p.index, p.bone);
                out.push_back({ p.index, label });
            }

            return out;
        }

        void EditRows(const sxc_data_api* api, const Model& m, const std::vector<sxc_lights_point>& points, const std::unordered_set<uint32_t>& missing) {
            sxc::data::Table& t = T().rows;
            const int fType = t.Field("AttachType"), fIndex = t.Field("AttachIndex"), fLight = t.Field("LightID"), fFlags = t.Field("Flags");

            for (uint32_t id : RowsOf(m.key)) {
                const sxc::data::Row r = t.Find(id);

                if (!r) {
                    continue;
                }

                ImGui::PushID(static_cast<int>(id));
                const uint32_t type = r.Uint(fType);
                const bool lacks = missing.count(id) != 0;
                char header[200];
                std::snprintf(header, sizeof header, "Row %u: %s at %s %d%s%s###row", id, LightName(r.Uint(fLight)).c_str(), type < 4 ? kAttachTypes[type] : "?",
                              r.Int(fIndex), (r.Uint(fFlags) & SXC_LIGHTS_SUPPRESS_MODEL_LIGHTS) ? ", own lights off" : "", lacks ? "  (MISSING: at the origin)" : "");

                if (lacks) {
                    ImGui::PushStyleColor(ImGuiCol_Text, kMissing);
                }

                const bool open = ImGui::CollapsingHeader(header);

                if (lacks) {
                    ImGui::PopStyleColor();
                }

                if (!open) {
                    ImGui::PopID();
                    continue;
                }

                g_focusRow = id;

                // The light-specific pickers; every field below in the Data view's form.
                int attach = static_cast<int>(type < 4 ? type : 0);
                ImGui::SetNextItemWidth(220.0f);

                if (ImGui::Combo("Attach to", &attach, kAttachTypes, 4)) {
                    r.SetRaw(fType, 0, static_cast<uint32_t>(attach));
                    const std::vector<Choice> first = ChoicesFor(static_cast<uint32_t>(attach), points, m.bones);
                    r.SetRaw(fIndex, 0, static_cast<uint32_t>(first.empty() ? 0 : first.front().index));
                }

                if (type != SXC_LIGHTS_ATTACH_ORIGIN) {
                    const std::vector<Choice> choices = ChoicesFor(type, points, m.bones);
                    const int32_t current = r.Int(fIndex);
                    std::string preview = std::to_string(current) + " (not on this model)";

                    for (const Choice& c : choices) {
                        if (c.index == current) {
                            preview = c.label;
                        }
                    }

                    ImGui::SetNextItemWidth(220.0f);

                    if (ImGui::BeginCombo("On", preview.c_str())) {
                        for (const Choice& c : choices) {
                            if (ImGui::Selectable(c.label.c_str(), c.index == current)) {
                                r.SetRaw(fIndex, 0, static_cast<uint32_t>(c.index));
                            }
                        }

                        if (choices.empty()) {
                            ImGui::TextDisabled("the model has none");
                        }

                        ImGui::EndCombo();
                    }
                }

                const uint32_t light = r.Uint(fLight);
                ImGui::SetNextItemWidth(220.0f);

                if (ImGui::BeginCombo("Light", LightName(light).c_str())) {
                    if (ImGui::Selectable("no light (suppress only)", light == 0)) {
                        r.SetRaw(fLight, 0, 0);
                    }

                    const sxc::data::Table& lt = T().lights;

                    for (uint32_t i = 0; i < lt.RowCount(); i++) {
                        const uint32_t lid = lt.At(i).Id();

                        if (ImGui::Selectable(LightName(lid).c_str(), lid == light)) {
                            r.SetRaw(fLight, 0, lid);
                        }
                    }

                    ImGui::EndCombo();
                }

                if (ImGui::SmallButton("Open in the Data view")) {
                    api->open_row(t.Handle(), id);
                }

                ImGui::SameLine();

                if (ImGui::SmallButton("Clone")) {
                    api->clone_row(t.Handle(), r.Index());
                }

                ImGui::SameLine();

                if (ImGui::SmallButton("Delete")) {
                    api->remove_row(t.Handle(), r.Index());
                    ImGui::PopID();
                    break;
                }

                if (ImGui::TreeNode("All fields")) {
                    api->draw_row_form(t.Handle(), r.Index());
                    ImGui::TreePop();
                }

                ImGui::PopID();
            }

            if (ImGui::Button("Add a light to this model")) {
                const int32_t index = api->add_row(t.Handle());

                if (index >= 0) {
                    const sxc::data::Row r = t.At(static_cast<uint32_t>(index));
                    r.SetString(t.Field("ModelPath"), 0, m.path.c_str());

                    // A useful start: the first particle emitter (a flame), else the first attachment point, else the origin.
                    const std::vector<Choice> emitters = ChoicesFor(SXC_LIGHTS_ATTACH_EMITTER, points, m.bones);
                    const std::vector<Choice> attachments = ChoicesFor(SXC_LIGHTS_ATTACH_POINT, points, m.bones);
                    const uint32_t type = !emitters.empty() ? SXC_LIGHTS_ATTACH_EMITTER : (!attachments.empty() ? SXC_LIGHTS_ATTACH_POINT : SXC_LIGHTS_ATTACH_ORIGIN);
                    r.SetRaw(fType, 0, type);
                    r.SetRaw(fIndex, 0, static_cast<uint32_t>(!emitters.empty() ? emitters.front().index : (!attachments.empty() ? attachments.front().index : 0)));
                    r.SetFloat(t.Field("Direction"), 2, -1.0f);

                    if (T().lights.RowCount()) {
                        r.SetRaw(fLight, 0, T().lights.At(0).Id());
                    }

                    g_focusRow = r.Id();
                }
            }
        }

        // --- DoodadLightProperties -----------------------------------------------------------------
        void EditLights(const sxc_data_api* api) {
            sxc::data::Table& t = T().lights;
            const int fName = t.Field("Name"), fType = t.Field("Type"), fColor = t.Field("Color"), fRadius = t.Field("Radius");
            static char filter[64] = "";
            ImGui::SetNextItemWidth(220.0f);
            ImGui::InputTextWithHint("##lightfilter", "filter by name or id", filter, sizeof filter);
            const std::string wanted = Normalize(filter);

            for (uint32_t i = 0; i < t.RowCount(); i++) {
                const sxc::data::Row r = t.At(i);
                const uint32_t id = r.Id();
                const std::string name(r.String(fName));

                if (!wanted.empty() && Normalize((std::to_string(id) + " " + name).c_str()).find(wanted) == std::string::npos) {
                    continue;
                }

                ImGui::PushID(static_cast<int>(id));
                const uint32_t c = r.Uint(fColor);
                ImGui::ColorButton("##swatch", ImVec4(((c >> 16) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, (c & 0xFF) / 255.0f, 1.0f), ImGuiColorEditFlags_NoTooltip,
                                   ImVec2(14.0f, 14.0f));
                ImGui::SameLine();
                const float radius = r.Float(fRadius);
                char header[160];
                std::snprintf(header, sizeof header, "%u %s (%s, %s)###light", id, name.c_str(), r.Uint(fType) == 1 ? "spot" : "point",
                              radius > 0.0f ? (std::to_string(static_cast<int>(radius + 0.5f)) + " yd").c_str() : "default radius");

                if (ImGui::CollapsingHeader(header)) {
                    if (ImGui::SmallButton("Open in the Data view")) {
                        api->open_row(t.Handle(), id);
                    }

                    ImGui::SameLine();

                    if (ImGui::SmallButton("Clone")) {
                        api->clone_row(t.Handle(), i);
                    }

                    ImGui::SameLine();

                    if (ImGui::SmallButton("Delete")) {
                        api->remove_row(t.Handle(), i);
                        ImGui::PopID();
                        break;
                    }

                    api->draw_row_form(t.Handle(), i);
                }

                ImGui::PopID();
            }

            if (ImGui::Button("Add a light")) {
                const int32_t index = api->add_row(t.Handle());

                if (index >= 0) {
                    const sxc::data::Row r = t.At(static_cast<uint32_t>(index));
                    r.SetString(fName, 0, "new light");
                    r.SetRaw(fColor, 0, 0xFF9F49);   // the stock torches' colour
                }
            }
        }

        // --- markers in the world ------------------------------------------------------------------
        ImU32 Colour(const float rgb[3], float alpha) {
            const float top = std::max(std::max(rgb[0], rgb[1]), std::max(rgb[2], 1e-4f));
            return IM_COL32(static_cast<int>(rgb[0] / top * 255.0f), static_cast<int>(rgb[1] / top * 255.0f), static_cast<int>(rgb[2] / top * 255.0f),
                            static_cast<int>(alpha * 255.0f));
        }

        struct Painter {
            ImDrawList* draw = ImGui::GetBackgroundDrawList(ImGui::GetMainViewport());
            ImVec2 size = ImGui::GetIO().DisplaySize;

            bool At(const float p[3], ImVec2& out) const {
                return world::Project(p, size.x, size.y, out.x, out.y);
            }

            void Line(const float a[3], const float b[3], ImU32 c, float width = 1.5f) const {
                ImVec2 pa, pb;

                if (At(a, pa) && At(b, pb)) {
                    draw->AddLine(pa, pb, c, width);
                }
            }

            void Cross(const float p[3], float r, ImU32 c, float width = 2.0f) const {
                for (int axis = 0; axis < 3; axis++) {
                    float a[3] = { p[0], p[1], p[2] }, b[3] = { p[0], p[1], p[2] };
                    a[axis] -= r;
                    b[axis] += r;
                    Line(a, b, c, width);
                }
            }

            void Rings(const float p[3], float r, ImU32 c) const {
                constexpr int kSegments = 40;

                for (int plane = 0; plane < 3; plane++) {
                    const int ia = plane == 1 ? 1 : 0, ib = plane == 0 ? 1 : 2;

                    for (int k = 0; k < kSegments; k++) {
                        const float a0 = 6.2831853f * k / kSegments, a1 = 6.2831853f * (k + 1) / kSegments;
                        float s0[3] = { p[0], p[1], p[2] }, s1[3] = { p[0], p[1], p[2] };
                        s0[ia] += std::cos(a0) * r;
                        s0[ib] += std::sin(a0) * r;
                        s1[ia] += std::cos(a1) * r;
                        s1[ib] += std::sin(a1) * r;
                        Line(s0, s1, c, 1.0f);
                    }
                }
            }

            void Label(const float p[3], const char* text, ImU32 c) const {
                ImVec2 at;

                if (At(p, at)) {
                    draw->AddText(ImVec2(at.x + 4.0f, at.y - 14.0f), c, text);
                }
            }
        };

        void DrawMarkers(const sxc_lights_api* api, const world::Snapshot& s, const Model* selected, const std::vector<sxc_lights_point>& points,
                         const std::unordered_set<uint32_t>& missing) {
            const Painter paint;

            // The lights as drawn last frame (after merging, flickering): a cross in their colour, rings at
            // their radius, a spot's axis; a small square for an indoor light, a white dot for one with a shadow map.
            if (g_markers) {
                static std::vector<sxc_lights_active> active(512);
                const uint32_t n = api->active_lights(active.data(), static_cast<uint32_t>(active.size()));

                for (uint32_t i = 0; i < n; i++) {
                    const sxc_lights_active& l = active[i];

                    if (g_markers == 1 && (!selected || Normalize(l.model_path) != selected->key)) {
                        continue;
                    }

                    const float dx = l.pos[0] - s.playerPos[0], dy = l.pos[1] - s.playerPos[1], dz = l.pos[2] - s.playerPos[2];

                    if (dx * dx + dy * dy + dz * dz > g_range * g_range) {
                        continue;
                    }

                    const ImU32 c = Colour(l.color, 1.0f);
                    paint.Cross(l.pos, 0.4f, c);

                    if (g_rings) {
                        paint.Rings(l.pos, l.radius, Colour(l.color, 0.45f));
                    }

                    if (l.cos_outer > -1.5f) {
                        const float tip[3] = { l.pos[0] + l.spot_dir[0] * l.radius * 0.5f, l.pos[1] + l.spot_dir[1] * l.radius * 0.5f, l.pos[2] + l.spot_dir[2] * l.radius * 0.5f };
                        paint.Line(l.pos, tip, c);
                    }

                    ImVec2 at;

                    if (paint.At(l.pos, at)) {
                        if (l.indoor) {
                            paint.draw->AddRect(ImVec2(at.x - 5.0f, at.y - 5.0f), ImVec2(at.x + 5.0f, at.y + 5.0f), c);
                        }

                        if (l.shadowed) {
                            paint.draw->AddCircleFilled(at, 2.5f, IM_COL32(255, 255, 255, 255));
                        }
                    }
                }
            }

            if (!selected) {
                return;
            }

            // The selected model's points: attachment points yellow, emitters orange, own lights cyan, bones grey;
            // the focused row's attachment white and larger.
            sxc::data::Table& t = T().rows;
            const sxc::data::Row focus = g_focusRow ? t.Find(g_focusRow) : sxc::data::Row();
            const uint32_t focusType = focus ? focus.Uint(t.Field("AttachType")) : 99;
            const int32_t focusIndex = focus ? focus.Int(t.Field("AttachIndex")) : -1;
            char label[48];

            for (const sxc_lights_point& p : points) {
                const bool focused = p.kind == focusType && p.index == focusIndex;

                if (!g_points && !focused) {
                    continue;
                }

                if (p.kind == SXC_LIGHTS_ATTACH_BONE && !g_bones && !focused) {
                    continue;
                }

                const ImU32 c = focused ? IM_COL32(255, 255, 255, 255)
                              : p.kind == SXC_LIGHTS_ATTACH_POINT ? IM_COL32(255, 230, 60, 255)
                              : p.kind == SXC_LIGHTS_ATTACH_EMITTER ? IM_COL32(255, 140, 40, 255)
                              : p.kind == SXC_LIGHTS_POINT_OWN_LIGHT ? IM_COL32(60, 220, 255, 255)
                                                                     : IM_COL32(170, 170, 170, 200);
                paint.Cross(p.pos, focused ? 0.3f : 0.12f, c, focused ? 2.5f : 1.5f);
                const char kind = p.kind == SXC_LIGHTS_ATTACH_POINT ? 'A' : p.kind == SXC_LIGHTS_ATTACH_EMITTER ? 'E' : p.kind == SXC_LIGHTS_POINT_OWN_LIGHT ? 'L' : 'B';
                std::snprintf(label, sizeof label, "%c%d", kind, p.index);
                paint.Label(p.pos, label, c);
            }

            // Rows whose attachment the model lacks: a red mark at its origin, where their light now sits.
            for (uint32_t id : RowsOf(selected->key)) {
                if (missing.count(id)) {
                    paint.Cross(selected->pos, 0.6f, IM_COL32(255, 60, 50, 255), 3.0f);
                    std::snprintf(label, sizeof label, "row %u: attachment missing", id);
                    paint.Label(selected->pos, label, IM_COL32(255, 60, 50, 255));
                }
            }
        }
    }

    void DrawLightEditor(void*) {
        const sxc_lights_api* lightsApi = lights::EditorApi();
        const sxc_data_api* dataApi = DataApi();

        if (!lightsApi || !dataApi) {
            ImGui::TextColored(kMissing, "This client is too old for the light editor (update the client).");
            return;
        }

        if (!T().lights || !T().rows) {
            ImGui::TextColored(kMissing, "The light tables aren't registered.");
            return;
        }

        TableLine(dataApi, "DoodadLightProperties", T().lights);
        TableLine(dataApi, "DoodadLightAssignment", T().rows);

        if (g_message[0]) {
            ImGui::TextDisabled("%s", g_message);
        }

        if (!lights::Config().enabled) {
            ImGui::TextColored(kStock, "Our lights are off in the Lights panel: edits won't show.");
        }

        const world::Snapshot& s = world::Current();

        if (!s.inWorld) {
            ImGui::TextDisabled("Not in the world.");
            return;
        }

        static std::vector<uint32_t> missingIds(1024);
        const uint32_t missingCount = lightsApi->missing_rows(missingIds.data(), static_cast<uint32_t>(missingIds.size()));
        const std::unordered_set<uint32_t> missing(missingIds.begin(), missingIds.begin() + missingCount);

        ImGui::SeparatorText("Markers in the world");
        ImGui::SetNextItemWidth(220.0f);
        ImGui::Combo("Lights", &g_markers, "off\0the selected model's\0all within the range\0");
        ImGui::SetItemTooltip("A cross in the light's colour where it is drawn; a square: indoor; a white dot: it has a shadow map.");
        ImGui::SameLine();
        ImGui::Checkbox("Reach", &g_rings);
        ImGui::SetItemTooltip("Rings at the light's radius (where it reaches 0).");
        ImGui::Checkbox("The selected model's points", &g_points);
        ImGui::SetItemTooltip("A yellow: attachment points (by ID), E orange: particle emitters, L cyan: its own lights,\n"
                              "B grey: bones. The open row's attachment is white.");
        ImGui::SameLine();
        ImGui::Checkbox("Bones", &g_bones);

        ImGui::SeparatorText("Models near you");
        ImGui::SetNextItemWidth(220.0f);
        ImGui::SliderFloat("Within", &g_range, 3.0f, 80.0f, "%.0f yd");
        ImGui::SetNextItemWidth(220.0f);
        ImGui::InputTextWithHint("##search", "search the path", g_search, sizeof g_search);

        for (int c = 0; c < 5; c++) {
            ImGui::SameLine();
            ImGui::Checkbox(kCategories[c], &g_category[c]);
        }

        ImGui::TextColored(kOurs, "green: has our light");
        ImGui::SameLine();
        ImGui::TextColored(kStock, " amber: only its own");
        ImGui::SameLine();
        ImGui::TextColored(kMissing, " red: a row's attachment is missing");

        const std::vector<Model> models = NearbyModels(lightsApi, s);
        const std::string search = Normalize(g_search);
        const Model* selected = nullptr;

        if (ImGui::BeginChild("models", ImVec2(0.0f, 220.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY)) {
            int shown = 0;

            for (const Model& m : models) {
                if (!g_category[CategoryOf(m.key)] || (!search.empty() && m.key.find(search) == std::string::npos)) {
                    continue;
                }

                shown++;
                bool ours = false, lacks = false;

                for (uint32_t id : RowsOf(m.key)) {
                    const sxc::data::Row r = T().rows.Find(id);
                    ours = ours || (r && r.Uint(T().rows.Field("LightID")) != 0);
                    lacks = lacks || missing.count(id) != 0;
                }

                char line[400];
                std::snprintf(line, sizeof line, "%5.1f yd  %s%s%s  (%u placed; A %u, E %u, L %u, B %u)", m.distance, m.path.c_str(), m.parent.empty() ? "" : "  on ",
                              m.parent.c_str(), m.placed, m.attachments, m.emitters, m.ownLights, m.bones);
                const bool colour = lacks || ours || m.ownLights > 0;

                if (colour) {
                    ImGui::PushStyleColor(ImGuiCol_Text, lacks ? kMissing : (ours ? kOurs : kStock));
                }

                if (ImGui::Selectable(line, m.key == g_selected)) {
                    g_selected = m.key;
                    g_focusRow = 0;
                }

                if (colour) {
                    ImGui::PopStyleColor();
                }
            }

            if (!shown) {
                ImGui::TextDisabled(models.empty() ? "No models within the range." : "None match the search and filters.");
            }
        }

        ImGui::EndChild();

        for (const Model& m : models) {
            if (m.key == g_selected) {
                selected = &m;
            }
        }

        // Out of range (walked away, teleported): the selection goes.
        if (!selected && !g_selected.empty()) {
            std::snprintf(g_message, sizeof g_message, "selection cleared: no longer within %.0f yd", g_range);
            g_selected.clear();
            g_focusRow = 0;
        }

        static std::vector<sxc_lights_point> points(4096);
        std::vector<sxc_lights_point> found;

        if (selected) {
            const uint32_t n = lightsApi->model_points(selected->path.c_str(), s.playerPos, WXL_TRUE, points.data(), static_cast<uint32_t>(points.size()));
            found.assign(points.begin(), points.begin() + n);
        }

        DrawMarkers(lightsApi, s, selected, found, missing);

        ImGui::SeparatorText("The selected model");

        if (!selected) {
            ImGui::TextDisabled("Select a model above to put lights on it.");
        } else {
            ImGui::TextWrapped("%s", selected->path.c_str());

            if (!selected->parent.empty()) {
                ImGui::TextDisabled("on %s%s", selected->parent.c_str(), selected->animated ? "" : " (not animated now: its points are unknown)");
            }

            ImGui::Text("A %u, E %u, L %u, B %u", selected->attachments, selected->emitters, selected->ownLights, selected->bones);
            EditRows(dataApi, *selected, found, missing);
        }

        ImGui::SeparatorText("Lights (DoodadLightProperties)");
        EditLights(dataApi);
    }
}
