#include "lights/Lights.h"

#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>
#include "SXC/Api/sxc_lights.h"
#include "TableWatch.h"
#include "wxl/sdk.hpp"

namespace ia::lights {
    namespace {
        Settings g_settings;
        bool g_running = false;

        TableWatch g_properties("DoodadLightProperties");
        TableWatch g_assignments("DoodadLightAssignment");
        std::string g_tableStatus = "not loaded yet";
        bool g_tableSent = false;

        // DoodadLightProperties and DoodadLightAssignment to the client (WXL's features/DoodadLightTable).
        // A Properties float of -1 takes the default (the settings'); Intensity -1 the stock torches' 1.4.
        void SendTable(const sxc_lights_api* api) {
            std::vector<sxc_lights_kind> kinds;
            std::vector<sxc_lights_assignment> rows;
            std::vector<std::string> paths;
            const sxc::data::Table& p = g_properties.Table();
            const sxc::data::Table& a = g_assignments.Table();

            // A table without a file yet still counts: rows added in the editor show before the first save.
            if (p) {
                const int id = p.Field("ID"), type = p.Field("Type"), color = p.Field("Color"), intensity = p.Field("Intensity"), radius = p.Field("Radius"),
                          falloff = p.Field("Falloff"), inner = p.Field("InnerAngle"), outer = p.Field("OuterAngle"), mode = p.Field("FlickerMode"),
                          speed = p.Field("FlickerSpeed"), amount = p.Field("FlickerAmount"), flags = p.Field("Flags");

                for (uint32_t i = 0; i < p.RowCount(); i++) {
                    const sxc::data::Row r = p.At(i);
                    sxc_lights_kind k = {};
                    k.id = r.Uint(id);
                    k.type = r.Uint(type);
                    const uint32_t c = r.Uint(color);
                    const float in = r.Float(intensity) >= 0.0f ? r.Float(intensity) : 1.4f;
                    k.color[0] = ((c >> 16) & 0xFF) / 255.0f * in;
                    k.color[1] = ((c >> 8) & 0xFF) / 255.0f * in;
                    k.color[2] = (c & 0xFF) / 255.0f * in;
                    k.radius = r.Float(radius);
                    k.falloff = r.Float(falloff);
                    k.inner_angle = r.Float(inner) >= 0.0f ? r.Float(inner) : 20.0f;
                    k.outer_angle = r.Float(outer) >= 0.0f ? r.Float(outer) : 35.0f;
                    k.flicker_mode = r.Int(mode);
                    k.flicker_speed = r.Float(speed);
                    k.flicker_amount = r.Float(amount);
                    k.flags = r.Uint(flags);
                    kinds.push_back(k);
                }
            }

            if (a) {
                const int path = a.Field("ModelPath"), type = a.Field("AttachType"), index = a.Field("AttachIndex"), offset = a.Field("Offset"),
                          direction = a.Field("Direction"), light = a.Field("LightID"), flags = a.Field("Flags");
                paths.reserve(a.RowCount());

                for (uint32_t i = 0; i < a.RowCount(); i++) {
                    const sxc::data::Row r = a.At(i);
                    paths.emplace_back(r.String(path));
                    sxc_lights_assignment row = {};
                    row.attach_type = r.Uint(type);
                    row.attach_index = r.Int(index);

                    for (uint32_t e = 0; e < 3; e++) {
                        row.offset[e] = r.Float(offset, e);
                        row.direction[e] = r.Float(direction, e);
                    }

                    row.light_id = r.Uint(light);
                    row.flags = r.Uint(flags);
                    row.id = r.Id();
                    rows.push_back(row);
                }

                for (size_t i = 0; i < rows.size(); i++) {
                    rows[i].model_path = paths[i].c_str();
                }
            }

            api->set_table(kinds.empty() ? nullptr : kinds.data(), static_cast<uint32_t>(kinds.size()), rows.empty() ? nullptr : rows.data(),
                           static_cast<uint32_t>(rows.size()));
            char line[160];
            std::snprintf(line, sizeof(line), "%u light(s) in DoodadLightProperties%s, %u assignment(s) in DoodadLightAssignment%s", static_cast<unsigned>(kinds.size()),
                          p.Loaded() ? "" : " (no file)", static_cast<unsigned>(rows.size()), a.Loaded() ? "" : " (no file)");
            g_tableStatus = line;
        }

        const sxc_lights_api* Api() {
            static const sxc_lights_api* s_api =
                wxl::Host() ? static_cast<const sxc_lights_api*>(wxl::Host()->get_service(SXC_LIGHTS_SERVICE, SXC_LIGHTS_SERVICE_VERSION)) : nullptr;
            return s_api;
        }

        void Send(const sxc_lights_api* api, bool enabled) {
            sxc_lights_settings s = {};
            s.struct_size = sizeof(s);
            s.enabled = enabled ? WXL_TRUE : WXL_FALSE;
            s.stock_off = g_settings.stockOff ? WXL_TRUE : WXL_FALSE;
            s.debug = g_settings.debug;
            s.radius = g_settings.radius;
            s.brightness = g_settings.brightness;
            s.range = g_settings.range;
            s.cell_size = g_settings.cellSize;
            s.max_per_cell = g_settings.maxPerCell;
            s.merge_distance = g_settings.mergeDistance;
            s.flicker = g_settings.flicker;
            s.flicker_speed = g_settings.flickerSpeed;
            s.separate_indoor = g_settings.separateIndoor ? WXL_TRUE : WXL_FALSE;
            s.shadows = g_settings.shadows ? WXL_TRUE : WXL_FALSE;
            s.shadow_count = g_settings.shadowCount;
            s.shadow_updates = g_settings.shadowUpdates;
            s.shadow_doodads = g_settings.shadowDoodads ? WXL_TRUE : WXL_FALSE;
            api->set_settings(&s);
        }
    }

    Settings& Config() { return g_settings; }

    void Update(const world::Snapshot&) {
        if (const sxc_lights_api* api = Api()) {
            // Both asked every frame (a change to either sends both).
            const bool changed = g_properties.Changed() | g_assignments.Changed();

            if (changed || !g_tableSent) {
                SendTable(api);
                g_tableSent = true;
            }

            Send(api, g_settings.enabled);
            g_running = true;
        }
    }

    void Stop() {
        const sxc_lights_api* api = Api();

        if (api && g_running) {
            Send(api, false);
            g_running = false;
        }
    }

    bool Available() {
        const sxc_lights_api* api = Api();
        return api && api->available();
    }

    const char* TableStatus() { return g_tableStatus.c_str(); }

    bool Stats(sxc_lights_stats& out) {
        const sxc_lights_api* api = Api();
        out = sxc_lights_stats{};
        out.struct_size = sizeof(out);

        if (!api) {
            return false;
        }

        api->stats(&out);
        return true;
    }

    const char* Status() {
        const sxc_lights_api* api = Api();
        return api ? api->status() : "the client has no sxc.lights (update the client)";
    }

    const sxc_lights_api* EditorApi() {
        const sxc_lights_api* api = Api();
        return api && api->struct_size >= offsetof(sxc_lights_api, missing_rows) + sizeof(api->missing_rows) ? api : nullptr;
    }
}
