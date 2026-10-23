cmake_minimum_required(VERSION 3.21)

# No language here: wxl_add_mod enables C++ in the per-architecture child build.
project(immersive-azeroth VERSION 0.1.0 LANGUAGES NONE)

# Checkouts beside this one (the XLClient hub layout): the SDK, and our client for the sxc.* headers
# (SXC/Api) and its Dear ImGui, which the mod must share version for version.
set(WXL_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../sdk" CACHE PATH "Path to the WarcraftXL SDK")
set(SXC_CLIENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../client" CACHE PATH "Path to our client checkout")
include("${WXL_SDK_DIR}/cmake/wxl_add_mod.cmake")

set(IA_IMGUI_DIR "${SXC_CLIENT_DIR}/SXC/ThirdParty/imgui")

# dist/seyri.immersive-azeroth/{mod.json, bin/x64/immersive-azeroth.dll}. x64 only: it needs our client.
wxl_add_mod(immersive-azeroth
    SOURCES
        src/Mod.cpp src/Tables.cpp src/World.cpp src/Lua.cpp
        src/climate/Climate.cpp
        src/env/Shelter.cpp src/env/Wind.cpp src/env/Materials.cpp src/env/CoverTable.cpp src/env/Regional.cpp src/env/Fields.cpp
        src/fog/Fog.cpp src/fog/Field.cpp src/fog/FogSettings.cpp src/fog/Far.cpp src/fog/Wake.cpp
        src/cover/Cover.cpp
        src/lights/Lights.cpp
        src/sky/Clock.cpp src/sky/SkyTime.cpp src/sky/BodyTables.cpp src/sky/Paths.cpp src/sky/Ephemeris.cpp src/sky/Bodies.cpp
        src/ui/SkyPanel.cpp src/ui/ClimatePanel.cpp src/ui/WindPanel.cpp src/ui/GroundPanel.cpp src/ui/FogPanel.cpp src/ui/CoverPanel.cpp src/ui/LightsPanel.cpp src/ui/LightEditorPanel.cpp
    MANIFEST mod.json
    TARGETS win-x64)

if (TARGET immersive-azeroth)
    target_compile_features(immersive-azeroth PRIVATE cxx_std_17)
    target_include_directories(immersive-azeroth PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${SXC_CLIENT_DIR}" "${IA_IMGUI_DIR}")
    target_sources(immersive-azeroth PRIVATE
        "${IA_IMGUI_DIR}/imgui.cpp"
        "${IA_IMGUI_DIR}/imgui_draw.cpp"
        "${IA_IMGUI_DIR}/imgui_tables.cpp"
        "${IA_IMGUI_DIR}/imgui_widgets.cpp")
endif ()
