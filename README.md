# Immersive Azeroth

Makes the world of WoW 3.3.5a feel alive: simulation and data-driven systems for the sky, climate,
fog, ground covers, lights and wind. Successor of WXL's `wxl-seyris-living-azeroth`, rebuilt for
our recompiled client (source + Vulkan). The port plan:
`orchestration/docs/research/immersive-azeroth-port-2026-10-19.md` in the XLClient hub.

A [WarcraftXL](https://github.com/WarcraftXL/sdk) mod. It needs an SXC build of the client: the
client's `sxc.*` services (data tables, settings, the tools overlay), published by the client's
`sxc.bridge` mod.

## State

- Its 17 custom tables, registered with the client's data layer (editable live in the overlay's
  Data view; the features follow every edit).
- **Sky clock**: a debug override (scrub, pause, speed up), pinned or shifted time per place
  (`SkyTime.cdbc`), seasonal day length (`AreaClimate.cdbc` DayLength). The sky, the lighting, fog
  colours, M2 skyboxes, day/night ambience and day/night spells follow it; the on-screen clock and
  `GetGameTime()` stay on server time. `Map.dbc`'s TimeOfDayOverride still wins.
- **Sky bodies**: the sun, the moons and custom bodies from the `Sky*` tables (sets per place,
  orbits, keyframe and follow paths, visibility cycles, the lore clock), the glares on them, a body
  driving the world light, hiding M2 skyboxes per place.
- **Climate**: temperature and humidity per place, blended across borders.
- Panels in the overlay (Ctrl+F9): Overview, Sky, Climate.
- Settings (`[ImmersiveAzeroth]`): `Enabled`, `SkyBodies`, `SkyDayLength`, `LoreYear`.

Next, in the plan's order: wind and the world fields, then fog (froxels), covers, lights, grass.

## Lua

Read-only, for addons (example: `addons/ImmersiveAzerothClimate`):

| Function | Returns |
|---|---|
| `ImmersiveAzeroth_GetVersion()` | API version (1) |
| `ImmersiveAzeroth_GetTemperature()` | ground degC, air degC (the ground's own comes with the fields; the air's for now) |
| `ImmersiveAzeroth_GetHumidity()` | 0..1 |
| `ImmersiveAzeroth_GetWeather()` | "fine" / "rain" / "snow" / "sand", intensity 0..1 |
| `ImmersiveAzeroth_GetSkyTime()` | hour, minute, day fraction, day number, overridden (1/0) |
| `ImmersiveAzeroth_GetSkyDate()` | year, month, day, day of year, year fraction |
| `ImmersiveAzeroth_GetSeason()` | name, phase (0 midsummer), warmth -1..1 |
| `ImmersiveAzeroth_GetSkyBodies()` | the body names where the player is |
| `ImmersiveAzeroth_GetSkyBody(name)` | azimuth, elevation, visible, lit, size (degrees) |
| `ImmersiveAzeroth_GetSkyBodyNext(name, "rise"/"set")` | minutes, or nil beyond 3 days |
| `ImmersiveAzeroth_GetSkyBodyCycle(name)` | position, untilNext, period, "years"/"days", realDays |
| `ImmersiveAzeroth_GetLoreTime()` | lore time, year, day of year, years per day |

## Building

Checkouts beside each other, as in the hub (`client/`, `sdk/`, `mods/immersive-azeroth/`):

```powershell
cmake -S . -B build
cmake --build build --config Release
```

Output: `build/dist/seyri.immersive-azeroth/{mod.json, bin/x64/immersive-azeroth.dll}`. The hub's
`orchestration/tools/deploy-client.ps1` copies it into the client's `Mods/` with the client, and the
addons into `Interface/AddOns/`.

The mod compiles the client's Dear ImGui (`client/SXC/ThirdParty/imgui`) so its panels share the
overlay's context; rebuild it when the client's ImGui changes.

## Tables

`DBFilesClient\*.cdbc`, documented in WXL's module (`docs/cdbc-tables.md`), schemas in
`src/Tables.cpp`.
