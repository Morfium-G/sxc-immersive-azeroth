// The Rivers panel (kb: rivers): the rivers of the map you are on (RiverPath) and their nodes (RiverNode),
// laid out where you stand: a new river here, a node where I stand, a node to me, a node between two, the
// width, speed and kind per node. Markers in the world while it is open: the selected river's line, its
// nodes and their width. The flow itself shows in the Water panel's debug view "River flow".
#include "ui/Panels.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>
#include "SXC/Api/sxc_data.hpp"
#include "World.h"
#include "imgui.h"
#include "water/Rivers.h"

namespace ia::ui {
    namespace {
        constexpr float kDefaultWidth = 10.0f, kDefaultSpeed = 1.5f;
        const ImVec4 kWarn(1.0f, 0.35f, 0.3f, 1.0f);
        uint32_t g_river = 0;     // the selected RiverPath
        uint32_t g_node = 0;      // the open RiverNode
        bool g_markers = true;
        bool g_arrows = true;     // the flow around you as arrows
        char g_message[256] = "";

        const sxc_data_api* DataApi() {
            const sxc_data_api* api = sxc::data::Api();
            return api && api->struct_size >= offsetof(sxc_data_api, open_row) + sizeof(api->open_row) ? api : nullptr;
        }

        struct Tables {
            sxc::data::Table paths, nodes;
        };

        Tables& T() {
            static Tables t;

            if (!t.paths) {
                t.paths = sxc::data::Table::Get("RiverPath");
            }

            if (!t.nodes) {
                t.nodes = sxc::data::Table::Get("RiverNode");
            }

            return t;
        }

        // One table's line: its state, undo / redo, save, reload.
        void TableLine(const sxc_data_api* api, const char* label, const sxc::data::Table& t) {
            ImGui::PushID(label);
            ImGui::Text("%s: %u rows%s", label, t.RowCount(), api->has_changes(t.Handle()) ? " (edited, not saved)" : "");

            if (api->changed_on_disk(t.Handle())) {
                ImGui::SameLine();
                ImGui::TextColored(kWarn, "changed on disk");
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

        struct NodeRef {
            uint32_t id;
            int32_t order;
            float pos[3];
        };

        // The river's nodes, in order.
        std::vector<NodeRef> NodesOf(uint32_t river) {
            std::vector<NodeRef> out;
            const sxc::data::Table& n = T().nodes;
            const int fPath = n.Field("PathID"), fOrder = n.Field("Order"), fPos = n.Field("Position");

            for (uint32_t i = 0; i < n.RowCount(); i++) {
                const sxc::data::Row r = n.At(i);

                if (r.Uint(fPath) == river) {
                    out.push_back(NodeRef{ r.Id(), r.Int(fOrder), { r.Float(fPos, 0), r.Float(fPos, 1), r.Float(fPos, 2) } });
                }
            }

            std::sort(out.begin(), out.end(), [](const NodeRef& a, const NodeRef& b) { return a.order != b.order ? a.order < b.order : a.id < b.id; });
            return out;
        }

        void SetPos(const sxc::data::Row& r, const float pos[3]) {
            const int f = T().nodes.Field("Position");

            for (uint32_t c = 0; c < 3; c++) {
                r.SetFloat(f, c, pos[c]);
            }
        }

        // A node of the river at `pos` with `order`; its id.
        uint32_t AddNode(const sxc_data_api* api, uint32_t river, int32_t order, const float pos[3]) {
            sxc::data::Table& n = T().nodes;
            const int32_t index = api->add_row(n.Handle());

            if (index < 0) {
                return 0;
            }

            const sxc::data::Row r = n.At(static_cast<uint32_t>(index));
            r.SetRaw(n.Field("PathID"), 0, river);
            r.SetRaw(n.Field("Order"), 0, static_cast<uint32_t>(order));
            SetPos(r, pos);
            return r.Id();
        }

        // Orders 10, 20, 30 ... so a node fits between any two.
        void Renumber(const std::vector<NodeRef>& nodes) {
            sxc::data::Table& n = T().nodes;
            const int fOrder = n.Field("Order");

            for (size_t i = 0; i < nodes.size(); i++) {
                if (const sxc::data::Row r = n.Find(nodes[i].id); r && r.Int(fOrder) != static_cast<int32_t>((i + 1) * 10)) {
                    r.SetRaw(fOrder, 0, static_cast<uint32_t>((i + 1) * 10));
                }
            }
        }

        // Where the player stands (swimming: at the surface). The flow takes a node's x and y only.
        void Here(const world::Snapshot& s, float pos[3]) {
            pos[0] = s.playerPos[0];
            pos[1] = s.playerPos[1];
            pos[2] = s.playerPos[2];
        }

        void NewRiver(const sxc_data_api* api, const world::Snapshot& s) {
            sxc::data::Table& p = T().paths;
            const int32_t index = api->add_row(p.Handle());

            if (index < 0) {
                std::snprintf(g_message, sizeof g_message, "RiverPath: no row added");
                return;
            }

            const sxc::data::Row r = p.At(static_cast<uint32_t>(index));
            char name[64];
            std::snprintf(name, sizeof name, "River %u", r.Id());
            r.SetString(p.Field("Name"), 0, name);
            r.SetRaw(p.Field("MapID"), 0, static_cast<uint32_t>(s.mapId));
            r.SetFloat(p.Field("Width"), 0, kDefaultWidth);
            r.SetFloat(p.Field("Speed"), 0, kDefaultSpeed);
            g_river = r.Id();
            float pos[3];
            Here(s, pos);
            g_node = AddNode(api, g_river, 10, pos);
            std::snprintf(g_message, sizeof g_message, "%s: walk downstream and add nodes where the river runs", name);
        }

        // A river leaving this one at a node: its first node there (a fork; a channel around an island
        // branches, then ends where it meets the river again). Where rivers overlap, the flow blends.
        void Branch(const sxc_data_api* api, const NodeRef& at) {
            sxc::data::Table& p = T().paths;
            const sxc::data::Row from = p.Find(g_river);
            const int32_t index = from ? api->add_row(p.Handle()) : -1;

            if (index < 0) {
                return;
            }

            const sxc::data::Row r = p.At(static_cast<uint32_t>(index));
            char name[96];
            std::snprintf(name, sizeof name, "%s branch", std::string(from.String(p.Field("Name"))).c_str());
            r.SetString(p.Field("Name"), 0, name);
            r.SetRaw(p.Field("MapID"), 0, from.Uint(p.Field("MapID")));
            r.SetFloat(p.Field("Width"), 0, from.Float(p.Field("Width")));
            r.SetFloat(p.Field("Speed"), 0, from.Float(p.Field("Speed")));
            r.SetRaw(p.Field("BranchesFromID"), 0, g_river);
            g_river = r.Id();
            g_node = AddNode(api, g_river, 10, at.pos);
            std::snprintf(g_message, sizeof g_message, "%s: walk down the branch and add nodes; end it where it meets the river again", name);
        }

        void EditNode(const sxc_data_api* api, const world::Snapshot& s, const std::vector<NodeRef>& nodes, size_t i) {
            sxc::data::Table& n = T().nodes;
            const sxc::data::Row r = n.Find(nodes[i].id);

            if (!r) {
                return;
            }

            ImGui::PushID(static_cast<int>(nodes[i].id));
            const float dx = nodes[i].pos[0] - s.playerPos[0], dy = nodes[i].pos[1] - s.playerPos[1];
            char label[96];
            std::snprintf(label, sizeof label, "Node %zu  (%.0f yd)###node", i + 1, std::sqrt(dx * dx + dy * dy));

            if (ImGui::Selectable(label, g_node == nodes[i].id)) {
                g_node = g_node == nodes[i].id ? 0 : nodes[i].id;
            }

            if (g_node == nodes[i].id) {
                ImGui::Indent();

                if (ImGui::SmallButton("To me")) {
                    float pos[3];
                    Here(s, pos);
                    SetPos(r, pos);
                }

                ImGui::SetItemTooltip("Moves the node to where you stand.");
                ImGui::SameLine();

                if (ImGui::SmallButton(i + 1 < nodes.size() ? "Insert after" : "Add after")) {
                    float pos[3];

                    if (i + 1 < nodes.size()) {
                        for (int c = 0; c < 3; c++) {
                            pos[c] = (nodes[i].pos[c] + nodes[i + 1].pos[c]) * 0.5f;
                        }
                    } else {
                        Here(s, pos);
                    }

                    Renumber(nodes);
                    g_node = AddNode(api, g_river, static_cast<int32_t>((i + 1) * 10 + 5), pos);
                }

                ImGui::SetItemTooltip(i + 1 < nodes.size() ? "A node halfway to the next." : "A node where you stand, after this one.");
                ImGui::SameLine();

                if (ImGui::SmallButton("Delete")) {
                    api->remove_row(n.Handle(), static_cast<uint32_t>(api->find_row(n.Handle(), nodes[i].id)));
                    g_node = 0;
                    ImGui::Unindent();
                    ImGui::PopID();
                    return;
                }

                ImGui::SameLine();

                if (ImGui::SmallButton("Row form")) {
                    api->open_row(n.Handle(), nodes[i].id);
                }

                ImGui::SameLine();

                if (ImGui::SmallButton("Branch here")) {
                    Branch(api, nodes[i]);
                    ImGui::Unindent();
                    ImGui::PopID();
                    return;
                }

                ImGui::SetItemTooltip("A new river leaving this one here (a fork, or a channel around an island: end it where it rejoins).");

                const int fWidth = n.Field("Width"), fSpeed = n.Field("Speed"), fKind = n.Field("Kind");
                float width = r.Float(fWidth), speed = r.Float(fSpeed);
                ImGui::SetNextItemWidth(200.0f);

                if (ImGui::SliderFloat("Width (yd)", &width, 0.0f, 80.0f, width > 0.0f ? "%.1f" : "the river's")) {
                    r.SetFloat(fWidth, 0, width);
                }

                ImGui::SetNextItemWidth(200.0f);

                if (ImGui::SliderFloat("Speed (yd/s)", &speed, 0.0f, 10.0f, speed > 0.0f ? "%.1f" : "the river's")) {
                    r.SetFloat(fSpeed, 0, speed);
                }

                static const char* const kKinds[] = { "calm", "rapids", "falls" };
                int kind = std::clamp(r.Int(fKind), 0, 2);
                ImGui::SetNextItemWidth(200.0f);

                if (ImGui::Combo("Stretch after it", &kind, kKinds, IM_ARRAYSIZE(kKinds))) {
                    r.SetRaw(fKind, 0, static_cast<uint32_t>(kind));
                }

                ImGui::Unindent();
            }

            ImGui::PopID();
        }

        // The flow around the player as the water gets it: an arrow every 4 yd where a river is, as long as
        // a second's travel, coloured by speed like the debug view (blue still .. red at 4 yd / s).
        void DrawArrows(const world::Snapshot& s) {
            ImDrawList* draw = ImGui::GetBackgroundDrawList(ImGui::GetMainViewport());
            const ImVec2 size = ImGui::GetIO().DisplaySize;
            constexpr float kStep = 4.0f, kReach = 36.0f;
            const float ox = std::floor(s.playerPos[0] / kStep) * kStep, oy = std::floor(s.playerPos[1] / kStep) * kStep;

            for (float x = ox - kReach; x <= ox + kReach; x += kStep) {
                for (float y = oy - kReach; y <= oy + kReach; y += kStep) {
                    float fx = 0.0f, fy = 0.0f, w = 0.0f;

                    if (!rivers::FlowAt(x, y, fx, fy, w) || w < 0.05f) {
                        continue;
                    }

                    float a[3] = { x, y, s.playerPos[2] };
                    float height = 0.0f;

                    if (rivers::SurfaceAt(a, height)) {
                        a[2] = height + 0.1f;
                    }

                    const float speed = std::sqrt(fx * fx + fy * fy);
                    const float length = std::max(speed, 0.6f) / std::max(speed, 1e-3f);
                    const float b[3] = { a[0] + fx * length, a[1] + fy * length, a[2] };
                    ImVec2 pa, pb;

                    if (!world::Project(a, size.x, size.y, pa.x, pa.y) || !world::Project(b, size.x, size.y, pb.x, pb.y)) {
                        continue;
                    }

                    const float heat = std::clamp(speed / 4.0f, 0.0f, 1.0f);
                    const float cr = std::clamp(heat * 4.0f - 2.0f, 0.0f, 1.0f), cg = std::clamp(heat < 0.5f ? heat * 4.0f : 4.0f - heat * 4.0f, 0.0f, 1.0f),
                                cb = std::clamp(2.0f - heat * 4.0f, 0.0f, 1.0f);
                    const ImU32 c = IM_COL32(static_cast<int>(cr * 255.0f), static_cast<int>(cg * 255.0f), static_cast<int>(cb * 255.0f), static_cast<int>(80.0f + 175.0f * w));
                    draw->AddLine(pa, pb, c, 2.0f);
                    const float dx = pb.x - pa.x, dy = pb.y - pa.y, dl = std::sqrt(dx * dx + dy * dy);

                    if (dl > 4.0f) {
                        const float ux = dx / dl, uy = dy / dl, head = std::min(8.0f, dl * 0.4f);
                        draw->AddTriangleFilled(pb, ImVec2(pb.x - ux * head - uy * head * 0.5f, pb.y - uy * head + ux * head * 0.5f),
                                                ImVec2(pb.x - ux * head + uy * head * 0.5f, pb.y - uy * head - ux * head * 0.5f), c);
                    }
                }
            }
        }

        void DrawMarkers(const std::vector<NodeRef>& nodes, uint32_t river) {
            ImDrawList* draw = ImGui::GetBackgroundDrawList(ImGui::GetMainViewport());
            const ImVec2 size = ImGui::GetIO().DisplaySize;
            const sxc::data::Table& n = T().nodes;
            const sxc::data::Row path = T().paths.Find(river);
            const float fallback = path && path.Float(T().paths.Field("Width")) > 0.0f ? path.Float(T().paths.Field("Width")) : kDefaultWidth;
            const int fWidth = n.Field("Width");
            ImVec2 last;
            bool haveLast = false;

            for (size_t i = 0; i < nodes.size(); i++) {
                ImVec2 at;
                const bool shown = world::Project(nodes[i].pos, size.x, size.y, at.x, at.y);

                if (shown && haveLast) {
                    draw->AddLine(last, at, IM_COL32(80, 200, 255, 230), 2.5f);
                }

                if (shown) {
                    const ImU32 c = nodes[i].id == g_node ? IM_COL32(255, 255, 255, 255) : IM_COL32(80, 200, 255, 255);
                    draw->AddCircleFilled(at, 5.0f, c);
                    char label[16];
                    std::snprintf(label, sizeof label, "%zu", i + 1);
                    draw->AddText(ImVec2(at.x + 7.0f, at.y - 14.0f), c, label);

                    // The width across the river: a line through the node square to its stretch.
                    const sxc::data::Row r = n.Find(nodes[i].id);
                    const float width = r && r.Float(fWidth) > 0.0f ? r.Float(fWidth) : fallback;
                    const NodeRef& a = nodes[i > 0 ? i - 1 : i];
                    const NodeRef& b = nodes[i + 1 < nodes.size() ? i + 1 : i];
                    float tx = b.pos[0] - a.pos[0], ty = b.pos[1] - a.pos[1];
                    const float tl = std::sqrt(tx * tx + ty * ty);

                    if (tl > 1e-3f) {
                        tx /= tl;
                        ty /= tl;
                        const float l[3] = { nodes[i].pos[0] - ty * width * 0.5f, nodes[i].pos[1] + tx * width * 0.5f, nodes[i].pos[2] };
                        const float rr[3] = { nodes[i].pos[0] + ty * width * 0.5f, nodes[i].pos[1] - tx * width * 0.5f, nodes[i].pos[2] };
                        ImVec2 pl, pr;

                        if (world::Project(l, size.x, size.y, pl.x, pl.y) && world::Project(rr, size.x, size.y, pr.x, pr.y)) {
                            draw->AddLine(pl, pr, IM_COL32(80, 200, 255, 140), 1.5f);
                        }
                    }
                }

                last = at;
                haveLast = shown;
            }
        }
    }

    void DrawRivers(void*) {
        const sxc_data_api* api = DataApi();
        const world::Snapshot& s = world::Current();
        const rivers::Stats& stats = rivers::Current();

        if (!api || !T().paths || !T().nodes) {
            ImGui::TextColored(kWarn, "The river tables aren't registered (or this client can't edit tables).");
            return;
        }

        if (!stats.client) {
            ImGui::TextColored(kWarn, "This client doesn't take rivers (update the client).");
        }

        ImGui::TextWrapped("Rivers flow along their nodes, upstream first; the banks slow and turn the flow. See it in the Water panel's "
                           "debug view \"River flow\". Map %d: %u rivers with %u nodes sent.", s.mapId, stats.rivers, stats.nodes);
        TableLine(api, "RiverPath", T().paths);
        TableLine(api, "RiverNode", T().nodes);

        if (g_message[0]) {
            ImGui::TextDisabled("%s", g_message);
        }

        if (ImGui::Button("New river here")) {
            NewRiver(api, s);
        }

        ImGui::SetItemTooltip("A river on this map with its first node where you stand. Stand at its upstream end.");
        ImGui::SameLine();
        ImGui::Checkbox("Markers", &g_markers);
        ImGui::SameLine();
        ImGui::Checkbox("Flow arrows", &g_arrows);
        ImGui::SetItemTooltip("The flow around you as the water gets it: a second's travel, blue still .. red at 4 yd/s.");

        if (g_arrows) {
            DrawArrows(s);
        }

        // The rivers of this map.
        sxc::data::Table& p = T().paths;
        const int fMap = p.Field("MapID"), fName = p.Field("Name");

        if (ImGui::BeginChild("rivers", ImVec2(0.0f, 110.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY)) {
            int shown = 0;

            for (uint32_t i = 0; i < p.RowCount(); i++) {
                const sxc::data::Row r = p.At(i);

                if (r.Int(fMap) != s.mapId) {
                    continue;
                }

                shown++;
                char line[160];
                std::snprintf(line, sizeof line, "%u  %s  (%zu nodes)", r.Id(), std::string(r.String(fName)).c_str(), NodesOf(r.Id()).size());

                if (ImGui::Selectable(line, g_river == r.Id())) {
                    g_river = r.Id();
                    g_node = 0;
                }
            }

            if (!shown) {
                ImGui::TextDisabled("No rivers on this map yet.");
            }
        }

        ImGui::EndChild();

        const sxc::data::Row river = g_river ? p.Find(g_river) : sxc::data::Row();

        if (!river) {
            return;
        }

        ImGui::PushID("river");
        char name[96];
        std::snprintf(name, sizeof name, "%s", std::string(river.String(fName)).c_str());
        ImGui::SetNextItemWidth(200.0f);

        if (ImGui::InputText("Name", name, sizeof name, ImGuiInputTextFlags_EnterReturnsTrue)) {
            river.SetString(fName, 0, name);
        }

        ImGui::SameLine();

        if (ImGui::SmallButton("Row form")) {
            api->open_row(p.Handle(), g_river);
        }

        if (const uint32_t from = river.Uint(p.Field("BranchesFromID"))) {
            ImGui::TextDisabled("Branches from river %u.", from);
        }

        const int fWidth = p.Field("Width"), fSpeed = p.Field("Speed");
        float width = river.Float(fWidth), speed = river.Float(fSpeed);
        ImGui::SetNextItemWidth(200.0f);

        if (ImGui::SliderFloat("Width (yd)", &width, 1.0f, 80.0f, "%.1f")) {
            river.SetFloat(fWidth, 0, width);
        }

        ImGui::SetItemTooltip("Bank to bank, where a node doesn't set its own.");
        ImGui::SetNextItemWidth(200.0f);

        if (ImGui::SliderFloat("Speed (yd/s)", &speed, 0.1f, 10.0f, "%.1f")) {
            river.SetFloat(fSpeed, 0, speed);
        }

        ImGui::SetItemTooltip("Where a node doesn't set its own. A walk is ~2.5, a run 7.");
        const std::vector<NodeRef> nodes = NodesOf(g_river);

        if (ImGui::Button("Add a node where I stand")) {
            float pos[3];
            Here(s, pos);
            Renumber(nodes);
            g_node = AddNode(api, g_river, static_cast<int32_t>((nodes.size() + 1) * 10), pos);
        }

        ImGui::SetItemTooltip("At the river's downstream end.");

        if (nodes.size() < 2) {
            ImGui::TextColored(kWarn, "A river flows once it has two nodes.");
        }

        for (size_t i = 0; i < nodes.size(); i++) {
            EditNode(api, s, nodes, i);
        }

        ImGui::PopID();

        if (g_markers) {
            DrawMarkers(NodesOf(g_river), g_river);
        }
    }
}
