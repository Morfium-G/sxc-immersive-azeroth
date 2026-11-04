// The Rivers panel (kb: rivers): the rivers of the map you are on (RiverPath) and their nodes (RiverNode),
// laid out where you stand: a new river here, a node where I stand, a node to me, a node between two, the
// width, speed and kind per node. Markers in the world while it is open: the selected river's line, its
// nodes and their width, the map's other rivers dim; and the editor in the world (EditInWorld): nodes
// dragged with the mouse, "+" handles to insert and extend, ends joined to other rivers, nodes fitted to
// the banks. The flow itself shows in the Water panel's debug view "River flow".
#include "ui/Panels.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <utility>
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

        bool FitToBanks(const std::vector<NodeRef>& nodes, size_t i);
        void KeepJoined(int mapId, uint32_t river);

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
                ImGui::SameLine();

                if (ImGui::SmallButton("Fit to banks") && FitToBanks(nodes, i)) {
                    KeepJoined(world::Current().mapId, g_river);
                }

                ImGui::SetItemTooltip("Moves the node to the middle between the banks and sets its width to bank to bank.");

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

        // --- editing in the world ------------------------------------------------------------------------
        // The selected river's nodes as handles: drag one across the water (the mouse ray meets the water,
        // or the ground where there is none), a "+" in each stretch's middle inserts a node there, one past
        // the end extends the river; right-click a node for more. An end dragged onto another river joins
        // it (the last node: JoinsPathID; the first: BranchesFromID) and stays on it when that river moves.
        bool g_edit = true;
        uint32_t g_dragNode = 0;    // the node being dragged
        uint32_t g_snapRiver = 0;   // the river the dragged end would join on release
        uint32_t g_menuNode = 0;    // the node the right-click menu is for

        // How the selected river's ends join others, for the drag and the release.
        enum class End { None, First, Last };

        struct Handle {
            enum Kind { None, Node, Insert, Extend, River } kind = None;
            size_t index = 0;    // the node; for Insert the stretch's first node
            uint32_t river = 0;  // River: another river's id
            float pos[3] = {};
        };

        // Where the mouse ray meets the surface: the water's, or the ground's where there is none (or where it
        // stands above the water). Marched out from the camera, then narrowed down.
        bool PickSurface(const ImVec2& mouse, float out[3]) {
            const ImVec2 size = ImGui::GetIO().DisplaySize;
            float o[3], d[3];

            if (!world::Unproject(mouse.x, mouse.y, size.x, size.y, o, d)) {
                return false;
            }

            auto surfaceAt = [](float x, float y, float& z) {
                float liquid = 0.0f, ground = 0.0f;
                uint32_t type = 0;
                const bool wet = terrain::LiquidAt(x, y, liquid, type);
                const bool solid = terrain::HeightAt(x, y, ground);
                z = wet ? (solid ? std::max(liquid, ground) : liquid) : ground;
                return wet || solid;
            };
            auto below = [&](float t) {
                float z = 0.0f;
                return surfaceAt(o[0] + d[0] * t, o[1] + d[1] * t, z) && o[2] + d[2] * t <= z;
            };

            float t0 = 0.0f;

            for (float t = 0.5f; t < 600.0f; t += 0.25f + t * 0.02f) {
                if (!below(t)) {
                    t0 = t;
                    continue;
                }

                float t1 = t;

                for (int i = 0; i < 12; i++) {
                    const float mid = (t0 + t1) * 0.5f;
                    (below(mid) ? t1 : t0) = mid;
                }

                out[0] = o[0] + d[0] * t1;
                out[1] = o[1] + d[1] * t1;
                surfaceAt(out[0], out[1], out[2]);
                return true;
            }

            return false;
        }

        // The point of a river's line nearest (x, y) (its height between the nodes) and how far it is.
        bool NearestOnRiver(const std::vector<NodeRef>& nodes, float x, float y, float out[3], float& distance) {
            distance = 1e9f;

            for (size_t i = 0; i + 1 < nodes.size(); i++) {
                const float* a = nodes[i].pos;
                const float* b = nodes[i + 1].pos;
                const float abx = b[0] - a[0], aby = b[1] - a[1], l2 = abx * abx + aby * aby;
                const float t = l2 > 1e-6f ? std::clamp(((x - a[0]) * abx + (y - a[1]) * aby) / l2, 0.0f, 1.0f) : 0.0f;
                const float px = a[0] + abx * t, py = a[1] + aby * t;
                const float dd = std::sqrt((px - x) * (px - x) + (py - y) * (py - y));

                if (dd < distance) {
                    distance = dd;
                    out[0] = px;
                    out[1] = py;
                    out[2] = a[2] + (b[2] - a[2]) * t;
                }
            }

            return distance < 1e9f;
        }

        float WidthOf(uint32_t river) {
            const sxc::data::Row path = T().paths.Find(river);
            const float w = path ? path.Float(T().paths.Field("Width")) : 0.0f;
            return w > 0.0f ? w : kDefaultWidth;
        }

        // The rivers of this map other than `except`, with their nodes.
        std::vector<std::pair<uint32_t, std::vector<NodeRef>>> OtherRivers(int mapId, uint32_t except) {
            std::vector<std::pair<uint32_t, std::vector<NodeRef>>> out;
            const sxc::data::Table& p = T().paths;
            const int fMap = p.Field("MapID");

            for (uint32_t i = 0; i < p.RowCount(); i++) {
                const sxc::data::Row r = p.At(i);

                if (r.Int(fMap) == mapId && r.Id() != except) {
                    out.emplace_back(r.Id(), NodesOf(r.Id()));
                }
            }

            return out;
        }

        // The ends of other rivers attached to `river` (joining it, or branching from it) put back on its line
        // where it moved.
        void KeepJoined(int mapId, uint32_t river) {
            const std::vector<NodeRef> line = NodesOf(river);
            const sxc::data::Table& p = T().paths;
            const int fJoins = p.Field("JoinsPathID"), fBranches = p.Field("BranchesFromID");

            if (line.size() < 2) {
                return;
            }

            for (const auto& [id, nodes] : OtherRivers(mapId, river)) {
                const sxc::data::Row r = p.Find(id);

                if (!r || nodes.empty()) {
                    continue;
                }

                for (int end = 0; end < 2; end++) {
                    const bool last = end == 1;

                    if (r.Uint(last ? fJoins : fBranches) != river) {
                        continue;
                    }

                    const NodeRef& at = last ? nodes.back() : nodes.front();
                    float on[3], distance;

                    if (NearestOnRiver(line, at.pos[0], at.pos[1], on, distance) && distance > 0.01f) {
                        if (const sxc::data::Row node = T().nodes.Find(at.id)) {
                            SetPos(node, on);
                        }
                    }
                }
            }
        }

        // A node set to the river's banks there: across the river (square to its stretches), out each side
        // until the ground rises above the water or the water ends; the node moved to the middle and its
        // width to bank to bank. False (with a message) where no bank is found within 60 yd either side,
        // and for an end joined to another river: it stays on that river (bank to bank there is the other
        // river's).
        bool FitToBanks(const std::vector<NodeRef>& nodes, size_t i) {
            const sxc::data::Table& paths = T().paths;

            if (const sxc::data::Row path = paths.Find(g_river)) {
                const bool joined = (i + 1 == nodes.size() && path.Uint(paths.Field("JoinsPathID")) != 0)
                    || (i == 0 && path.Uint(paths.Field("BranchesFromID")) != 0);

                if (joined) {
                    std::snprintf(g_message, sizeof g_message, "Node %zu is joined to another river: it stays as it is.", i + 1);
                    return false;
                }
            }

            const sxc::data::Row r = T().nodes.Find(nodes[i].id);
            const NodeRef& a = nodes[i > 0 ? i - 1 : i];
            const NodeRef& b = nodes[i + 1 < nodes.size() ? i + 1 : i];
            float tx = b.pos[0] - a.pos[0], ty = b.pos[1] - a.pos[1];
            const float l = std::sqrt(tx * tx + ty * ty);

            if (!r || l < 1e-3f) {
                std::snprintf(g_message, sizeof g_message, "Node %zu: no stretch to measure across.", i + 1);
                return false;
            }

            tx /= l;
            ty /= l;
            const float* p = nodes[i].pos;
            float reach[2] = { -1.0f, -1.0f };

            for (int side = 0; side < 2; side++) {
                const float sx = side ? ty : -ty, sy = side ? -tx : tx;

                for (float d = 0.0f; d <= 60.0f; d += 0.5f) {
                    const float x = p[0] + sx * d, y = p[1] + sy * d;
                    float liquid = 0.0f, ground = -1e9f;
                    uint32_t type = 0;
                    const bool wet = terrain::LiquidAt(x, y, liquid, type);
                    terrain::HeightAt(x, y, ground);

                    if (!wet || ground >= liquid) {
                        reach[side] = d;
                        break;
                    }
                }
            }

            if (reach[0] < 0.0f || reach[1] < 0.0f || reach[0] + reach[1] < 1.0f) {
                std::snprintf(g_message, sizeof g_message, "Node %zu: no banks found within 60 yd either side.", i + 1);
                return false;
            }

            const float shift = (reach[1] - reach[0]) * 0.5f;   // toward side 1 by this
            float pos[3] = { p[0] + ty * shift, p[1] - tx * shift, p[2] };
            float liquid = 0.0f;
            uint32_t type = 0;

            if (terrain::LiquidAt(pos[0], pos[1], liquid, type)) {
                pos[2] = liquid;
            }

            SetPos(r, pos);
            r.SetFloat(T().nodes.Field("Width"), 0, reach[0] + reach[1]);
            return true;
        }

        // Where the "+" past the river's end sits: on along its last stretch, about its width out.
        void ExtendPoint(const std::vector<NodeRef>& nodes, float out[3]) {
            const NodeRef& b = nodes.back();
            const NodeRef& a = nodes[nodes.size() >= 2 ? nodes.size() - 2 : 0];
            float tx = b.pos[0] - a.pos[0], ty = b.pos[1] - a.pos[1];
            const float l = std::sqrt(tx * tx + ty * ty);

            if (l < 1e-3f) {
                tx = 1.0f;
                ty = 0.0f;
            } else {
                tx /= l;
                ty /= l;
            }

            const float reach = std::max(WidthOf(g_river), 8.0f);
            out[0] = b.pos[0] + tx * reach;
            out[1] = b.pos[1] + ty * reach;
            out[2] = b.pos[2];
        }

        // The handle under the mouse (within 10 px), nearest first: the selected river's nodes, then its "+"s,
        // then other rivers' lines (to select them).
        Handle HandleAt(const ImVec2& mouse, const std::vector<NodeRef>& nodes, const world::Snapshot& s) {
            const ImVec2 size = ImGui::GetIO().DisplaySize;
            constexpr float kReach = 10.0f;
            Handle best;
            float bestD = kReach;
            auto consider = [&](Handle::Kind kind, size_t index, const float pos[3], uint32_t river) {
                ImVec2 at;

                if (!world::Project(pos, size.x, size.y, at.x, at.y)) {
                    return;
                }

                const float d = std::sqrt((at.x - mouse.x) * (at.x - mouse.x) + (at.y - mouse.y) * (at.y - mouse.y));

                if (d < bestD) {
                    bestD = d;
                    best.kind = kind;
                    best.index = index;
                    best.river = river;
                    std::copy(pos, pos + 3, best.pos);
                }
            };

            for (size_t i = 0; i < nodes.size(); i++) {
                consider(Handle::Node, i, nodes[i].pos, 0);
            }

            if (best.kind != Handle::None) {
                return best;   // nodes before the "+"s, which sit close on short stretches
            }

            for (size_t i = 0; i + 1 < nodes.size(); i++) {
                float mid[3];

                for (int c = 0; c < 3; c++) {
                    mid[c] = (nodes[i].pos[c] + nodes[i + 1].pos[c]) * 0.5f;
                }

                consider(Handle::Insert, i, mid, 0);
            }

            if (!nodes.empty()) {
                float past[3];
                ExtendPoint(nodes, past);
                consider(Handle::Extend, nodes.size() - 1, past, 0);
            }

            if (best.kind != Handle::None) {
                return best;
            }

            // Other rivers: their nodes and a few points along each stretch.
            for (const auto& [id, other] : OtherRivers(s.mapId, g_river)) {
                for (size_t i = 0; i < other.size(); i++) {
                    consider(Handle::River, i, other[i].pos, id);

                    for (int k = 1; i + 1 < other.size() && k < 4; k++) {
                        float p[3];

                        for (int c = 0; c < 3; c++) {
                            p[c] = other[i].pos[c] + (other[i + 1].pos[c] - other[i].pos[c]) * k / 4.0f;
                        }

                        consider(Handle::River, i, p, id);
                    }
                }
            }

            return best;
        }

        void DrawPlus(ImDrawList* draw, const ImVec2& at, ImU32 c) {
            draw->AddCircle(at, 6.0f, c, 12, 1.5f);
            draw->AddLine(ImVec2(at.x - 3.5f, at.y), ImVec2(at.x + 3.5f, at.y), c, 1.5f);
            draw->AddLine(ImVec2(at.x, at.y - 3.5f), ImVec2(at.x, at.y + 3.5f), c, 1.5f);
        }

        // The dragged node is an end of the river: which one.
        End EndOf(const std::vector<NodeRef>& nodes, uint32_t node) {
            if (nodes.size() < 2) {
                return End::None;
            }

            return node == nodes.back().id ? End::Last : (node == nodes.front().id ? End::First : End::None);
        }

        void EditInWorld(const sxc_data_api* api, const world::Snapshot& s, const std::vector<NodeRef>& nodes) {
            ImGuiIO& io = ImGui::GetIO();
            const ImVec2 mouse = io.MousePos;
            const bool overUi = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) || (ImGui::IsAnyItemActive() && !g_dragNode);
            const Handle hover = overUi || g_dragNode ? Handle() : HandleAt(mouse, nodes, s);
            sxc::data::Table& p = T().paths;
            const int fJoins = p.Field("JoinsPathID"), fBranches = p.Field("BranchesFromID");

            // Over a handle or dragging, the mouse is ours: the game doesn't turn the camera or click a unit.
            if (hover.kind != Handle::None || g_dragNode) {
                ImGui::SetNextFrameWantCaptureMouse(true);
            }

            if (hover.kind != Handle::None && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                switch (hover.kind) {
                    case Handle::Node:
                        g_node = g_dragNode = nodes[hover.index].id;
                        break;
                    case Handle::Insert:
                        Renumber(nodes);
                        g_node = g_dragNode = AddNode(api, g_river, static_cast<int32_t>((hover.index + 1) * 10 + 5), hover.pos);
                        break;
                    case Handle::Extend:
                        Renumber(nodes);
                        g_node = g_dragNode = AddNode(api, g_river, static_cast<int32_t>((nodes.size() + 1) * 10), hover.pos);
                        break;
                    case Handle::River:
                        g_river = hover.river;
                        g_node = 0;
                        return;
                    default:
                        break;
                }
            }

            if (hover.kind == Handle::Node && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                g_node = g_menuNode = nodes[hover.index].id;
                ImGui::OpenPopup("river node");
            }

            if (g_dragNode) {
                const sxc::data::Row r = T().nodes.Find(g_dragNode);
                const End end = EndOf(nodes, g_dragNode);
                float at[3];

                if (!r) {
                    g_dragNode = 0;
                } else if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    // An end near another river snaps onto its line.
                    g_snapRiver = 0;

                    if (PickSurface(mouse, at)) {
                        if (end != End::None) {
                            float best = std::max(3.0f, WidthOf(g_river) * 0.5f);

                            for (const auto& [id, other] : OtherRivers(s.mapId, g_river)) {
                                float on[3], distance;

                                if (NearestOnRiver(other, at[0], at[1], on, distance) && distance < best) {
                                    best = distance;
                                    g_snapRiver = id;
                                    std::copy(on, on + 3, at);
                                }
                            }
                        }

                        const float* was = nullptr;

                        for (const NodeRef& n : nodes) {
                            if (n.id == g_dragNode) {
                                was = n.pos;
                            }
                        }

                        if (!was || std::fabs(was[0] - at[0]) + std::fabs(was[1] - at[1]) + std::fabs(was[2] - at[2]) > 0.02f) {
                            SetPos(r, at);
                            KeepJoined(s.mapId, g_river);
                        }
                    }
                } else {
                    // Released: an end joins the river it snapped to, or leaves the one it was on.
                    if (const sxc::data::Row path = p.Find(g_river); path && end != End::None) {
                        const int f = end == End::Last ? fJoins : fBranches;

                        if (f >= 0 && path.Uint(f) != g_snapRiver) {
                            path.SetRaw(f, 0, g_snapRiver);
                            std::snprintf(g_message, sizeof g_message, g_snapRiver ? "Its %s joins river %u." : "Its %s joins no river now.",
                                          end == End::Last ? "last node" : "first node", g_snapRiver);
                        }
                    }

                    g_dragNode = 0;
                    g_snapRiver = 0;
                }
            }

            // The right-click menu.
            if (ImGui::BeginPopup("river node")) {
                size_t i = nodes.size();

                for (size_t k = 0; k < nodes.size(); k++) {
                    if (nodes[k].id == g_menuNode) {
                        i = k;
                    }
                }

                if (i == nodes.size()) {
                    ImGui::CloseCurrentPopup();
                } else {
                    ImGui::TextDisabled("Node %zu", i + 1);

                    if (const sxc::data::Row r = T().nodes.Find(nodes[i].id); r && ImGui::MenuItem("To me")) {
                        float pos[3];
                        Here(s, pos);
                        SetPos(r, pos);
                        KeepJoined(s.mapId, g_river);
                    }

                    if (ImGui::MenuItem("Fit to banks") && FitToBanks(nodes, i)) {
                        KeepJoined(s.mapId, g_river);
                    }

                    if (ImGui::MenuItem("Branch here")) {
                        Branch(api, nodes[i]);
                    }

                    if (ImGui::MenuItem("Delete")) {
                        sxc::data::Table& n = T().nodes;
                        api->remove_row(n.Handle(), static_cast<uint32_t>(api->find_row(n.Handle(), nodes[i].id)));
                        g_node = 0;
                    }
                }

                ImGui::EndPopup();
            }

            // The handles' looks: the one under the mouse brighter, the "+"s, the snap target.
            ImDrawList* draw = ImGui::GetBackgroundDrawList(ImGui::GetMainViewport());
            const ImVec2 size = io.DisplaySize;
            ImVec2 at;

            for (size_t i = 0; i + 1 < nodes.size(); i++) {
                float mid[3];

                for (int c = 0; c < 3; c++) {
                    mid[c] = (nodes[i].pos[c] + nodes[i + 1].pos[c]) * 0.5f;
                }

                if (world::Project(mid, size.x, size.y, at.x, at.y)) {
                    const bool hot = hover.kind == Handle::Insert && hover.index == i;
                    DrawPlus(draw, at, hot ? IM_COL32(255, 255, 255, 255) : IM_COL32(80, 200, 255, 170));
                }
            }

            if (!nodes.empty()) {
                float past[3];
                ExtendPoint(nodes, past);

                if (world::Project(past, size.x, size.y, at.x, at.y)) {
                    DrawPlus(draw, at, hover.kind == Handle::Extend ? IM_COL32(255, 255, 255, 255) : IM_COL32(80, 255, 160, 170));
                }
            }

            if (hover.kind == Handle::Node && world::Project(hover.pos, size.x, size.y, at.x, at.y)) {
                draw->AddCircle(at, 9.0f, IM_COL32(255, 255, 255, 255), 16, 2.0f);
            }

            if (hover.kind == Handle::River) {
                ImGui::SetTooltip("River %u: click to select it", hover.river);
            }

            if (g_dragNode && g_snapRiver) {
                for (const NodeRef& n : nodes) {
                    if (n.id == g_dragNode && world::Project(n.pos, size.x, size.y, at.x, at.y)) {
                        draw->AddCircle(at, 12.0f, IM_COL32(255, 220, 80, 255), 20, 2.5f);
                        draw->AddText(ImVec2(at.x + 14.0f, at.y - 6.0f), IM_COL32(255, 220, 80, 255), "joins");
                    }
                }
            }
        }

        // The other rivers of this map, dim, so a river can be picked and an end dragged onto one.
        void DrawOtherRivers(const world::Snapshot& s) {
            ImDrawList* draw = ImGui::GetBackgroundDrawList(ImGui::GetMainViewport());
            const ImVec2 size = ImGui::GetIO().DisplaySize;

            for (const auto& [id, other] : OtherRivers(s.mapId, g_river)) {
                ImVec2 last;
                bool haveLast = false;

                for (const NodeRef& n : other) {
                    ImVec2 at;
                    const bool shown = world::Project(n.pos, size.x, size.y, at.x, at.y);

                    if (shown && haveLast) {
                        draw->AddLine(last, at, IM_COL32(170, 170, 190, 130), 1.5f);
                    }

                    if (shown) {
                        draw->AddCircleFilled(at, 3.0f, IM_COL32(170, 170, 190, 160));
                    }

                    last = at;
                    haveLast = shown;
                }
            }
        }

        void DrawMarkers(const std::vector<NodeRef>& nodes, uint32_t river) {
            ImDrawList* draw = ImGui::GetBackgroundDrawList(ImGui::GetMainViewport());
            const ImVec2 size = ImGui::GetIO().DisplaySize;
            const sxc::data::Table& n = T().nodes;
            const float fallback = WidthOf(river);
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
        ImGui::BeginDisabled(!g_markers || !world::HasPointQueries());
        ImGui::Checkbox("Edit in the world", &g_edit);
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Drag a node across the water. The + in a stretch's middle inserts a node, the green + past the end "
                              "extends the river. Right-click a node for more. Drag an end onto another river to join it (it stays "
                              "on it when that river moves). Click another river's line to select it.");
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
            if (g_markers) {
                DrawOtherRivers(s);

                if (g_edit) {
                    EditInWorld(api, s, {});
                }
            }

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
        ImGui::SameLine();
        ImGui::BeginDisabled(nodes.size() < 2);

        if (ImGui::Button("Fit all to banks")) {
            int fitted = 0;

            for (size_t i = 0; i < nodes.size(); i++) {
                fitted += FitToBanks(nodes, i) ? 1 : 0;
            }

            KeepJoined(world::Current().mapId, g_river);
            std::snprintf(g_message, sizeof g_message, "%d of %zu nodes fitted to their banks (joined ends stay as they are).", fitted, nodes.size());
        }

        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Every node to the middle between its banks, its width bank to bank. Loaded ground only: stand near the river.");

        if (nodes.size() < 2) {
            ImGui::TextColored(kWarn, "A river flows once it has two nodes.");
        }

        for (size_t i = 0; i < nodes.size(); i++) {
            EditNode(api, s, nodes, i);
        }

        ImGui::PopID();

        if (const uint32_t joins = river.Uint(p.Field("JoinsPathID"))) {
            ImGui::TextDisabled("Flows into river %u.", joins);
        }

        if (g_markers) {
            DrawOtherRivers(s);
            DrawMarkers(NodesOf(g_river), g_river);

            if (g_edit) {
                EditInWorld(api, s, NodesOf(g_river));
            }
        }
    }
}
