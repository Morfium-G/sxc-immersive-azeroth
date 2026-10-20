#include "World.h"

#include <cstddef>
#include <vector>
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

    // The table with the point queries (appended to sxc.world), or null on an older client.
    const sxc_world_api* PointApi() {
        const sxc_world_api* api = Api();
        return api && api->struct_size >= offsetof(sxc_world_api, area_name) + sizeof(api->area_name) ? api : nullptr;
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

    const char* AreaName(uint32_t area) {
        const sxc_world_api* api = PointApi();
        const char* name = api ? api->area_name(area) : nullptr;
        return name ? name : "";
    }

    bool Trace(const float from[3], const float to[3], float& fraction) {
        fraction = 1.0f;
        const sxc_world_api* api = PointApi();

        if (!api) {
            return false;
        }

        const wxl_vec3 a{ from[0], from[1], from[2] }, b{ to[0], to[1], to[2] };
        return api->trace(&a, &b, SXC_TRACE_TERRAIN_AND_WMO, &fraction) != 0;
    }

    bool HasPointQueries() {
        return PointApi() != nullptr;
    }

    namespace {
        // The table with project and the test weather (appended later still), or null.
        const sxc_world_api* ViewApi() {
            const sxc_world_api* api = Api();
            return api && api->struct_size >= offsetof(sxc_world_api, weather_test) + sizeof(api->weather_test) ? api : nullptr;
        }
    }

    bool Project(const float pos[3], float width, float height, float& x, float& y) {
        const sxc_world_api* api = ViewApi();
        const wxl_vec3 p{ pos[0], pos[1], pos[2] };
        float fx = 0.0f, fy = 0.0f;

        if (!api || !api->project(&p, &fx, &fy)) {
            return false;
        }

        x = fx * width;
        y = fy * height;
        return true;
    }

    bool SetClientWeather(int type, float intensity) {
        const sxc_world_api* api = ViewApi();

        if (!api) {
            return false;
        }

        api->set_weather(type, intensity);
        return true;
    }

    int ClientWeatherTest() {
        const sxc_world_api* api = ViewApi();
        return api ? api->weather_test() : -1;
    }

    int Units(const float centre[3], float range, Unit* out, int max) {
        const sxc_world_api* api = Api();

        if (!api || api->struct_size < offsetof(sxc_world_api, units) + sizeof(api->units) || !out || max <= 0) {
            return 0;
        }

        static std::vector<sxc_world_unit> s_units;
        s_units.resize(static_cast<size_t>(max));
        const wxl_vec3 c{ centre[0], centre[1], centre[2] };
        const int count = api->units(&c, range, s_units.data(), max, sizeof(sxc_world_unit));

        for (int i = 0; i < count; i++) {
            const sxc_world_unit& u = s_units[i];
            Unit& o = out[i];
            o.guid = u.guid;
            o.pos[0] = u.position.x;
            o.pos[1] = u.position.y;
            o.pos[2] = u.position.z;
            o.boundingRadius = u.bounding_radius;
            o.effectiveRadius = u.effective_radius;
            o.distance = u.distance;
            o.mounted = u.mounted != WXL_FALSE;
            o.isPlayer = u.is_player != WXL_FALSE;
        }

        return count;
    }
}

namespace ia::terrain {
    using world::PointApi;

    bool HeightAt(float x, float y, float& z) {
        const sxc_world_api* api = PointApi();
        return api && api->terrain_height(x, y, &z);
    }

    namespace {
        bool Sample(float x, float y, sxc_terrain_sample& s) {
            const sxc_world_api* api = PointApi();
            s = sxc_terrain_sample{};
            s.struct_size = sizeof(s);
            return api && api->terrain_sample(x, y, &s);
        }

        Surface SurfaceOf(const sxc_terrain_sample& s, int layer) {
            Surface out;
            out.area = s.area;
            out.texture = s.layers[layer].texture;
            out.groundEffect = s.layers[layer].ground_effect;
            out.terrainType = s.layers[layer].terrain_type;
            return out;
        }
    }

    bool LayerWeightsAt(float x, float y, LayerWeights& out) {
        sxc_terrain_sample s;
        out = LayerWeights{};

        if (!Sample(x, y, s)) {
            return false;
        }

        out.serial = s.chunk_serial;
        out.layers = s.layer_count < 4 ? s.layer_count : 4;
        out.dominant = s.dominant_layer;

        for (int l = 0; l < out.layers; l++) {
            out.weight[l] = s.layers[l].weight;
            out.surface[l] = SurfaceOf(s, l);
        }

        return true;
    }

    bool SurfaceAt(float x, float y, Surface& out) {
        sxc_terrain_sample s;
        out = Surface{};

        if (!Sample(x, y, s)) {
            return false;
        }

        out.area = s.area;

        if (s.dominant_layer >= 0 && s.dominant_layer < s.layer_count) {
            out = SurfaceOf(s, s.dominant_layer);
        }

        return true;
    }

    bool LiquidAt(float x, float y, float& z, uint32_t& liquidType) {
        const sxc_world_api* api = PointApi();
        return api && api->terrain_liquid(x, y, &z, &liquidType);
    }

    int LiquidCategoryOf(uint32_t liquidType) {
        const sxc_world_api* api = PointApi();
        return api ? api->liquid_category(liquidType, nullptr) : -1;
    }

    const char* LiquidNameOf(uint32_t liquidType) {
        const sxc_world_api* api = PointApi();
        const char* name = nullptr;

        if (api) {
            api->liquid_category(liquidType, &name);
        }

        return name ? name : "";
    }
}
