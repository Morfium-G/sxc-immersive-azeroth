#include "sky/Bodies.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "climate/Climate.h"
#include "sky/BodyTables.h"
#include "sky/Clock.h"
#include "sky/Ephemeris.h"
#include "sky/Paths.h"
#include "sky/SkyApi.h"

namespace ia::sky::bodies {
    bool g_showRoutes = false;
    bool g_debugHideMoonGlare = false;
    bool g_glaresFollow = true;
    bool g_bodiesDriveLight = true;
    bool g_debugAllOwnDraw = false;
    bool g_hideZoneSkybox = false;
    bool g_hideAllSkyboxes = false;

    namespace {
        constexpr float kDistance = SXC_SKY_DISTANCE;
        constexpr float kPi = 3.14159265f;
        constexpr float kLightHorizonDegrees = 4.0f; // a driving body hands over the light within +-this of the horizon

        bool g_enabled = true;
        const std::vector<const bodytables::Body*>* g_list = nullptr; // this frame's set
        int g_setId = -1;
        float g_northOffset = 0.0f;
        char g_where[96] = "-";
        Info g_info[kMaxBodies];
        int g_count = 0;
        bool g_profileHideZone = false, g_profileHideAll = false;
        bool g_hideSunGlare = false, g_hideMoonGlare = false; // the set's bodies say so (Flags 2)
        char g_lightSource[64] = "stock";
        sxc_sky_lighting g_lastLighting = {};

        // A body as it goes to sxc.sky.
        struct Draw {
            float dir[3] = { 0.0f, 0.0f, 1.0f };
            float sizeDegrees = 0.0f;
            uint32_t color = 0xFFFFFFFFu;
            std::string texture;
            bool additive = false;
            float rotation = 0.0f;
        };

        std::vector<Draw> g_afterDome; // queued in pass 0, drawn in pass 1

        uint32_t Tint(uint32_t a, uint32_t b) { // ARGB x ARGB per channel
            uint32_t out = 0;

            for (int s = 0; s < 32; s += 8) {
                out |= ((((a >> s) & 0xFF) * ((b >> s) & 0xFF) + 127) / 255) << s;
            }

            return out;
        }

        uint32_t ScaleAlpha(uint32_t color, float k) {
            const uint32_t a = static_cast<uint32_t>(((color >> 24) & 0xFF) * k + 0.5f);
            return (color & 0x00FFFFFFu) | (a << 24);
        }

        uint32_t ScaleRgb(uint32_t color, float k) {
            uint32_t out = color & 0xFF000000u;

            for (int s = 0; s < 24; s += 8) {
                out |= static_cast<uint32_t>(((color >> s) & 0xFF) * k + 0.5f) << s;
            }

            return out;
        }

        bool StockRoleOf(bodytables::StockRole r, sxc_sky_role& out) {
            switch (r) {
                case bodytables::StockRole::Sun: out = SXC_SKY_SUN; return true;
                case bodytables::StockRole::WhiteLady: out = SXC_SKY_WHITE_LADY; return true;
                case bodytables::StockRole::BlueChild: out = SXC_SKY_BLUE_CHILD; return true;
                default: return false;
            }
        }

        // The stock body textures (DayNight's sky init).
        const char* StockTexture(bodytables::StockRole r) {
            switch (r) {
                case bodytables::StockRole::Sun: return "Textures\\sunCenter.blp";
                case bodytables::StockRole::WhiteLady: return "Textures\\moon.blp";
                case bodytables::StockRole::BlueChild: return "Textures\\moon02.blp";
                default: return "";
            }
        }

        // Texture and colour of a body: the stock role's live colour (the zone's sun colour band, the
        // storm fade), tinted by the row. The stock Blue Child's colour is never set (black, the
        // client bug): it counts as white. Flags 1 (own colour): the row's Color, keeping only the
        // client's alpha.
        bool BaseLook(const bodytables::Body& b, Draw& d) {
            sxc_sky_role role;
            uint32_t base = StockRoleOf(b.role, role) ? Stock(role).color : 0xFFFFFFFFu;

            if ((base & 0x00FFFFFFu) == 0 || (b.flags & bodytables::kBodyOwnColor)) {
                base |= 0x00FFFFFFu;
            }

            d.color = Tint(base, b.color);
            d.texture = !b.texture.empty() ? b.texture : StockTexture(b.role);
            return !d.texture.empty();
        }

        void Send(const Draw& d, bool afterDome) {
            sxc_sky_body body = {};
            body.struct_size = sizeof(body);
            body.texture = d.texture.c_str();
            body.direction = wxl_vec3{ d.dir[0], d.dir[1], d.dir[2] };
            body.size_degrees = d.sizeDegrees;
            body.color = d.color;
            body.blend = d.additive ? 1 : 0;
            body.rotation_degrees = d.rotation;
            body.zenith_up = afterDome;
            Api()->draw_body(&body);
        }

        // Drawn after the dome: opted in, or needing what the stock draw can't do.
        bool OwnDraw(const bodytables::Body& b) {
            return g_debugAllOwnDraw || (b.flags & bodytables::kBodyOwnDraw) || b.blend == 1 || b.rotation != 0.0f || b.rotationPerDay != 0.0f;
        }

        // The body's route over the next day (or its cycle): faint dots where it will be.
        void DrawRoute(const bodytables::Body& b, const Draw& look, double nowBase) {
            const double span = ephemeris::RouteSpanDays(b) * 1440.0;

            for (int k = 1; k < kRouteSlots; k++) {
                const ephemeris::Place p = ephemeris::Where(b, ephemeris::At(nowBase + span * k / kRouteSlots), g_northOffset);

                if (!p.ok || p.visibility <= 0.0f) {
                    continue;
                }

                Draw dot = look;

                for (int c = 0; c < 3; c++) {
                    dot.dir[c] = p.dir[c];
                }

                dot.sizeDegrees = p.sizeDegrees * 0.15f;
                dot.color = (look.color & 0x00FFFFFFu) | 0x80000000u;
                dot.additive = false;
                dot.rotation = 0.0f;
                Send(dot, false);
            }
        }

        // How much it's day, 0..1, from the client's own sun: what the sky's colours follow,
        // whatever our sun does. Night below -6 degrees, full day from +10.
        float DayAmount(const float* cam) {
            const sxc_sky_stock sun = Stock(SXC_SKY_SUN);
            const float d[3] = { sun.position.x - cam[0], sun.position.y - cam[1], sun.position.z - cam[2] };
            const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);

            if (!(len > 1e-4f)) {
                return 0.0f;
            }

            const float z = d[2] / len;
            const float el = std::asin(z > 1.0f ? 1.0f : (z < -1.0f ? -1.0f : z)) * 180.0f / kPi;
            float t = (el + 6.0f) / 16.0f;
            t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
            return t * t * (3.0f - 2.0f * t);
        }

        bool Overlaps(const float a[3], float ra, const float b[3], float rb) {
            const float cosd = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
            return std::acos(cosd > 1.0f ? 1.0f : (cosd < -1.0f ? -1.0f : cosd)) < ra + rb;
        }

        void DrawOurs() {
            g_count = 0;
            g_afterDome.clear();

            if (!g_list) {
                return;
            }

            float cam[3];
            Camera(cam);
            const ephemeris::Moment now = ephemeris::Now();
            const float day = DayAmount(cam);

            // Pass 1: where every body is, and its look.
            struct Ready {
                bool draw = false;
                Draw d;
                float order = 0.0f;
                float alpha = 1.0f;
                float rgb = 1.0f;
                uint32_t id = 0;
                float radius = 0.0f; // for the overlap test, radians
                const bodytables::Body* body = nullptr;
                bool own = false;
            };

            Ready ready[kMaxBodies];
            ephemeris::Place places[kMaxBodies];

            for (size_t i = 0; i < g_list->size() && g_count < kMaxBodies; i++) {
                const bodytables::Body& b = *(*g_list)[i];
                const int k = g_count++;
                Info& info = g_info[k];
                info = Info{};
                info.name = b.name.c_str();
                ready[k].id = b.id;
                ready[k].order = static_cast<float>(b.order);
                ready[k].body = &b;
                ready[k].own = info.ownDraw = OwnDraw(b);

                const ephemeris::Place& p = places[k] = ephemeris::Where(b, now, g_northOffset);
                info.status = p.status;
                info.visibility = p.visibility;
                info.windowOpen = p.windowOpen;

                if (!p.ok) {
                    continue;
                }

                Draw& d = ready[k].d;

                if (!BaseLook(b, d)) {
                    info.status = "no texture (check the path)";
                    continue;
                }

                if (g_showRoutes) {
                    DrawRoute(b, d, now.baseMinutes);
                }

                if (p.visibility <= 0.0f) {
                    continue;
                }

                // A stock body the row doesn't change keeps the client's placement exactly; everything
                // else goes where the ephemeris says.
                float dir[3] = { p.dir[0], p.dir[1], p.dir[2] };
                float sizeDegrees = p.sizeDegrees;
                sxc_sky_role role;

                if (p.stockExact && StockRoleOf(b.role, role)) {
                    const sxc_sky_stock s = Stock(role);
                    const float rel[3] = { s.position.x - cam[0], s.position.y - cam[1], s.position.z - cam[2] };
                    const float dist = std::sqrt(rel[0] * rel[0] + rel[1] * rel[1] + rel[2] * rel[2]);

                    if (!(dist > 0.001f)) {
                        continue;
                    }

                    for (int c = 0; c < 3; c++) {
                        dir[c] = rel[c] / dist;
                    }

                    sizeDegrees = 2.0f * std::atan(0.5f * s.scale / dist) * 180.0f / kPi;
                }

                for (int c = 0; c < 3; c++) {
                    d.dir[c] = dir[c];
                }

                d.sizeDegrees = sizeDegrees;
                ready[k].alpha = p.alpha;
                ready[k].draw = sizeDegrees > 0.0f;
                ready[k].radius = sizeDegrees * 0.5f * kPi / 180.0f;
                info.drawn = true;
                paths::ToCompass(dir, &info.azimuth, &info.elevation);
                info.sizeDegrees = sizeDegrees;
            }

            // Pass 2: followers with relative keys dim their leader and go in front of / behind it.
            for (int k = 0; k < g_count; k++) {
                const ephemeris::Place& p = places[k];

                if (!p.ok || !p.leaderId) {
                    continue;
                }

                for (int j = 0; j < g_count; j++) {
                    if (ready[j].id != p.leaderId) {
                        continue;
                    }

                    ready[j].rgb *= p.leaderBrightness; // darker, never see-through

                    if (p.relativeOrder) {
                        ready[k].order = ready[j].order + (p.relativeOrder > 0 ? 0.5f : -0.5f);
                    }
                }
            }

            // Pass 3: draw back to front (equal order: set order).
            int sorted[kMaxBodies];

            for (int k = 0; k < g_count; k++) {
                sorted[k] = k;
            }

            for (int a = 1; a < g_count; a++) { // insertion sort: stable, a handful of bodies
                for (int b = a; b > 0 && ready[sorted[b]].order < ready[sorted[b - 1]].order; b--) {
                    std::swap(sorted[b], sorted[b - 1]);
                }
            }

            for (int s = 0; s < g_count; s++) {
                Ready& r = ready[sorted[s]];

                if (!r.draw) {
                    continue;
                }

                if (r.alpha < 1.0f) r.d.color = ScaleAlpha(r.d.color, r.alpha);
                if (r.rgb < 1.0f) r.d.color = ScaleRgb(r.d.color, r.rgb);

                // A see-through body over one drawn before it: its black silhouette first, so what's
                // behind it (the Blue Child behind the White Lady, the stars) stays hidden while it fades.
                const auto silhouetteFirst = [&](bool afterDome) {
                    for (int q = 0; q < s; q++) {
                        const Ready& o = ready[sorted[q]];

                        if (!o.draw || !Overlaps(r.d.dir, r.radius, o.d.dir, o.radius)) {
                            continue;
                        }

                        Draw silhouette = r.d;
                        silhouette.color = 0xFF000000u;
                        silhouette.additive = false;

                        if (afterDome) {
                            g_afterDome.push_back(silhouette);
                        } else {
                            Send(silhouette, false);
                        }

                        break;
                    }
                };

                if (r.own) {
                    // After the dome: by day it fades to its DayVisibility instead of being washed out
                    // (auto: the sun stays, the others to a pale 25%).
                    const bodytables::Body& b = *r.body;
                    const float dayVis = b.dayVisibility >= 0.0f ? (b.dayVisibility > 1.0f ? 1.0f : b.dayVisibility)
                                                                 : (b.role == bodytables::StockRole::Sun ? 1.0f : 0.25f);
                    r.d.rotation = b.rotation + static_cast<float>(std::fmod(static_cast<double>(b.rotationPerDay) * now.baseDays, 360.0));
                    r.d.color = ScaleAlpha(r.d.color, 1.0f - day * (1.0f - dayVis));
                    r.d.additive = b.blend == 1;

                    if ((r.d.color >> 24) < 0xFF && !r.d.additive) {
                        silhouetteFirst(true);
                    }

                    g_afterDome.push_back(r.d);
                    continue;
                }

                if ((r.d.color >> 24) < 0xFF) {
                    silhouetteFirst(false);
                }

                Send(r.d, false);
            }
        }

        float Smooth(float e0, float e1, float x) {
            float t = (x - e0) / (e1 - e0);
            t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
            return t * t * (3.0f - 2.0f * t);
        }

        void GlaresOnBodies(const ephemeris::Moment& now, sxc_sky_lighting& io) {
            if (!g_glaresFollow) {
                return;
            }

            float cam[3];
            Camera(cam);
            const bodytables::Body* sun = nullptr;
            const bodytables::Body* moon = nullptr;

            for (const bodytables::Body* b : *g_list) {
                if (!sun && b->role == bodytables::StockRole::Sun) sun = b;
                if (!moon && b->role == bodytables::StockRole::WhiteLady) moon = b;
            }

            const struct {
                const bodytables::Body* body;
                sxc_sky_glare* glare;
                bool moon;
            } kGlares[] = { { sun, &io.sun_glare, false }, { moon, &io.moon_glare, true } };

            for (const auto& g : kGlares) {
                // A set without the role has no such glare (it would shine where no body is).
                if (!g.body) {
                    g.glare->hide = WXL_TRUE;
                    continue;
                }

                const ephemeris::Place p = ephemeris::Where(*g.body, now, g_northOffset);

                if (!p.ok || p.visibility <= 0.0f || p.alpha <= 0.0f) {
                    g.glare->hide = WXL_TRUE;
                    continue;
                }

                if (p.stockExact) {
                    continue; // the client's own placement is ours
                }

                g.glare->position = wxl_vec3{ cam[0] + p.dir[0] * kDistance, cam[1] + p.dir[1] * kDistance, cam[2] + p.dir[2] * kDistance };

                if (g.moon) {
                    const float scale = 2.0f * kDistance * std::tan(p.sizeDegrees * 0.5f * kPi / 180.0f);
                    g.glare->scale_min = scale;
                    g.glare->scale_max = scale;
                }
            }
        }

        void LightFromBodies(const ephemeris::Moment& now, sxc_sky_lighting& io) {
            float dir[3] = { io.light_direction.x, io.light_direction.y, io.light_direction.z }; // the stock light
            float tint[3] = { 1.0f, 1.0f, 1.0f };
            std::snprintf(g_lightSource, sizeof(g_lightSource), "stock");

            if (!g_bodiesDriveLight) {
                return;
            }

            // Drivers in set order (lower Order first) take priority: blend from the last up to the
            // first, so the first one up wins and a setting one hands over to the next.
            bool any = false;

            for (size_t i = g_list->size(); i-- > 0;) {
                const bodytables::Body& b = *(*g_list)[i];

                if (!(b.flags & bodytables::kBodyDrivesLight)) {
                    continue;
                }

                const ephemeris::Place p = ephemeris::Where(b, now, g_northOffset);

                if (!p.ok) {
                    continue;
                }

                const float z = p.dir[2] > 1.0f ? 1.0f : (p.dir[2] < -1.0f ? -1.0f : p.dir[2]);
                const float el = std::asin(z) * 180.0f / kPi;
                const float w = Smooth(-kLightHorizonDegrees, kLightHorizonDegrees, el) * p.visibility;

                if (w <= 0.0f) {
                    continue;
                }

                any = true;
                float len = 0.0f;

                for (int c = 0; c < 3; c++) {
                    dir[c] += (-p.dir[c] - dir[c]) * w;
                    len += dir[c] * dir[c];
                }

                len = std::sqrt(len);

                if (len > 1e-4f) {
                    for (int c = 0; c < 3; c++) {
                        dir[c] /= len;
                    }
                }

                // LightColor x LightStrength: 0 = the zone's light colour as is, 1 = times LightColor.
                const float s = (b.lightStrength < 0.0f ? 0.0f : (b.lightStrength > 1.0f ? 1.0f : b.lightStrength)) * w;

                for (int c = 0; c < 3; c++) {
                    const float lc = ((b.lightColor >> (16 - 8 * c)) & 0xFF) / 255.0f;
                    tint[c] += (1.0f + (lc - 1.0f) * s - tint[c]) * w;
                }

                std::snprintf(g_lightSource, sizeof(g_lightSource), "%s%s", b.name.c_str(), w < 1.0f ? " (handing over)" : "");
            }

            if (!any) {
                return;
            }

            io.light_direction = wxl_vec3{ dir[0], dir[1], dir[2] };
            uint32_t t = 0;

            for (int c = 0; c < 3; c++) {
                t |= static_cast<uint32_t>(tint[c] * 255.0f + 0.5f) << (16 - 8 * c);
            }

            io.diffuse_tint = t;
        }

        void HideSkyboxes(sxc_sky_lighting& io) {
            const bool hideAll = g_hideAllSkyboxes || g_profileHideAll;
            const bool hideZone = g_hideZoneSkybox || g_profileHideZone;
            int strongest = -1;

            for (int i = 0; i < 3; i++) {
                if (io.skyboxes[i].used && (strongest < 0 || io.skyboxes[i].weight > io.skyboxes[strongest].weight)) {
                    strongest = i;
                }
            }

            for (int i = 0; i < 3; i++) {
                if (io.skyboxes[i].used && (hideAll || (hideZone && i == strongest))) {
                    io.skyboxes[i].hide = WXL_TRUE;
                }
            }

            if (hideAll) {
                io.hide_current = WXL_TRUE;
            }
        }

        // Rise / set answers, cached per body and base minute (Lua may ask every frame).
        struct NextCache {
            double minute = -1.0;
            int setId = -1;
            double rise = -1.0, set = -1.0;
            bool haveRise = false, haveSet = false;
        };

        std::map<std::string, NextCache> g_next;

        const bodytables::Body* Find(const char* name) {
            if (!g_list || !name) {
                return nullptr;
            }

            for (const bodytables::Body* b : *g_list) {
                if (_stricmp(b->name.c_str(), name) == 0) {
                    return b;
                }
            }

            return nullptr;
        }
    }

    void Update(const world::Snapshot& snap) {
        bodytables::Update();
        const bodytables::Profile& p = bodytables::ProfileHere(snap);
        // Off means the client's own sky, the place's skyboxes included (to compare against stock).
        g_profileHideZone = g_enabled && p.hideZoneSkybox;
        g_profileHideAll = g_enabled && p.hideAllSkyboxes;
        g_setId = p.bodySet;
        g_northOffset = p.northOffset;
        ephemeris::LoreClock lore;
        lore.year = p.loreYear;
        lore.wobbleYears = p.loreWobbleYears;
        lore.wobbleMode = p.loreWobbleMode;
        lore.yearsPerDay = p.loreYearsPerDay;
        lore.startDate = p.loreStartDate;
        ephemeris::SetLoreClock(lore);
        const auto& list = bodytables::SetBodies(p.bodySet);
        g_list = Api() && g_enabled && p.bodySet > 0 && !list.empty() ? &list : nullptr;

        if (!g_list) {
            g_count = 0;
        }

        g_hideSunGlare = g_hideMoonGlare = false;

        if (g_list) {
            for (const bodytables::Body* b : *g_list) {
                if (b->flags & bodytables::kBodyNoGlare) {
                    if (b->role == bodytables::StockRole::Sun) g_hideSunGlare = true;
                    if (b->role == bodytables::StockRole::WhiteLady) g_hideMoonGlare = true;
                }
            }
        }

        static const char* const kScope[] = { "global", "map", "area" };
        std::snprintf(g_where, sizeof(g_where), "%s row %u", p.scopeType >= 0 && p.scopeType <= 2 ? kScope[p.scopeType] : "no", p.scopeId);
    }

    void OnLighting(sxc_sky_lighting& io) {
        HideSkyboxes(io);

        if (g_enabled && g_list) {
            const ephemeris::Moment now = ephemeris::Now();
            GlaresOnBodies(now, io);
            LightFromBodies(now, io);

            if (g_hideSunGlare) io.sun_glare.hide = WXL_TRUE;
            if (g_hideMoonGlare) io.moon_glare.hide = WXL_TRUE;
        } else {
            std::snprintf(g_lightSource, sizeof(g_lightSource), "stock");
        }

        if (g_debugHideMoonGlare) {
            io.moon_glare.hide = WXL_TRUE;
        }

        const float dir[3] = { io.light_direction.x, io.light_direction.y, io.light_direction.z };
        climate::SetLightDirection(dir);
        g_lastLighting = io;
    }

    bool OnBodies(int pass) {
        if (!g_enabled || !g_list) {
            return false;
        }

        if (pass == 0) {
            DrawOurs();
            return true;
        }

        for (const Draw& d : g_afterDome) {
            Send(d, true);
        }

        g_afterDome.clear();
        return true;
    }

    const char* LightSource() { return g_lightSource; }
    const sxc_sky_lighting& LastLighting() { return g_lastLighting; }
    void SetEnabled(bool on) { g_enabled = on; }
    bool Enabled() { return g_enabled; }
    int Count() { return g_list ? g_count : 0; }

    const Info& At(int i) {
        static const Info kNone;
        return i >= 0 && i < g_count ? g_info[i] : kNone;
    }

    const char* SetName() { return g_list ? bodytables::SetName(g_setId) : "stock (the client's own)"; }
    const char* ProfileWhere() { return g_where; }

    int BodyCount() { return g_list ? static_cast<int>(g_list->size()) : 0; }

    const char* BodyName(int i) {
        return g_list && i >= 0 && i < static_cast<int>(g_list->size()) ? (*g_list)[i]->name.c_str() : nullptr;
    }

    bool Query(const char* name, Now& out) {
        const bodytables::Body* b = Find(name);

        if (!b) {
            return false;
        }

        const ephemeris::Place p = ephemeris::Where(*b, ephemeris::Now(), g_northOffset);

        if (!p.ok) {
            return false;
        }

        paths::ToCompass(p.dir, &out.azimuth, &out.elevation);
        out.visible = ephemeris::Visible(p);
        out.sizeDegrees = p.sizeDegrees;
        out.lit = 1.0f;
        return true;
    }

    double MinutesUntil(const char* name, bool rise) {
        const bodytables::Body* b = Find(name);

        if (!b) {
            return -1.0;
        }

        const double from = clock::BaseMinutes();
        NextCache& c = g_next[b->name];
        const double minute = std::floor(from);

        if (c.minute != minute || c.setId != g_setId) {
            c = NextCache{};
            c.minute = minute;
            c.setId = g_setId;
        }

        double& value = rise ? c.rise : c.set;
        bool& have = rise ? c.haveRise : c.haveSet;

        if (!have) {
            value = ephemeris::MinutesUntil(*b, minute, g_northOffset, rise, 3.0);
            have = true;
        }

        if (value < 0.0) {
            return -1.0;
        }

        const double left = value - (from - minute);
        return left > 0.0 ? left : 0.0;
    }

    bool CycleOf(const char* name, Cycle& out) {
        const bodytables::Body* b = Find(name);

        if (!b || !(b->periodDays > 0.0f)) {
            return false;
        }

        const ephemeris::Moment m = ephemeris::Now();
        out.position = paths::PathTime(*b, { m.dayFraction, m.baseDays, m.loreYears });
        out.period = b->periodDays;
        out.untilNext = (1.0 - out.position) * out.period;
        out.lore = b->clock == 1;
        const ephemeris::LoreClock& lc = ephemeris::CurrentLoreClock();

        if (!out.lore) {
            out.realDays = out.untilNext;
        } else {
            out.realDays = lc.yearsPerDay > 0.0f && !ephemeris::LoreOverridden() ? out.untilNext / lc.yearsPerDay : -1.0;
        }

        return true;
    }
}
