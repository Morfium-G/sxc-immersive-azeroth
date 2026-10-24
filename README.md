# Immersive Azeroth

Makes the world of WoW 3.3.5a feel alive: a sky with its own clock and bodies, a climate, wind,
volumetric ground fog, snow, sand and mud covers that you sink into, point lights with shadows, and
grass that moves in the wind, parts around you and grows denser near you. Almost everything is
driven by data: seventeen tables decide what happens where, and you can edit them live in the game.

The successor of `wxl-seyris-living-azeroth` (WarcraftXL), rebuilt for a client built from source with a
Vulkan renderer.

## What you need

- **An SXC build of the client** ([Morfium-G/xlclient](https://github.com/Morfium-G/xlclient)): the mod
  uses the client's `sxc.*` services (data tables, settings, the tools overlay, the world, the sky,
  fog, covers, lights, grass), published by the client's `sxc.bridge` mod.
- **Vulkan** for most of it. On the D3D9 renderer the sky, climate and wind work; fog, covers, the
  lights and the grass features need the client's Vulkan renderer.
- A [WarcraftXL](https://github.com/WarcraftXL/sdk) mod host, which the client includes.

## Installing

1. Copy `seyri.immersive-azeroth` (the build output, see below) into the client's `Mods\` folder, next
   to `sxc.bridge`.
2. Copy the contents of [sample-data/](sample-data/) into the client folder (or a patch MPQ): the tables
   (`DbFilesClient\`) and the textures they use. Without tables the mod runs with built-in defaults:
   stock sky, a mild climate, no wind, no covers, no extra lights.
3. Optional: the example addon [addons/ImmersiveAzerothClimate](addons/ImmersiveAzerothClimate) into
   `Interface\AddOns\`.

In game, **Ctrl+F9** opens the tools overlay: the mod's panels are under "Immersive Azeroth", the
tables under the Data view.

## Features

- **Sky clock**: pinned or shifted time per place (SkyTime), seasonal day length (AreaClimate
  `DayLength`), a debug override (scrub, pause, speed up). The sky, lighting, fog colours, M2 skyboxes,
  day/night ambience and day/night spells follow it; the on-screen clock and `GetGameTime()` stay on
  server time. `Map.dbc`'s TimeOfDayOverride still wins.
- **Sky bodies** (`Sky*` tables): the sun, the moons and custom bodies, body sets per place, orbits,
  keyframe and follow paths, visibility cycles, a lore clock, glares, a body driving the world light,
  M2 skyboxes hidden per place.
- **Climate** (AreaClimate): temperature through the day and the year, humidity, fogginess, blended
  across borders.
- **Wind** (WindProfile): a steady wind per place pushed by the weather, gusts travelling across the
  ground, calmed under roofs and in the lee of walls and cliffs.
- **Ground** (GroundMaterial, GroundMaterialSelector): what each painted terrain layer and liquid is
  made of. On a 2 yd grid around you: ground moisture (rest, shores, rain, heat), ground temperature
  (lava, hot ground), snow (fallen and painted, melting by temperature and sun). Each zone's rain soak
  and fallen snow are kept for the session.
- **Fog** (FogSettings, Vulkan): volumetric ground fog of five kinds (mist, water fog, steam, area fog,
  haze) forming by the rules of the place, weather and time, pooling in hollows, drifting with the
  wind, pushed aside by characters; far fog to the horizon.
- **Covers** (SurfaceCover, Vulkan): snow, sand, mud and ash as part of the terrain, with depth, drifts,
  their own textures and wetness; characters sink in and leave trenches that fill back in.
- **Lights** (DoodadLightProperties, DoodadLightAssignment, Vulkan): per-pixel point and spot lights on
  the terrain, covers, models, WMOs and grass, indoor and outdoor apart, cube shadow maps from
  buildings, the terrain and doodads; an in-game light editor.
- **Grass** (GroundEffectDoodadWind, GroundEffectDoodadDensity, Vulkan): drawn instanced on the GPU
  (much longer grass distances), swaying in the wind, parting around characters, denser near you, lit
  by the lights; soft edges and ground effects placed by how much of a layer is painted.
- **Lua** for addons: temperature, humidity, weather, ground moisture, wind, the sky's time, date,
  season and bodies, the lore clock.

## Documentation

- [docs/cdbc-tables.md](docs/cdbc-tables.md): every table and column, how rows inherit and which wins.
- [docs/settings.md](docs/settings.md): the settings in `WTF\SXC.ini`.
- [docs/lua-api.md](docs/lua-api.md): the Lua functions.
- [docs/wdbx/](docs/wdbx/): WDBX Editor definitions of the tables (regenerate with
  `python tools/gen_wdbx.py` after changing a schema in `src/Tables.cpp`).
- [sample-data/](sample-data/): a full set of tables to start from.

## Building

Needs CMake 3.21+, Visual Studio 2022 (x64), and these checkouts beside each other:

```
client/                    the SXC client (its SXC/Api headers and its Dear ImGui)
sdk/                       the WarcraftXL SDK
mods/immersive-azeroth/    this repository
```

```powershell
cmake -S . -B build
cmake --build build --config Release
```

Other locations: `-DWXL_SDK_DIR=<sdk>` and `-DSXC_CLIENT_DIR=<client>`. Output:
`build/dist/seyri.immersive-azeroth/{mod.json, bin/x64/immersive-azeroth.dll}`.

The mod compiles the client's Dear ImGui (`client/SXC/ThirdParty/imgui`) so its panels share the
overlay's context: rebuild it when the client's ImGui changes. Its sources are listed in
`CMakeLists.txt` (not globbed): add new files there.
