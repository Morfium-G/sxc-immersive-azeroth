#include "env/Emitters.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <unordered_map>
#include "SXC/Api/sxc_lights.h"
#include "TableWatch.h"
#include "lights/Lights.h"

namespace ia::emitters {
    namespace {
        constexpr uint32_t kMaxPlaced = 2048;
        constexpr float kDefaultRadius = 8.0f;
        constexpr float kDefaultSpread = 180.0f;     // degrees: temperature and moisture reach every way...
        constexpr float kDefaultDraftSpread = 30.0f; // ...a draft blows along its direction

        struct Properties {
            int type = kTemperature;
            float strength = 0.0f, radius = kDefaultRadius, falloff = 1.0f, spread = kDefaultSpread;
        };

        TableWatch g_table("DoodadEmitterProperties");
        std::unordered_map<uint32_t, Properties> g_properties;
        std::string g_tableStatus = "not loaded yet";

        std::vector<Emitter> g_emitters;
        std::vector<sxc_lights_emitter> g_placed;
        Stats g_stats;

        const sxc_lights_api* Api() {
            const sxc_lights_api* api = lights::EditorApi();
            return api && api->struct_size >= offsetof(sxc_lights_api, emitters) + sizeof(api->emitters) ? api : nullptr;
        }

        void ReadTable() {
            g_properties.clear();
            const sxc::data::Table& t = g_table.Table();

            if (t) {
                const int type = t.Field("Type"), strength = t.Field("Strength"), radius = t.Field("Radius"), falloff = t.Field("Falloff"),
                          spread = t.Field("Spread");

                for (uint32_t i = 0; i < t.RowCount(); i++) {
                    const sxc::data::Row r = t.At(i);
                    Properties p;
                    p.type = r.Int(type);
                    p.strength = r.Float(strength);
                    p.radius = r.Float(radius) > 0.0f ? r.Float(radius) : kDefaultRadius;
                    p.falloff = r.Float(falloff) > 0.0f ? r.Float(falloff) : 1.0f;
                    p.spread = r.Float(spread) >= 0.0f ? r.Float(spread) : (p.type == kDraft ? kDefaultDraftSpread : kDefaultSpread);
                    g_properties[r.Id()] = p;
                }
            }

            char line[128];
            std::snprintf(line, sizeof(line), "%u emitter(s) in DoodadEmitterProperties%s", static_cast<unsigned>(g_properties.size()),
                          t && t.Loaded() ? "" : " (no file)");
            g_tableStatus = line;
        }
    }

    void Update() {
        if (g_table.Changed()) {
            ReadTable();
        }

        g_emitters.clear();
        g_stats = Stats{};
        const sxc_lights_api* api = Api();

        if (!api || g_properties.empty()) {
            return;
        }

        g_placed.resize(kMaxPlaced);
        const uint32_t n = api->emitters(g_placed.data(), kMaxPlaced);
        g_stats.placed = n;
        constexpr float kRad = 3.14159265f / 180.0f;

        for (uint32_t i = 0; i < n; i++) {
            const sxc_lights_emitter& placed = g_placed[i];
            const auto found = g_properties.find(placed.emitter_id);

            if (found == g_properties.end()) {
                g_stats.unknown++;
                continue;
            }

            const Properties& p = found->second;
            Emitter e;
            e.type = p.type;
            e.strength = p.strength;
            e.radius = p.radius * (placed.scale > 0.0f ? placed.scale : 1.0f);
            e.falloff = p.falloff;
            e.cosSpread = p.spread >= 179.5f ? -1.0f : std::cos(p.spread * kRad);
            e.row = placed.row_id;
            e.id = placed.emitter_id;

            for (int k = 0; k < 3; k++) {
                e.pos[k] = placed.pos[k];
                e.dir[k] = placed.direction[k];
            }

            switch (e.type) {
                case kTemperature: g_stats.temperature++; break;
                case kMoisture: g_stats.moisture++; break;
                case kDraft: g_stats.draft++; break;
                default: continue;
            }

            g_emitters.push_back(e);
        }
    }

    const std::vector<Emitter>& All() { return g_emitters; }

    float Reach(const Emitter& e, float distance) {
        if (e.radius <= 0.0f || distance >= e.radius) {
            return 0.0f;
        }

        const float t = distance / e.radius;
        const float w = (1.0f - t * t) * (1.0f - t * t);
        return e.falloff == 1.0f ? w : std::pow(w, e.falloff);
    }

    float Cone(const Emitter& e, const float d[3], float distance) {
        if (e.cosSpread <= -0.999f || distance <= 1.0f) {
            return 1.0f;
        }

        const float c = (d[0] * e.dir[0] + d[1] * e.dir[1] + d[2] * e.dir[2]) / distance;
        const float band = std::fmax(0.1f * (1.0f - e.cosSpread), 1e-3f);
        return std::fmin(std::fmax((c - e.cosSpread) / band, 0.0f), 1.0f);
    }

    void DraftAt(const float pos[3], float& x, float& y) {
        x = 0.0f;
        y = 0.0f;

        for (const Emitter& e : g_emitters) {
            if (e.type != kDraft) {
                continue;
            }

            const float d[3] = { pos[0] - e.pos[0], pos[1] - e.pos[1], pos[2] - e.pos[2] };
            const float dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            const float reach = Reach(e, dist) * Cone(e, d, dist);

            if (reach <= 0.0f) {
                continue;
            }

            // Every way: outward from the source. Else along its direction.
            float dirX = e.dir[0], dirY = e.dir[1];

            if (e.cosSpread <= -0.999f) {
                const float flat = std::sqrt(d[0] * d[0] + d[1] * d[1]);
                dirX = flat > 1e-3f ? d[0] / flat : 0.0f;
                dirY = flat > 1e-3f ? d[1] / flat : 0.0f;
            }

            x += dirX * e.strength * reach;
            y += dirY * e.strength * reach;
        }
    }

    Stats GetStats() { return g_stats; }

    const char* TableStatus() { return g_tableStatus.c_str(); }

    void Reset() {
        g_emitters.clear();
        g_stats = Stats{};
    }
}
