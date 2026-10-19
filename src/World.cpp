#include "World.h"

#include "SXC/Api/sxc_world.h"
#include "wxl/sdk.hpp"

namespace ia::world {
    namespace {
        Snapshot g_snap;

        const sxc_world_api* Api() {
            static const sxc_world_api* s_api =
                wxl::Host() ? static_cast<const sxc_world_api*>(wxl::Host()->get_service(SXC_WORLD_SERVICE, SXC_WORLD_SERVICE_VERSION)) : nullptr;
            return s_api;
        }
    }

    const Snapshot& Refresh() {
        Snapshot s;

        if (const sxc_world_api* api = Api()) {
            sxc_world_snapshot w = {};
            w.struct_size = sizeof(w);
            api->snapshot(&w);
            s.inWorld = w.in_world != 0;
            s.playerPos[0] = w.player_position.x;
            s.playerPos[1] = w.player_position.y;
            s.playerPos[2] = w.player_position.z;
            s.mapId = w.map_id;
            s.areaCount = w.area_count < kMaxAreaChain ? w.area_count : kMaxAreaChain;

            for (int i = 0; i < s.areaCount; i++) {
                s.areaChain[i] = w.area_chain[i];
            }

            s.outdoors = w.outdoors != 0;
            s.playerTerrainType = w.terrain_type;
            s.weatherRaw = w.weather_raw;
            s.weatherIntensity = w.weather_intensity;
            s.weatherType = w.weather_type;
            s.cameraPos[0] = w.camera_position.x;
            s.cameraPos[1] = w.camera_position.y;
            s.cameraPos[2] = w.camera_position.z;
            s.zoneText = w.zone_text ? w.zone_text : "";
            s.subZoneText = w.subzone_text ? w.subzone_text : "";
        }

        g_snap = s;
        return g_snap;
    }

    const Snapshot& Current() {
        return g_snap;
    }

    int AreaChain(uint32_t area, uint32_t* out, int max) {
        return Api() ? Api()->area_chain(area, out, max) : 0;
    }

    bool ChunkAreaAt(float x, float y, uint32_t& area) {
        area = Api() ? Api()->terrain_area_at(x, y) : 0;
        return area != 0;
    }
}
