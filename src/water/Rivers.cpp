#include "water/Rivers.h"

#include <algorithm>
#include <cstddef>
#include <vector>
#include "SXC/Api/sxc_water.h"
#include "TableWatch.h"
#include "World.h"
#include "wxl/sdk.hpp"

namespace ia::rivers {
    namespace {
        constexpr float kDefaultWidth = 10.0f, kDefaultSpeed = 1.5f;

        TableWatch g_paths("RiverPath");
        TableWatch g_nodes("RiverNode");
        int g_map = -2;
        Stats g_stats;

        const sxc_water_api* Api() {
            static const sxc_water_api* s_api =
                wxl::Host() ? static_cast<const sxc_water_api*>(wxl::Host()->get_service(SXC_WATER_SERVICE, SXC_WATER_SERVICE_VERSION)) : nullptr;
            return s_api && s_api->struct_size >= offsetof(sxc_water_api, set_rivers) + sizeof(s_api->set_rivers) ? s_api : nullptr;
        }

        void Send(const sxc_water_api* api, int map) {
            std::vector<sxc_water_river> rivers;
            std::vector<sxc_water_river_node> nodes;
            const sxc::data::Table& p = g_paths.Table();
            const sxc::data::Table& n = g_nodes.Table();

            if (p && n) {
                const int fMap = p.Field("MapID"), fWidth = p.Field("Width"), fSpeed = p.Field("Speed"), fJoins = p.Field("JoinsPathID"),
                          fBranches = p.Field("BranchesFromID");
                const int fPath = n.Field("PathID"), fOrder = n.Field("Order"), fPos = n.Field("Position"), fNodeWidth = n.Field("Width"),
                          fNodeSpeed = n.Field("Speed"), fKind = n.Field("Kind");

                // The nodes by river, in order.
                struct Entry {
                    uint32_t path;
                    int32_t order;
                    uint32_t index;
                };
                std::vector<Entry> entries;
                entries.reserve(n.RowCount());

                for (uint32_t i = 0; i < n.RowCount(); i++) {
                    const sxc::data::Row r = n.At(i);
                    entries.push_back(Entry{ r.Uint(fPath), r.Int(fOrder), i });
                }

                std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
                    return a.path != b.path ? a.path < b.path : a.order != b.order ? a.order < b.order : a.index < b.index;
                });

                for (uint32_t i = 0; i < p.RowCount(); i++) {
                    const sxc::data::Row r = p.At(i);

                    if (r.Int(fMap) != map) {
                        continue;
                    }

                    const float width = r.Float(fWidth) > 0.0f ? r.Float(fWidth) : kDefaultWidth;
                    const float speed = r.Float(fSpeed) > 0.0f ? r.Float(fSpeed) : kDefaultSpeed;
                    sxc_water_river river = {};
                    river.id = r.Id();
                    river.first_node = static_cast<uint32_t>(nodes.size());
                    river.joins = fJoins >= 0 ? r.Uint(fJoins) : 0;
                    river.branches_from = fBranches >= 0 ? r.Uint(fBranches) : 0;
                    auto e = std::lower_bound(entries.begin(), entries.end(), river.id, [](const Entry& a, uint32_t id) { return a.path < id; });

                    for (; e != entries.end() && e->path == river.id; ++e) {
                        const sxc::data::Row node = n.At(e->index);
                        sxc_water_river_node out = {};
                        out.x = node.Float(fPos, 0);
                        out.y = node.Float(fPos, 1);
                        out.z = node.Float(fPos, 2);
                        out.width = node.Float(fNodeWidth) > 0.0f ? node.Float(fNodeWidth) : width;
                        out.speed = node.Float(fNodeSpeed) > 0.0f ? node.Float(fNodeSpeed) : speed;
                        out.kind = node.Uint(fKind);
                        nodes.push_back(out);
                    }

                    river.node_count = static_cast<uint32_t>(nodes.size()) - river.first_node;

                    if (river.node_count >= 2) {
                        rivers.push_back(river);
                    } else {
                        nodes.resize(river.first_node);
                    }
                }
            }

            api->set_rivers(rivers.empty() ? nullptr : rivers.data(), static_cast<uint32_t>(rivers.size()), nodes.empty() ? nullptr : nodes.data(),
                            static_cast<uint32_t>(nodes.size()));
            g_stats.rivers = static_cast<uint32_t>(rivers.size());
            g_stats.nodes = static_cast<uint32_t>(nodes.size());
        }
    }

    void Update(const world::Snapshot& snap) {
        const sxc_water_api* api = Api();
        g_stats.client = api != nullptr;

        // Both asked every frame, so each sees its own change.
        const bool paths = g_paths.Changed();
        const bool nodes = g_nodes.Changed();

        if (!api || (!paths && !nodes && snap.mapId == g_map)) {
            return;
        }

        g_map = snap.mapId;
        Send(api, snap.mapId);
    }

    const Stats& Current() {
        return g_stats;
    }

    bool FlowAt(float x, float y, float& flowX, float& flowY, float& weight) {
        const sxc_water_api* api = Api();

        if (!api || api->struct_size < offsetof(sxc_water_api, flow_at) + sizeof(api->flow_at)) {
            return false;
        }

        return api->flow_at(x, y, &flowX, &flowY, &weight) != WXL_FALSE;
    }

    bool SurfaceAt(const float pos[3], float& height) {
        const sxc_water_api* api = Api();
        const wxl_vec3 p = { pos[0], pos[1], pos[2] };
        uint32_t type = 0;
        return api && api->liquid_at(&p, &height, &type) != WXL_FALSE;
    }
}
