# Lua functions for addons

The mod adds read-only global functions to the client's addon Lua (through the WarcraftXL SDK's
script service). They are registered whenever the client builds its script context, so they exist
from the moment the interface loads (OnLoad handlers can use them) and after every `/reload`. Without
the mod they're simply missing, so check before calling: `if ImmersiveAzeroth_GetTemperature then ... end`.

Everything is about **where the player stands**. Values are `nil` outside the world or where nothing is
known yet.

| Function | Returns |
|---|---|
| `ImmersiveAzeroth_GetVersion()` | the API version (now **2**). Raised when functions are added; existing ones never change what they return |
| `ImmersiveAzeroth_GetTemperature()` | `ground, air` in °C. **air**: the place's climate (AreaClimate: time of day, season, weather). **ground**: near the ground right there, warmed by lava and hot ground nearby |
| `ImmersiveAzeroth_GetHumidity()` | the air's humidity, 0..1 (AreaClimate) |
| `ImmersiveAzeroth_GetGroundMoisture()` | `moisture, rest`, 0..1: how wet the ground is now, and where it rests when nothing wets or dries it (version 2) |
| `ImmersiveAzeroth_GetWeather()` | `"fine"`, `"rain"`, `"snow"` or `"sand"`, and the intensity 0..1 |
| `ImmersiveAzeroth_GetField(name)` | any world field by name, or `nil`: `"temperature"` (°C, the ground's), `"moisture"` (0..1), `"snow"` (yd of snow lying there: fallen + what's left of painted snow; painted depth as on flat open ground), and every field added later (version 2) |
| `ImmersiveAzeroth_GetFieldNames()` | the names of all world fields, as multiple returns (version 2) |
| `ImmersiveAzeroth_GetWind()` | `strength, bearing, gust, shelter` (version 2): strength 0..1 (gusts can pass 1), bearing in compass degrees the wind blows **toward** (0 north, 90 east), gust 0..1, shelter 0..1 (1 exposed; lower under a roof or in a lee) |
| `ImmersiveAzeroth_GetSkyTime()` | `hour, minute, dayFraction, day, overridden`: the time the sky and the world's light show. `dayFraction` 0..1 from midnight, `day` = days since 1 Jan 1970, `overridden` 1 while the Sky panel's debug override runs, else 0. Includes the place's pinned or shifted time (SkyTime). `GetGameTime()` stays the server's time |
| `ImmersiveAzeroth_GetSkyDate()` | `year, month, day, dayOfYear, yearFraction`: the date the sky and the seasons run on. `dayOfYear` 0-based, `yearFraction` 0 = 1 January .. 1 |
| `ImmersiveAzeroth_GetSeason()` | `"spring"`, `"summer"`, `"autumn"` or `"winter"`, then `phase` 0..1 (0 midsummer, 0.5 midwinter) and `warmth` −1..1 (midwinter .. midsummer), where the player is: a place's climate can shift its seasons (AreaClimate `SeasonOffset`) |
| `ImmersiveAzeroth_GetSkyBodies()` | the names of the sky bodies where the player is (SkyBody `Name`), as multiple returns; none while the mod's bodies are off |
| `ImmersiveAzeroth_GetSkyBody(name)` | `azimuth, elevation, visible, lit, size`: compass degrees (0 north, 90 east), degrees above the horizon, `visible` 1 when above the horizon and in its visibility window (else 0), `lit` 0..1 (always 1 until moon phases), `size` its angular diameter in degrees. `nil` for an unknown name |
| `ImmersiveAzeroth_GetSkyBodyNext(name, "rise" or "set")` | minutes until the body next becomes visible / stops being visible, or `nil` if not within 3 days. Real minutes unless a debug override speeds time up; a pinned place's daily bodies never rise or set |
| `ImmersiveAzeroth_GetSkyBodyCycle(name)` | `position, untilNext, period, unit, realDays`: where the body is in its own cycle (one trip along its path, SkyBody `PeriodDays`). `position` 0..1, 0 = the cycle's start (the lore Blue Child: the Embrace); `untilNext` and `period` in `unit`, `"years"` for a body on the lore clock, else `"days"`; `realDays` = realm days until the next start, `nil` while lore time is frozen. `nil` for an unknown name or a body without a cycle. Years to the next Embrace: `select(2, ImmersiveAzeroth_GetSkyBodyCycle("BlueChild"))` |
| `ImmersiveAzeroth_GetLoreTime()` | `loreTime, year, day, yearsPerDay`: the lore clock where the player is (lore calendar: 0 = the Dark Portal, 27 = WotLK, 28 = the Embrace). `loreTime` in years with the fraction (wobble included), `year` the whole lore year, `day` the place's day of the year (1-based; SkyTime `PinnedDay` if set), `yearsPerDay` 0 when lore time is frozen |

The names changed from WXL's `LivingAzeroth_*`; the values are the same. The version restarted at 1
with the port.

Example addon: [../addons/ImmersiveAzerothClimate](../addons/ImmersiveAzerothClimate) (temperature,
humidity, weather, soil, snow and wind under the minimap clock, details on hover). Copy the folder into
`Interface\AddOns\`.

## Adding a function (mod code)

1. Write it in `src/Lua.cpp` as `void Name(wxl::ScriptCall& call)`: read arguments with the call
   (`ArgCount`, `String`; see the SDK's `wxl/services/script.hpp`), push results (`PushNumber`,
   `PushString`, `PushNil`).
2. Register it in `Register()`: `script.RegisterFunction<&Name>("ImmersiveAzeroth_Name");`.
3. Raise `kApiVersion` and add a row to the table above.

A new world field (`env/Fields`) needs no new function: `ImmersiveAzeroth_GetField` finds it by name.

Rules, because any addon can call these (trusted or not): **read-only**, **numbers and strings only**
(no pointers, no handles), **validate every argument** as if the caller were hostile, keep pushed
results few, and never change what an existing function returns. Add a new one instead.
