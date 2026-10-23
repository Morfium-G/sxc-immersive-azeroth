#include "ui/Panels.h"

#include "imgui.h"
#include "lights/Lights.h"

namespace ia::ui {
    void DrawLights(void*) {
        lights::Settings& s = lights::Config();

        if (!lights::Available()) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "The client doesn't draw our lights: %s", lights::Status());
            return;
        }

        // Short readout lines (the panel's text can't be copied), always the same lines so the settings
        // below don't move when the lights are switched off.
        sxc_lights_stats st;
        lights::Stats(st);
        const char* off = s.enabled ? "" : " (off)";
        ImGui::Text("Tables: %s", lights::TableStatus());
        ImGui::Text("Table lights: %u%s", st.table_lights, off);

        if (st.attach_missing) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Attachment missing: %u (placed at the model's origin; Logs\\sxc.log names them)", st.attach_missing);
        } else {
            ImGui::Text("Attachment missing: 0%s", off);
        }

        ImGui::Text("Models' own lights: %u (%u placed by the client)%s", st.model_lights, st.placed, off);
        ImGui::Text("Models suppressed: %u%s", st.suppressed, off);
        ImGui::Text("In range: %u, merged: %u, drawn: %u%s%s", st.in_range, st.merged, st.active, st.dropped ? " (over 512: farthest left out)" : "", off);
        ImGui::Text("Indoors: %u (located this frame: %u lights, %u models)%s", st.indoor, st.indoor_tests, st.model_tests, off);
        ImGui::Text("Grid: %u x %u, busiest cell: %u, cells over limit: %u%s", st.cells, st.cells, st.busiest_cell, st.full_cells, off);
        ImGui::Text("Shadows: %u lights (of %u maps), waiting: %u%s", st.shadowed, st.shadow_slots, st.shadow_waiting, off);
        ImGui::Text("Shadow maps drawn: %u (%u triangles)%s", st.shadow_draws, st.shadow_triangles, off);

        ImGui::TextDisabled("Now: terrain, models and WMOs per pixel, with shadows. Next: the light editor.");

        ImGui::SeparatorText("Settings (not saved)");
        ImGui::Checkbox("Our lights", &s.enabled);
        ImGui::SameLine();
        ImGui::Checkbox("Stock terrain lights off", &s.stockOff);
        ImGui::SetItemTooltip("The client lights each terrain chunk with its 3 nearest point lights, per vertex: only the models' own lights\n"
                              "(rare in 3.3.5: held torches and a few others). Ours replace them while on. Independent of a table row's\n"
                              "\"suppress\" flag, which leaves a model's own lights out of ours.");
        ImGui::Checkbox("Inside and outside apart", &s.separateIndoor);
        ImGui::SetItemTooltip("A light inside an indoor WMO group (a tavern, a crypt) lights only indoor surfaces; one outside only the terrain\n"
                              "and outdoor WMO groups; a model takes the lights of the side it stands on. Keeps lamps from shining through\n"
                              "outer walls. Lights with a shadow map skip this guess (their walls, floors and roofs are in the map).");
        ImGui::Checkbox("Shadows", &s.shadows);
        ImGui::SetItemTooltip("The nearest lights that stand still get a cube shadow map of the WMOs and the terrain around them:\n"
                              "walls between rooms, floors, roofs. Drawn once, redrawn when the light moves or a WMO near it finishes\n"
                              "loading. Models (doodads, units) cast none. Glass and other see-through WMO faces let the light through.");
        ImGui::SetNextItemWidth(200.0f);
        ImGui::SliderInt("Lights with shadows", &s.shadowCount, 0, 64);
        ImGui::SetItemTooltip("The nearest this many (up to the maps the device holds).");
        ImGui::SetNextItemWidth(200.0f);
        ImGui::SliderInt("Shadow maps per frame", &s.shadowUpdates, 1, 16);
        ImGui::SetItemTooltip("How many maps are drawn at most in one frame (new lights, moved ones, changed surroundings).");
        ImGui::SetNextItemWidth(200.0f);
        ImGui::Combo("Debug view", &s.debug, "off\0our lights only\0the cell's count\0");
        ImGui::SetItemTooltip("Our lights only: the terrain grey, lit by our lights alone.\n"
                              "The cell's count: red the lights reaching the pixel, green the lights its grid cell lists,\n"
                              "both as a share of what a cell holds (full green: the cell is full, some may be left out).");

        ImGui::SliderFloat("Default radius", &s.radius, 2.0f, 40.0f, "%.1f yd");
        ImGui::SetItemTooltip("A light is exactly 0 here. Only for lights without a Radius of their own (table rows with -1, the models' own lights).\n"
                              "The stock torches fall to 5%% at about 13 yd.");
        ImGui::SliderFloat("Brightness", &s.brightness, 0.0f, 4.0f, "%.2f");
        ImGui::SliderFloat("Flicker", &s.flicker, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Flicker pace", &s.flickerSpeed, 0.1f, 4.0f, "%.2f");
        ImGui::SliderFloat("Merge within", &s.mergeDistance, 0.0f, 3.0f, "%.2f yd");
        ImGui::SetItemTooltip("Closer lights become one (Blizzard places torches in groups of three).");

        ImGui::SeparatorText("Grid");
        ImGui::SliderFloat("Range", &s.range, 32.0f, 512.0f, "%.0f yd");
        ImGui::SetItemTooltip("No lights beyond this from the camera; they fade out over its last third.");
        ImGui::SliderFloat("Cell size", &s.cellSize, 4.0f, 32.0f, "%.0f yd");
        ImGui::SetItemTooltip("The grid reaches the range, 64 cells per side at most (larger cells past that).");
        ImGui::SliderInt("Lights per cell", &s.maxPerCell, 1, 32);
        ImGui::SetItemTooltip("Each pixel loops over its cell's lights; a fuller cell keeps the strongest at its middle.");
    }
}
