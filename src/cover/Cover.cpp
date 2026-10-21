#include "cover/Cover.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include "SXC/Api/sxc_cover.h"
#include "env/Fields.h"
#include "env/Materials.h"
#include "wxl/sdk.hpp"

namespace ia::cover {
    namespace {
        Look g_look;
        std::vector<Entry> g_entries;
        std::vector<sxc_cover_entry> g_gpu;
        std::unordered_map<uint64_t, uint16_t> g_index; // (material, snow) -> entry index (1..)
        bool g_entriesDirty = true;
        bool g_full = false;           // more distinct covers than the client holds
        unsigned g_resolved = 0;
        uint32_t g_tableGeneration = 0, g_materialGeneration = 0;
        uint32_t g_mapsVersion = ~0u;
        int g_map = -2;
        bool g_resolverSet = false, g_running = false;

        const sxc_cover_api* Api() {
            static const sxc_cover_api* s_api =
                wxl::Host() ? static_cast<const sxc_cover_api*>(wxl::Host()->get_service(SXC_COVER_SERVICE, SXC_COVER_SERVICE_VERSION)) : nullptr;
            return s_api;
        }

        // The entry for a resolved row: one per ground material (and whether its cover is snow at this
        // place, which changes how it melts), made the first time a chunk names it.
        uint16_t EntryFor(const covertable::Values& v, uint32_t area, int map) {
            const bool snow = covertable::IsSnow(v.coverMaterial, area, map);
            const uint64_t key = (static_cast<uint64_t>(v.material) << 1) | (snow ? 1u : 0u);
            auto found = g_index.find(key);

            if (found != g_index.end()) {
                g_entries[found->second - 1].uses++;
                return found->second;
            }

            if (g_entries.size() >= SXC_COVER_MAX_ENTRIES) {
                g_full = true;
                return 0;
            }

            sxc_cover_entry e = {};
            e.depth = v.depth;
            e.max_slope = v.maxSlope;
            e.slope_fade = v.slopeFade;
            e.drift = v.driftNoise;
            e.tint[0] = ((v.tintColor >> 16) & 0xFF) / 255.0f;
            e.tint[1] = ((v.tintColor >> 8) & 0xFF) / 255.0f;
            e.tint[2] = (v.tintColor & 0xFF) / 255.0f;
            e.tint_strength = v.tintStrength;
            e.breakup = v.edgeBreakup;
            e.wetness = v.wetness;
            e.moisture_min = v.moistureMin;
            e.rest_moisture = materials::Get(v.material).restMoisture;
            e.texture = v.coverTexture >= 1 && v.coverTexture <= SXC_COVER_MAX_TEXTURES ? v.coverTexture : 0;
            e.z_offset = v.zOffset;
            e.stiffness = v.stiffness;
            e.rim = v.rim;
            e.relax_seconds = v.relaxSeconds;
            // SurfaceCover's water and vertex colour flags are sxc.cover's bits.
            e.flags = (v.flags & (covertable::kFlagPreventUnderwater | covertable::kFlagPreventOnLand | covertable::kFlagUseVertexColor)) |
                      (snow ? SXC_COVER_SNOW : 0u);

            Entry info;
            info.material = v.material;
            info.snow = snow;
            info.values = v;
            info.uses = 1;
            g_entries.push_back(info);
            g_gpu.push_back(e);
            const uint16_t index = static_cast<uint16_t>(g_entries.size());
            g_index.emplace(key, index);
            g_entriesDirty = true;
            return index;
        }

        // The client's question about one chunk: per layer, the painted cover (a row with Depth) and the
        // look fallen snow takes there (the layer's own row if its cover is snow, else the place's snow).
        void WXL_CALL Resolve(void*, sxc_cover_chunk* io) {
            if (!io || io->struct_size < sizeof(sxc_cover_chunk)) {
                return;
            }

            g_resolved++;
            const int count = io->layer_count < SXC_COVER_MAX_LAYERS ? io->layer_count : SXC_COVER_MAX_LAYERS;

            for (int l = 0; l < count; l++) {
                const sxc_cover_layer& layer = io->layers[l];
                const covertable::Values painted = covertable::Resolve(io->area, io->map_id, layer.texture, layer.ground_effect, layer.terrain_type);

                if (painted.depth > 0.0f) {
                    io->painted[l] = EntryFor(painted, io->area, io->map_id);
                }

                const covertable::Values look = covertable::SnowLook(io->area, io->map_id, layer.texture, layer.ground_effect, layer.terrain_type);

                if (look.material) {
                    io->snow_look[l] = EntryFor(look, io->area, io->map_id);
                }
            }
        }

        // The tables changed (or the map): the entries start over, the textures go again, every chunk asks again.
        void Restart(const sxc_cover_api* api) {
            g_entries.clear();
            g_gpu.clear();
            g_index.clear();
            g_full = false;
            g_resolved = 0;
            g_entriesDirty = true;

            std::vector<const char*> paths;
            std::vector<uint32_t> flags;
            const int textures = covertable::CoverTextureCount();

            for (int k = 1; k <= textures && k <= SXC_COVER_MAX_TEXTURES; k++) {
                paths.push_back(covertable::CoverTexturePath(k));
                flags.push_back(covertable::CoverTextureIgnoresSpecular(k) ? 0u : SXC_COVER_TEXTURE_GLOSS);
            }

            api->set_textures(paths.empty() ? nullptr : paths.data(), flags.empty() ? nullptr : flags.data(), static_cast<uint32_t>(paths.size()));
            api->invalidate();
        }

        void SendLook(const sxc_cover_api* api, bool enabled) {
            const fields::CoverMaps& maps = fields::CoverGround();
            sxc_cover_look look = {};
            look.struct_size = sizeof(look);
            look.enabled = enabled ? WXL_TRUE : WXL_FALSE;
            look.fallen_snow = g_look.fallenSnow ? WXL_TRUE : WXL_FALSE;
            look.debug = g_look.debug;
            look.brightness = g_look.brightness;
            look.wet_darkening = g_look.wetDarkening;
            look.thin_depth = g_look.thinDepth;
            look.depth_scale = g_look.depthScale;
            look.breakup_scale = g_look.breakupScale;
            look.drift_scale = g_look.driftScale;
            look.texture_tile = 0.0f; // the terrain layers'
            look.tess_detail = g_look.depth ? g_look.tessDetail : 0.0f;
            look.tess_reach = g_look.tessReach;
            look.tess_most = static_cast<float>(g_look.tessMost);
            look.trails = g_look.trails ? WXL_TRUE : WXL_FALSE;
            look.trail_persist = g_look.trailPersist;
            look.trail_slur = g_look.trailSlur;
            look.trail_rim = g_look.trailRim;
            look.trail_reach = g_look.trailReach;
            look.trail_carve = g_look.trailCarve;
            look.trail_lift = g_look.trailLift;

            for (int k = 0; k < 3; k++) {
                look.outside[k] = maps.outside[k];
            }

            api->set_look(&look);
        }
    }

    Look& LookSettings() { return g_look; }
    const std::vector<Entry>& Entries() { return g_entries; }

    void Update(const world::Snapshot& snap) {
        const sxc_cover_api* api = Api();

        if (!api) {
            return;
        }

        if (!g_resolverSet) {
            api->set_resolver(Resolve, nullptr);
            g_resolverSet = true;
        }

        g_running = true;

        if (covertable::Generation() != g_tableGeneration || materials::Generation() != g_materialGeneration || snap.mapId != g_map) {
            g_tableGeneration = covertable::Generation();
            g_materialGeneration = materials::Generation();
            g_map = snap.mapId;
            Restart(api);
        }

        if (g_entriesDirty) {
            api->set_entries(g_gpu.empty() ? nullptr : g_gpu.data(), static_cast<uint32_t>(g_gpu.size()));
            g_entriesDirty = false;
        }

        const fields::CoverMaps& maps = fields::CoverGround();

        if (maps.version != g_mapsVersion) {
            g_mapsVersion = maps.version;
            sxc_cover_map near = {};
            near.struct_size = sizeof(near);
            near.size = static_cast<uint32_t>(maps.size);
            near.cell_size = maps.cellSize;
            near.first_i = maps.firstI;
            near.first_j = maps.firstJ;
            near.cells = maps.cells;
            api->set_map(maps.cells && maps.size > 0 ? &near : nullptr);

            sxc_cover_map far = {};
            far.struct_size = sizeof(far);
            far.size = static_cast<uint32_t>(maps.farSize);
            far.cell_size = maps.farCellSize;
            far.first_i = maps.farFirstI;
            far.first_j = maps.farFirstJ;
            far.cells = maps.farCells;
            api->set_far(maps.farCells && maps.farSize > 0 && maps.farSize <= SXC_COVER_MAX_MAP ? &far : nullptr);
        }

        SendLook(api, g_look.enabled);
    }

    void Stop() {
        const sxc_cover_api* api = Api();

        if (!api || !g_running) {
            return;
        }

        SendLook(api, false);
        api->set_map(nullptr);
        api->set_far(nullptr);
        g_mapsVersion = ~0u;
        g_running = false;
    }

    bool Available() {
        const sxc_cover_api* api = Api();
        return api && api->available();
    }

    const char* Status() {
        static std::string s_line;
        const sxc_cover_api* api = Api();

        if (!api) {
            return "the client has no sxc.cover (update the client)";
        }

        s_line = api->status();

        if (g_full) {
            s_line += "; more covers than the client holds (127): some draw bare";
        }

        return s_line.c_str();
    }

    unsigned Resolved() { return g_resolved; }

    int TextureState(int texture) {
        const sxc_cover_api* api = Api();
        return api && texture > 0 ? api->texture_state(static_cast<uint32_t>(texture)) : 0;
    }
}
