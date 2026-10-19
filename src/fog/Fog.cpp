#include "fog/Fog.h"

#include <chrono>
#include <cmath>
#include <unordered_map>
#include <vector>
#include "SXC/Api/sxc_fog.h"
#include "env/Wind.h"
#include "fog/Field.h"
#include "fog/FogSettings.h"
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

                const bool blocked = grid.block[index] == 3;
                const bool unready = !grid.groundKnown[index] || grid.fresh[index];
                float* light = &g_light[index * 4];
                float* heavy = &g_pool[index * 4];
                light[0] = blocked || unready ? 0.0f : density;
                light[1] = density > 0.0f ? floorSum / density : g_base[index];
                light[2] = density > 0.0f ? weighted / density : 1.0f;
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

    void Draw(const world::Snapshot& snap) {
        const sxc_fog_api* api = Api();

        if (!api || !snap.inWorld) {
            return;
        }

        g_player[0] = snap.playerPos[0];
        g_player[1] = snap.playerPos[1];
        g_player[2] = snap.playerPos[2];
        const Grid& grid = Current();

        // The field: when it changed, and the colours twice a second (they follow the time of day).
        if (grid.ground && grid.amount[0]) {
            const bool any = AnyFog();
            const double now = NowMs();

            if (any && (grid.version != g_sentVersion || now - g_lastColours > kColourRefreshMs)) {
                Build(snap);
                sxc_fog_field field = {};
                field.struct_size = sizeof(field);
                field.size = N;
                field.cell_size = kCellSize;
                field.first_i = grid.firstI;
                field.first_j = grid.firstJ;
                field.light = g_light.data();
                field.pool = g_pool.data();
                field.colour = g_tint.data();
                api->set_field(&field);
                g_sentVersion = grid.version;
                g_lastColours = now;
                g_sentEmpty = false;
                g_uploads++;
            } else if (!any && !g_sentEmpty) {
                api->set_field(nullptr);
                g_sentEmpty = true;
            }
        }

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
        look.debug = g_look.debug ? 1 : 0;
        look.step_length = g_look.stepLength;
        look.method = g_look.froxels ? 1 : 0;
        look.max_steps = g_look.maxSteps;
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
