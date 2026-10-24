# Settings

Saved in the client's `WTF\SXC.ini`. Every setting below also shows in the overlay's **Settings view**
(Ctrl+F9), where it applies at once; editing the ini by hand works too (while the client is closed).
Missing keys are written with their default and a comment on first launch.

How the world *looks and behaves* comes from the tables ([cdbc-tables.md](cdbc-tables.md)); these are
switches and your own limits.

## `[ImmersiveAzeroth]`

| Key | Default | Meaning |
|---|---|---|
| Enabled | 1 | Immersive Azeroth's features. Off: the client's own sky, and nothing else changes either |
| SkyBodies | 1 | Draw the sun, the moons and custom bodies from the SkyBody tables. Off, or without the tables: the client's own sun and moons, and SkyProfile's skybox flags are ignored |
| SkyDayLength | 1 | Seasonal day length, × each place's AreaClimate `DayLength`: 0 = the stock day length everywhere, 1 = as the table says, up to 2 |
| LoreYear | 27 | The lore clock's year where no SkyProfile row sets one (0 = the Dark Portal opens, 27 = WotLK, 28 = the Embrace). Moves only sky bodies on the lore clock |

## `[ImmersiveAzerothGrass]`: the Grass panel

The Grass panel edits these; they're kept between sessions.

| Key | Default | Meaning |
|---|---|---|
| Motion | 1 | Grass sways in the wind and parts around characters |
| Sway | 0.35 | Yards the tips move at full wind |
| Flutter | 0.08 | Yards of extra per-blade shimmer, stronger in gusts |
| FlutterSpeed | 3 | Shimmer speed, radians a second |
| StiffBase | 0.15 | The bottom share of each blade that never moves (0..0.9) |
| Push | 0.5 | Yards grass leans away from characters |
| PushRadiusScale | 2 | Push radius as a multiple of a character's size |
| PushRadiusMin | 0.5 | The smallest push radius, yards |
| PushMountedScale | 2 | The push radius of a mounted character × this |
| Density | 1 | Extra plants near you, as GroundEffectDoodadDensity says |
| DensityMax | 8 | Your limit: plants drawn per placed plant, at most (1 = never denser; the tables are written for up to 8) |
| DensityRadius | 512 | Your limit: yards around you grass gets denser (up to 1024) |
| DensitySpread | 3 | Your limit: yards an extra plant may move from its plant (it stays on the plant's terrain triangle) |
| Edges | 1 | Soft edges (a test): where grass meets bare ground, plants near the border shrink and a few spill onto it |
| EdgeBand | 1.5 | Yards inside the border over which plants shrink (0 = none) |
| EdgeSize | 0.5 | Plant size right at the border |
| EdgeSpill | 2 | Yards plants spill onto the bare ground (0 = none); smaller the farther out |
| EdgeSpillShare | 0.6 | Spilled plants per plant near the border |
| EdgeSameTexture | 1 | Spill only onto bare ground painted with the same terrain texture |

## The client's settings the mod builds on

These belong to the client (SXC), not the mod; the mod's features need them on.

| Section, key | Default | Meaning |
|---|---|---|
| `[Terrain] OurShader` | 1 | The terrain drawn with the client's own shader (Vulkan). Covers, wetness and the lights on the terrain build on it |
| `[Grass] Instanced` | 1 | The grass drawn instanced on the GPU (Vulkan). Wind, parting, density and the lights on grass only work on this grass |
| `[Grass] Placement` | 2 | How a terrain cell picks its ground effect. 0: the layer the map file marks dominant (stock). 1: the dominant layer worked out from the paint (the largest painted share in the cell). 2: every layer's plants by how much of it is painted where they fall. Some maps mark a 0%-painted base layer dominant, which leaves cells bare in stock; 1 or 2 fix that. The Grass panel's "Ground effect from" |
| `[Grass] PaintedMin` | 0.3 | Placement 2: no plants of a layer where less of it than this is painted (a trace of grass on rock) |
| `[Grass] SteepestGround` | 0.4 | No plants on ground steeper than this, as the up part of the ground's normal (stock 0.4, about 66°; 0.71 = 45°; 0.87 = 30°) |
| `[Grass] SlopeSink` | 0 | Upright plants on a slope hang over the ground on their downhill side; this lowers each by that drop × the factor (stock 0) |
| `[Data] HotReload` | 1 | A table whose file changes on disk is reloaded within a second |

The grass distance itself is the client's own setting (`groundEffectDist`, options or console).

## Not saved yet

The other panels (Fog, Cover, Lights, Wind, Climate, Sky) change their settings for the session only:
the look of the fog and the covers, the lights' counts, ranges and shadows, and the testing tools
(weather, time of day, deposits). Their defaults are tuned to look right as they are.
