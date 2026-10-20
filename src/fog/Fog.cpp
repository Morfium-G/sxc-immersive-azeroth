#include "fog/Fog.h"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <unordered_map>
#include <vector>
#include "SXC/Api/sxc_fog.h"
#include "env/Regional.h"
#include "env/Wind.h"
#include "fog/Far.h"
#include "fog/Field.h"
#include "fog/FogSettings.h"
#include "fog/Wake.h"
#include "wxl/sdk.hpp"

namespace ia::fog {
    namespace {
        constexpr int N = kGridSize;
        constexpr double kColourRefreshMs = 500.0; // the stock colour follows the time of day: colours again at most this often

        Look g_look;
        std::vector<float> g_light(N * N * 4), g_pool(N * N * 4), g_tint(N * N * 4);
        std::vector<float> g_base(N * N), g_cellRgb(N * N * 3);
        std::vector<uint8_t> g_cellHas(N * N);
        uint32_t g_sentVersion = ~0u;
        double g_lastColours = 0.0;
        bool g_sentEmpty = false;
        unsigned g_uploads = 0;
        float g_player[3] = {};

        double NowMs() {
            using namespace std::chrono;
            return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
        }

        const sxc_fog_api* Api() {
            static const sxc_fog_api* s_api =
                wxl::Host() ? static_cast<const sxc_fog_api*>(wxl::Host()->get_service(SXC_FOG_SERVICE, SXC_FOG_SERVICE_VERSION)) : nullptr;
            return s_api;
        }

        int Wrap(int v) {
            const int m = v % N;
            return m < 0 ? m + N : m;
        }

        float Luminance(const float c[3]) { return 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2]; }

        // A kind's colour at a place (kind -1: the "all kinds" colour): the stock fog colour (it follows
        // the time of day), changed by the place's FogSettings. Hue mode keeps the stock colour's
        // brightness with Color's hue and saturation, so day and night still show.
        void FogColour(int kind, uint32_t area, int map, const float stock[3], float out[3]) {
            const fogsettings::Values& v = fogsettings::Resolve(area, map, kind);
            float c[3] = { stock[0], stock[1], stock[2] };

            if (v.colorMode == fogsettings::kColorReplace) {
                for (int i = 0; i < 3; i++) c[i] = v.color[i];
            } else if (v.colorMode == fogsettings::kColorHue) {
                const float wanted = Luminance(v.color);
                const float scale = wanted > 1.0e-4f ? Luminance(stock) / wanted : 0.0f;

                for (int i = 0; i < 3; i++) c[i] = v.color[i] * scale;
            }

            const float grey = Luminance(c);

            for (int i = 0; i < 3; i++) {
                out[i] = (grey + (c[i] - grey) * v.saturation) * v.brightness;
            }
        }

        // The debug view "fog coloured by place" (WXL's): each zone or area a colour from the palette, in
        // the order they're met, kept while the map stays.
        void PlaceColour(uint32_t area, int mapId, float out[3]) {
            static const float kPalette[8][3] = { { 1.0f, 0.15f, 0.15f }, { 0.2f, 1.0f, 0.2f }, { 0.25f, 0.4f, 1.0f }, { 1.0f, 0.95f, 0.2f },
                                                  { 1.0f, 0.3f, 1.0f },   { 0.2f, 1.0f, 1.0f }, { 1.0f, 0.6f, 0.15f }, { 1.0f, 1.0f, 1.0f } };
            static std::unordered_map<uint32_t, int> s_index;
            static int s_indexMap = -2;

            if (s_indexMap != mapId) {
                s_index.clear();
                s_indexMap = mapId;
            }

            const uint32_t place = g_look.debugPlaces == 1 ? (area ? regional::ZoneOf(area) : 0) : area;
            auto found = s_index.find(place);

            if (found == s_index.end()) {
                found = s_index.emplace(place, static_cast<int>(s_index.size()) % 8).first;
            }

            for (int ch = 0; ch < 3; ch++) {
                out[ch] = kPalette[found->second][ch];
            }
        }

        struct PlaceColours {
            float kind[kKindCount][3];
        };

        // The field into the client's three layers.
        void Build(const world::Snapshot& snap) {
            const Grid& grid = Current();
            float stock[3] = { 0.6f, 0.6f, 0.65f };
            Api()->stock_colour(stock);
            float placeColour[3];
            const uint32_t playerArea = snap.areaCount > 0 ? snap.areaChain[0] : 0;
            FogColour(-1, playerArea, snap.mapId, stock, placeColour);

            // Each place's kind colours, once per place per build (area 0: the player's place).
            std::unordered_map<uint32_t, PlaceColours> places;

            auto coloursOf = [&](uint32_t area) -> const PlaceColours& {
                auto found = places.find(area);

                if (found != places.end()) {
                    return found->second;
                }

                PlaceColours pc;

                for (int k = 0; k < kKindCount; k++) {
                    FogColour(k, area ? area : playerArea, snap.mapId, stock, pc.kind[k]);
                }

                return places.emplace(area, pc).first->second;
            };

            // Where the light layer starts: the lowest known ground within floorReach cells, so it
            // doesn't drape up walls.
            const int reach = g_look.floorReach < 0 ? 0 : (g_look.floorReach > 4 ? 4 : g_look.floorReach);

            for (int r = 0; r < N; r++) {
                for (int c = 0; c < N; c++) {
                    const int index = r * N + c;
                    float low = grid.ground[index];

                    for (int dr = -reach; dr <= reach; dr++) {
                        for (int dc = -reach; dc <= reach; dc++) {
                            const int n = Wrap(r + dr) * N + Wrap(c + dc);

                            if (grid.groundKnown[n] && grid.ground[n] < low) {
                                low = grid.ground[n];
                            }
                        }
                    }

                    g_base[index] = low;
                }
            }

            for (int index = 0; index < N * N; index++) {
                float density = 0.0f, weighted = 0.0f, patchy = 0.0f, floorSum = 0.0f;
                float pool = 0.0f, poolDensity = 0.0f, poolWeight = 0.0f;
                float mix[3] = {}, mixWeight = 0.0f;
                const uint32_t area = grid.area[index];
                PlaceColours colours = coloursOf(area);
                uint32_t otherArea = 0;
                const float otherShare = BlendOf(index, otherArea);

                // The place's colours, blended toward the other place around like its settings.
                if (otherShare > 0.0f && otherArea != area) {
                    const PlaceColours& other = coloursOf(otherArea);

                    for (int k = 0; k < kKindCount; k++) {
                        for (int ch = 0; ch < 3; ch++) {
                            colours.kind[k][ch] += (other.kind[k][ch] - colours.kind[k][ch]) * otherShare;
                        }
                    }
                }

                for (int k = 0; k < kKindCount; k++) {
                    const KindSettings ks = SettingsAt(k, index);

                    if (!Settings(k).enabled) {
                        continue;
                    }

                    const float a = grid.amount[k][index];
                    const float heavy = grid.pool[k][index];
                    const float lightShare = 1.0f - HeavyShare(k, index);
                    const float d = a * ks.density * lightShare;
                    density += d;
                    weighted += d * ks.thickness;
                    patchy += d * ks.patchiness;
                    // Water fog and steam start at the height they came from, never above the ground here.
                    const float kindFloor = grid.floor[k] ? std::fmin(grid.floor[k][index], grid.ground[index]) : g_base[index];
                    floorSum += d * kindFloor;
                    pool += heavy * ks.thickness;
                    poolDensity += heavy * ks.density;
                    poolWeight += heavy;
                    const float share = (a * lightShare + heavy) * ks.density;

                    for (int ch = 0; ch < 3; ch++) {
                        mix[ch] += share * colours.kind[k][ch];
                    }

                    mixWeight += share;
                }

                // Debug: the place's palette colour instead, blended toward the other place around by
                // the same share as its settings (where they mix shows as a mix).
                if (g_look.debugPlaces && mixWeight > 0.0f) {
                    float own[3], other[3];
                    PlaceColour(area, snap.mapId, own);
                    PlaceColour(otherShare > 0.0f && otherArea ? otherArea : area, snap.mapId, other);

                    for (int ch = 0; ch < 3; ch++) {
                        mix[ch] = (own[ch] + (other[ch] - own[ch]) * otherShare) * mixWeight;
                    }
                }

                const bool blocked = grid.block[index] == 3;
                const bool unready = !grid.groundKnown[index] || grid.fresh[index];
                float* light = &g_light[index * 4];
                float* heavy = &g_pool[index * 4];
                light[0] = blocked || unready ? 0.0f : density;
                light[1] = density > 0.0f ? floorSum / density : g_base[index];
                light[2] = unready ? -1.0f : (density > 0.0f ? weighted / density : 1.0f); // not ready: the far map shows
                light[3] = density > 0.0f ? patchy / density : 0.0f;
                const bool pooled = !blocked && !unready && pool >= 0.02f && poolWeight > 0.0f;
                heavy[0] = pooled ? poolDensity / poolWeight : 0.0f;
                heavy[1] = grid.ground[index] + pool;
                heavy[2] = std::fmin(std::fmax(pool * g_look.poolSoftness, 0.3f), 2.0f);
                heavy[3] = 0.0f;
                g_cellHas[index] = mixWeight > 0.0f ? 1 : 0;

                for (int ch = 0; ch < 3; ch++) {
                    g_cellRgb[index * 3 + ch] = mixWeight > 0.0f ? mix[ch] / mixWeight : placeColour[ch];
                }
            }

            // Cells without fog take a fog-bearing neighbour's colour (else the place's): the density
            // fades into them by the texture filter, and with the place's colour every patch got a rim
            // in it (WXL: red around white steam in Burning Steppes).
            for (int r = 0; r < N; r++) {
                for (int c = 0; c < N; c++) {
                    const int index = r * N + c;
                    const float* rgb = &g_cellRgb[index * 3];

                    for (int dr = -1; dr <= 1 && !g_cellHas[index]; dr++) {
                        for (int dc = -1; dc <= 1; dc++) {
                            const int n = Wrap(r + dr) * N + Wrap(c + dc);

                            if (g_cellHas[n]) {
                                rgb = &g_cellRgb[n * 3];
                                dr = 2;
                                break;
                            }
                        }
                    }

                    for (int ch = 0; ch < 3; ch++) {
                        g_tint[index * 4 + ch] = rgb[ch];
                    }

                    g_tint[index * 4 + 3] = 0.0f;
                }
            }
        }

        // The far map's colours: a kind's at a place, over this frame's stock colour.
        float g_farStock[3] = { 0.6f, 0.6f, 0.65f };
        int g_farMapId = -1;
        bool g_farSent = false;

        void FarColour(int kind, uint32_t area, float out[3]) {
            if (g_look.debugPlaces) {
                PlaceColour(area, g_farMapId, out);
                return;
            }

            FogColour(kind, area, g_farMapId, g_farStock, out);
        }

        const sxc_fog_api* FarApi() {
            const sxc_fog_api* api = Api();
            return api && api->struct_size >= offsetof(sxc_fog_api, set_far) + sizeof(api->set_far) ? api : nullptr;
        }

        void ClearFar() {
            if (g_farSent && FarApi()) {
                FarApi()->set_far(nullptr);
            }

            g_farSent = false;
        }

        // The far map: rebuilt twice a second by fog/Far; sent when it changed.
        void DrawFar(const world::Snapshot& snap) {
            if (!g_look.farFog || !FarApi()) {
                ClearFar();
                return;
            }

            Api()->stock_colour(g_farStock);
            g_farMapId = snap.mapId;
            farmap::Map map;
            bool any = false;

            if (!farmap::Update(&FarColour, snap.playerPos, map, any)) {
                return;
            }

            if (!any) {
                ClearFar();
                return;
            }

            sxc_fog_far out = {};
            out.struct_size = sizeof(out);
            out.size = static_cast<uint32_t>(map.size);
            out.cell_size = map.cellSize;
            out.first_i = map.firstI;
            out.first_j = map.firstJ;
            out.layer = map.layer;
            out.colour = map.colour;
            FarApi()->set_far(&out);
            g_farSent = true;
        }

        // Cells that just started fade in over the far fog: before, a cell switched from the far fog
        // (the zone's typical fog on the low ground around) to its own in one frame, and wherever the two
        // differed that popped (Seyri, Duskwood's area fog). True when any share changed.
        std::vector<float> g_ready(N * N, 0.0f);

        bool StepReady(float dt) {
            const Grid& grid = Current();
            const float step = g_look.fadeIn > 0.05f ? dt / g_look.fadeIn : 1.0f;
            bool changed = false;

            for (int index = 0; index < N * N; index++) {
                float& r = g_ready[index];
                const bool unready = !grid.groundKnown[index] || grid.fresh[index];

                if (unready) {
                    changed = changed || r != 0.0f;
                    r = 0.0f;
                } else if (r < 1.0f) {
                    r = std::fmin(1.0f, r + step);
                    changed = true;
                }
            }

            return changed;
        }

        bool AnyFog() {
            const Grid& grid = Current();

            for (int k = 0; k < kKindCount; k++) {
                if (grid.maxAmount[k] > 0.0f || (grid.pool[k] && GetStats().cellsWithFog[k] > 0)) {
                    return true;
                }
            }

            return false;
        }
    }

    Look& LookSettings() { return g_look; }

    void Draw(float dt, const world::Snapshot& snap) {
        const sxc_fog_api* api = Api();

        if (!api || !snap.inWorld) {
            return;
        }

        g_player[0] = snap.playerPos[0];
        g_player[1] = snap.playerPos[1];
        g_player[2] = snap.playerPos[2];
        const Grid& grid = Current();
        bool fogNear = false;

        // The field: when it changed, and the colours twice a second (they follow the time of day).
        if (grid.ground && grid.amount[0]) {
            const bool any = AnyFog();
            fogNear = any;
            const double now = NowMs();
            const bool fading = StepReady(dt);
            const bool rebuild = grid.version != g_sentVersion || now - g_lastColours > kColourRefreshMs;

            // Rebuilt when the field changed and for the colours; sent again as it is while cells fade in.
            if (any && (rebuild || fading)) {
                if (rebuild) {
                    Build(snap);
                    g_sentVersion = grid.version;
                    g_lastColours = now;
                }

                sxc_fog_field field = {};
                field.struct_size = sizeof(field);
                field.size = N;
                field.cell_size = kCellSize;
                field.first_i = grid.firstI;
                field.first_j = grid.firstJ;
                field.light = g_light.data();
                field.pool = g_pool.data();
                field.colour = g_tint.data();
                field.ready = g_ready.data();
                api->set_field(&field);
                g_sentEmpty = false;
                g_uploads++;
            } else if (!any && !g_sentEmpty) {
                api->set_field(nullptr);
                g_sentEmpty = true;
            }
        }

        DrawFar(snap);
        wake::Update(dt, snap, fogNear);

        // The look, every frame. The patches drift with the steady wind (the client sums the drift).
        const float bearing = wind::BearingDegrees() * 3.14159265f / 180.0f;
        const float speed = 3.0f * wind::SteadyGround() * g_look.drift; // yd per second
        sxc_fog_look look = {};
        look.struct_size = sizeof(look);
        look.enabled = g_look.enabled ? WXL_TRUE : WXL_FALSE;
        look.colour_mode = 0; // the extra haze: the place's stock colour
        look.brightness = g_look.brightness;
        look.density_scale = g_look.densityScale;
        look.noise_amount = g_look.noiseAmount;
        look.noise_tile = g_look.noiseTile;
        look.drift[0] = std::cos(bearing) * speed; // bearing clockwise from north (+x): east is -y
        look.drift[1] = -std::sin(bearing) * speed;
        look.far_distance = g_look.farDistance;
        look.haze_density = g_look.hazeDensity;
        look.haze_base = g_player[2] - 2.0f;
        look.haze_thickness = g_look.hazeThickness;
        look.debug = g_look.debugNearFar ? 2 : (g_look.debug ? 1 : 0);
        look.step_length = g_look.stepLength;
        look.method = g_look.froxels ? 1 : 0;
        look.max_steps = g_look.maxSteps;
        look.handover = g_look.handover;
        api->set_look(&look);
    }

    void Stop() {
        if (const sxc_fog_api* api = Api()) {
            sxc_fog_look look = {};
            look.struct_size = sizeof(look);
            look.enabled = WXL_FALSE;
            api->set_look(&look);
            api->set_field(nullptr);
        }

        ClearFar();
        farmap::Reset();
        wake::Reset();
        g_sentEmpty = true;
        g_sentVersion = ~0u;
    }

    bool Available() {
        return Api() && Api()->available();
    }

    const char* Status() {
        return Api() ? Api()->status() : "the client has no sxc.fog (an older client)";
    }

    unsigned Uploads() { return g_uploads; }
}
