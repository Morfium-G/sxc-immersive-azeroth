# Custom tables (cdbc)

Seventeen tables, plain WDBC files in the client's `DBFilesClient\` folder (loose files or in an MPQ).
The mod registers their schemas with the client's data layer (`src/Tables.cpp`), so they show up in the
overlay's **Data view** (Ctrl+F9): every field with its meaning as a tooltip, dropdowns for enums and
flags, pickers for links to other tables. Edits apply live: the features follow every change, before
you save. Saving writes the whole table and keeps the old file as `<file>.<yyyymmdd-hhmmss>.bak`. With
`[Data] HotReload` on, a file changed on disk (by another editor, a copy) is reloaded within a second.

For editing outside the client: WDBX Editor definitions in [wdbx/](wdbx/) (`ImmersiveAzeroth.xml` holds
all tables; one file per table to paste into your own definitions). They are generated from the
schemas by `python tools/gen_wdbx.py`. Files from WXL's `wxl-seyris-living-azeroth` have the same
layout and load as they are.

Examples for every table: [../sample-data/](../sample-data/).

**Rules for every table here:**
- Columns are only ever **appended**. A file from before a column existed still loads: the missing
  column reads its default (usually "inherit"), and the next save writes the full set of columns.
- **−1 inherits** (takes the value from the next row up, as each table describes). **Signed columns**
  (temperatures, offsets) inherit at **−1000** and below instead, since −1 °C is a real value.
- **Places** (ScopeType / ScopeID): 0 = Global (ScopeID ignored), 1 = Map (Map.dbc ID), 2 = Area
  (AreaTable ID: a zone or a sub-area). Unless a table says otherwise, the most specific row wins:
  the area itself, its parent zones, the map, global. Fields marked "field by field" are resolved
  per field, so a sub-area row can change one value and inherit the rest.
- **Comment** columns are free text for the editor (which place the row is) and never read.

## WindProfile.cdbc

Wind per place, field by field. **A negative value inherits** from the place above. No Global row =
no wind. The Wind panel shows the wind where you stand and draws it as arrows around you.

| Field | Meaning |
|---|---|
| ScopeType, ScopeID | the place |
| GroundMin / GroundMax | strength range (0..1) at ground level |
| AloftMin / AloftMax | strength range at and above AloftHeight |
| AloftHeight | yards above ground where wind is fully unobstructed |
| WeatherInfluence | 0..1, how far bad weather pushes strength toward the max |
| GustStrength | 0..1, gust amplitude on top of the steady wind |
| GustFrequency | relative, 1 = default |
| TerrainWeight | 0..1, reserved for terrain shaping |
| LockedDirection | compass degrees the wind blows **toward** (0 north, 90 east) |
| LockStrength | 0..1, pull toward LockedDirection (0 = no lock) |
| Flags | reserved, 0 |
| Comment | for the editor |

Roofs and overhangs shelter the ground below; walls and cliffs cast a wind shadow downwind. The grass
sways in this wind, and Lua reads it (`ImmersiveAzeroth_GetWind`).

## Grass: two tables

The client draws the ground-effect grass instanced on the GPU (Vulkan, `[Grass] Instanced`). These
tables shape how it moves and how dense it is; the **Grass panel** sets your own limits and shows what
the client does.

### GroundEffectDoodadWind.cdbc

Per plant model. **A row replaces the automatic values for that model**: the client finds each
model's bend from its geometry (the mean texture V of its lowest and highest 12%); models under
0.15 yd don't sway.

| Field | Meaning |
|---|---|
| ID | GroundEffectDoodad.dbc ID |
| Flags | 0x1 no wind, 0x2 no push, 0x10 flip root/tip (0x4 droop when wet, 0x8 water current: reserved) |
| Stiffness | 0 flexible .. 1 rigid (scales wind and push) |
| RootV / TipV | manual bend mapping in texture V; −1 = automatic from the model geometry |

### GroundEffectDoodadDensity.cdbc

How dense grass is drawn near the player: each placed plant gets `Multiplier − 1` extra copies, made
on the GPU, each turned and moved to a random spot of the plant's own terrain triangle (so it sits on
the ground exactly). Full density within half of `Radius`, fading out plant by plant toward `Radius`.
A fractional multiplier gives that share of the plants one more copy (6.5 = every plant 5 copies, half
of them a 6th). Your limits in the Grass panel cap the result (density up to 8, as these tables are
written for; radius up to 1024 yd).

| Field | Meaning |
|---|---|
| ID | row id |
| ScopeType, ScopeID | the place |
| DoodadID | GroundEffectDoodad.dbc ID; 0 = every doodad |
| GroundEffectID | GroundEffectTexture.dbc ID (what the map designer painted); 0 = any |
| Multiplier | plants drawn per placed plant (1 = no copies) |
| Radius | yards around the player |
| Spread | most yards a copy moves from its plant (copies never leave its terrain triangle) |

A float of **−1 takes that field from the next, less specific row**. No row at all = no copies.

**The most specific row wins**: place first, then what. Places: the chunk's own area (sub-zone), its
parent zones, the map, global. Within each place: DoodadID + GroundEffectID → DoodadID →
GroundEffectID → every doodad (0). Values are never added or multiplied across rows. So an area-wide
row (DoodadID 0) beats a global rule for one doodad; to keep one doodad different inside such an area,
give it its own Area row.

Area and ground effect come from the terrain chunk the plant grows on, not from where the player
stands, so a forest can be thinner even while you're outside it. A change to the table places the
grass again.

## Ground materials: three tables

What the ground is made of is decided once and shared by every feature that cares (covers, moisture,
ground temperature). Three roles:

1. **GroundMaterialSelector.cdbc**: *where* a material lies.
2. **GroundMaterial.cdbc**: *what* a material is (properties only).
3. **Feature tables keyed by material**: SurfaceCover.cdbc today. A new feature adds a new table and
   leaves the others alone.

Looked up per terrain cell from what it's painted with: each texture layer's **texture**, that layer's
**ground effect**, and the ground effect's **TerrainType**. Layers are weighted by their painted
strength, so two materials meeting mix smoothly. **The Ground panel shows all of this for the spot you
stand on** ("The ground's layers here": texture, effect, TerrainType, weight, area, material and its
values), so rows can be written on the spot.

### GroundMaterialSelector.cdbc

| Field | Meaning |
|---|---|
| ID | row id |
| ScopeType, ScopeID | the place |
| TexturePath | the painted texture's path as the Ground panel shows it; **empty = any**. Case and `/` vs `\` don't matter |
| GroundEffectID | GroundEffectTexture.dbc ID (the effect painted with the texture); **0 = any** |
| TerrainType | TerrainType.dbc ID; **−1 = any** (0 is Dirt). Stock: 0 Dirt, 1 Metallic, 2 Stone, 3 Snow, 4 Wood, 5 Grass, 6 Leaves, 7 Sand, 8 Soggy, 9 Dusty Grass, 10 None, 11 Water |
| LiquidType | **0 for terrain rows.** A LiquidType.dbc ID (as baked into ADTs and WMOs) makes this a **liquid row**: that liquid is this material. Only the place and MaterialID count on liquid rows |
| MaterialID | the GroundMaterial; **0 = no material here** (switches a broader row off, e.g. no snow in one zone) |
| Flags | reserved, 0 |

**The most specific row wins**: place first, then what. Within each place: TexturePath →
GroundEffectID → TerrainType → everything (empty texture, GroundEffectID 0, TerrainType −1). So "snow
everywhere" is one Global row with TerrainType 3; one texture that should be a different material gets
a TexturePath row; a zone that's different gets Area rows.

### GroundMaterial.cdbc

| Field | Meaning |
|---|---|
| ID | material id (what selectors and feature tables refer to) |
| Name | for authoring and the panels |
| Parent | material whose values a −1 field takes (0 = none). Feature tables follow the same chain |
| RestMoisture | 0 = bone dry .. 1 = soaked: **what the texture already shows** (wet-painted sand ≈ 0.8). Moisture rests here |
| Stiffness | resistance to deformation: **0 = gives way completely .. 1 = rigid**. For a cover: feet press away (1 − Stiffness) of its depth, so mud at 0.4 keeps a floor in every print |
| Flags | reserved, 0 |
| Absorbency | 0..1, how much outside moisture reaches it: **rain** on open ground and **water nearby**. Rock and ice ≈ 0.1 (rain runs off), sand and soil ≈ 0.9 |
| Temperature | °C of the material itself; **−1000 = none** (signed). A heat source: **liquid materials** (magma) and **terrain materials** (a painted lava-stream texture, where it's painted at least 50%) warm (or cool) the ground toward it, by 5% of the difference at the source, fading out by 12 yd and 4 yd above it. Liquid and ground heat don't add up; the stronger one counts. Hot ground dries out by itself |

A float of **−1 takes the value from the Parent** (and so on up). Defaults where no material in the
chain sets one: RestMoisture 0.3, Stiffness 0, Absorbency 0.5.

**Ground moisture** (runtime, not stored; the Climate panel shows it where you stand): every 2 yd
around you (±128 yd) the ground's moisture settles toward its **equilibrium**:
- the materials' RestMoisture (mixed by painted strength),
- wetter near a liquid: Absorbency × the liquid's RestMoisture × how close (full within 2 yd, none
  beyond 6) × how low (full up to 0.5 yd above the surface, none from 2 yd),
- drier in hot, dry air (from 20 °C, up to 60% below rest at 40 °C with no humidity).

It moves there over minutes: faster when warm and windy, slower in humid air, very slowly below
freezing. **Rain** wets ground that's open to the sky (no roof above), at Absorbency × the rain's
intensity. Submerged ground takes the liquid's RestMoisture.

**What a liquid does:** its liquid row's material (RestMoisture: how wet it makes the ground;
Temperature: heat), else built in by the liquid's category (LiquidType.dbc's Type column): water,
ocean and slime RestMoisture 1 and no temperature; magma RestMoisture 0 and 1000 °C, so the ground
around lava is hot and dry. The Climate panel names the nearest liquid, its category and what it does.

### SurfaceCover.cdbc

Which materials carry a cover (snow, sand, mud, ...), how deep, and how it looks and behaves. The cover
is part of the terrain (our terrain shader, Vulkan, `[Terrain] OurShader`): the ground rises by its
depth, units sink in and carve trenches with a rim, which fill back in over time. The **Cover panel**
shows the cover where you stand and lets you scale it for testing.

| Field | Meaning |
|---|---|
| ID | row id |
| MaterialID | the GroundMaterial that carries this cover (one row per material) |
| CoverMaterial | the material of what lies on top (snow on grass is still Snow); **0 = the ground material itself**. Its Stiffness decides how far feet press the cover down |
| Depth | yards of cover; **0 = no cover** (switches a material off) |
| MaxSlope | degrees: steeper ground holds no cover |
| SlopeFade | degrees below MaxSlope over which the cover thins out |
| DriftNoise | depth variation (drifts and lumps), share of the depth (0.35 = ±35%) |
| EdgeBreakup | 0 = smooth, rounded edges where covered ground meets bare ground .. 1 = ragged, patchy ones |
| Rim | height of the rim pushed up beside a trench, share of the depth |
| RelaxSeconds | how long a trench takes to fill back in |
| TintColor | colour as 0xRRGGBB, used with TintStrength |
| TintStrength | 0 = the plain cover colour .. 1 = TintColor. TintColor comes from the same row |
| CoverTexture | a BLP drawn on the cover, tiled like the terrain's layers (one repeat per terrain cell), e.g. the same texture as the ground it covers. **Empty = from the parent material's row, `-` = none.** Only so many different ones are drawn at once in view (the ones most present around you); the Cover panel lists them. TintColor/TintStrength still apply on top |
| Opacity | **reserved, not used yet** (see-through covers: slush, goo) |
| Flatten | **reserved, not used yet** (liquid-like covers that fill hollows) |
| ZOffset | yards the cover's base sits above (+) or **below (−)** the terrain. Below the terrain only what rises above it shows, and prints in it go down to its base (mud). Signed: **−1000 inherits** (exactly −1 does too, as older files wrote it) |
| Wetness | how wet the cover **looks** at rest: 0 = as its texture .. 1 = soaked: darker, and the specular gets stronger and tighter. On top of it, the ground moisture above its rest (rain, water nearby) makes it look wetter still. The shine comes from the CoverTexture's specular mask, so a cover without a CoverTexture doesn't shine |
| Flags | 0x1 **prevent underwater**: the cover thins out below a terrain liquid's surface (rivers, lakes, sea; not WMO water). 0x2 **prevent on land**: cover only there (river and sea floors). 0x4 **use MCCV**: tinted by the terrain's vertex colours, like the terrain. 0x8 **ignore specular map**: no shine from the CoverTexture. Flags come from the row that decides Depth, except 0x8, which belongs to the CoverTexture named in the same row |
| MeltKeptShare | **snow covers only**: the share of the painted depth mild weather melts it down to (at 15 °C; less above 0 °C, nothing at or below). 0.6 if no row sets it |
| MeltGoneTemperature | **snow covers only**: °C at which painted snow melts away completely (from MeltKeptShare at 15 °C down to nothing here). 25 if no row sets it; set it low (e.g. 16) for "no snow in summer" in a zone or on a texture. Signed: **−1000 inherits** |
| MoistureMin | the cover only forms where the ground's **moisture** (0 bone dry .. 1 soaked; the Climate panel shows it) is at least this, growing to its full Depth over the next 0.1: mud on riverbanks and in swamps, more of it after rain, drying away again. **2 (no row sets it) = off**: the cover doesn't depend on moisture. Off by default on purpose: nothing becomes mud unless a row asks for it |

**Specular:** a CoverTexture's alpha is its specular mask, as on the terrain's layers. If a `_s.blp`
next to it exists (later expansions keep the mask there), that one is used instead. The shine uses the
terrain's own sun and specular colour, so a custom sky's sun applies too.

A float of **−1 takes that field from the parent material's row** (ZOffset: exactly −1), and so on up
the chain; a material without a row takes everything from its parents'. TintColor comes with
TintStrength's row, Flags with Depth's row. Fields no row sets use the defaults: MaxSlope 45,
SlopeFade 15, DriftNoise 0.35, EdgeBreakup 0.5, Rim 0.3, RelaxSeconds 30, no tint, no cover texture,
ZOffset 0, Wetness 0, no flags, MoistureMin off. No material, no row along its chain, or Depth 0 = no
cover.

**Muddy footprints:** a thin cover below the terrain, e.g. Depth 0.015, ZOffset −0.02, Rim 4,
DriftNoise 0: only the rims of prints rise above the ground.

**Trench size:** like the client's own footprints. CreatureModelData's FootprintTextureLength/Width (in
inches) × the unit's scale, the mount's while mounted; the trench radius is 1.2 × the print's longer
side (a human's 0.33 yd print → 0.4 yd). Models with FootprintTextureID −1 (bats, birds and other
fliers that "walk" in the air) leave no trench.

**Snow: fallen and painted** (runtime; the Cover panel's snow lines and the Climate panel's zone list
show it). A cover is **snow** when its CoverMaterial is the fallback snow material (what
GroundMaterialSelector gives for **TerrainType 3** at the place, or globally) or a child of it.
- **Fallen snow** builds up while it snows on ground below 0 °C (0.2 yd an hour at full intensity, up
  to 0.6 yd) and melts above 0 °C (~1.5 cm per °C per day; its water soaks the ground). Snow falling on
  ground above 0 °C melts on arrival and soaks it, more slowly than rain. While it isn't snowing,
  fallen snow also **sublimates** (turns to vapour, no melt water), even below 0 °C: half of it in ~8 h
  in calm air at night, down to ~1 h in strong wind, dry air and sunshine. So always-cold zones don't
  keep fallen snow forever; their painted snow stays. It's kept per zone for the session (a zone you
  enter while it snows catches up on half an hour of snowfall) and follows that zone near you, less
  where a spot is warmer than its zone (hot ground and liquids, sunny slopes), none under roofs and
  liquids. Where a **snow cover** is painted it adds to its depth and keeps its look. Elsewhere it takes
  the look of the layer's own row **if that row's CoverMaterial is snow, even with Depth 0** ("no snow
  until it snows, then it looks like this", per area or texture), else of the fallback snow's row, else
  built-in values (plain white, slope limits 45°/15°). It never lies on or right beside **other**
  covers (sand, mud).
- **Painted snow** keeps its Depth as a baseline. Warm weather melts it down, slowly (degree-days),
  toward a cap: all of it at 0 °C or below, MeltKeptShare at 15 °C, nothing at MeltGoneTemperature. It
  opens in small holes and patches first. Heat sources (hot ground, hot liquids) melt it down to
  nothing on top and less further away, leaving pits. Colder again (night), it grows back toward its
  cap (~4.5 cm an hour, faster while it snows). Melt water wets the ground.
- The **effective temperature** of a spot is the ground's (air, hot liquids and ground nearby, toward a
  hot material's own on lava textures) plus up to 4 °C of sun by day on ground facing it.

## Doodad lights: two tables

Lights for placed models without editing the models: add a light to a model that has none, or replace
a model's own light entries with better ones. Never named `Light*` (those are the sky's). The lights
are per pixel on the terrain, the covers, models, WMOs and the grass (Vulkan), added to the client's
own lighting; inside and outside are told apart (a WMO's indoor groups only take indoor lights), and
the nearest lights (64 by default) cast **shadows** from WMOs, the terrain and placed doodads (cube
shadow maps, redrawn only when the light or what's around it changes). The **Lights panel** sets the
counts, ranges and shadows; the **Light editor** panel edits both tables live: the models near you
with their attachment points, emitters, bones and own lights, markers in the world, "add a light to
this model", save and reload.

### DoodadLightProperties.cdbc

What a light is. A value of **−1 takes the default** (the Lights panel's settings).

| Field | Meaning |
|---|---|
| ID | light id (what assignments refer to) |
| Name | for the editor |
| Type | 0 = point, 1 = spot |
| Color | 0xRRGGBB |
| Intensity | × the colour. The stock torches are 1.4 × 0xFF9F49 |
| Radius | yards; the light is **exactly 0** here (the stock falloff reaches ~13 yd to 5%). Default 16 |
| Falloff | shape: 1 = smooth (default), higher = tighter around the source |
| InnerAngle, OuterAngle | spot: degrees from the axis at full strength / none beyond |
| FlickerMode | −1 default (smooth), 0 off, 1 smooth (a flame), 2 noise, 3 noise steps (no easing) |
| FlickerSpeed | × the default pace |
| FlickerAmount | 0..1, how much the light dips |
| Flags | reserved, 0 |

### DoodadLightAssignment.cdbc

Where lights go. Several rows per model are fine (a brazier with two flames).

| Field | Meaning |
|---|---|
| ID | row id |
| ModelPath | the model's path as the Light editor shows it; case and `/` vs `\` don't matter |
| AttachType | 0 = the model's origin, 1 = attachment point (by its ID), 2 = bone (by index), 3 = particle emitter (by index: its position, e.g. a flame) |
| AttachIndex | the attachment point ID / bone index / emitter index |
| AttachName | **reserved** for named attachment points, empty |
| OffsetX/Y/Z | yards from the attachment, in the model's space (scaled with the doodad) |
| DirectionX/Y/Z | spot lights: where the cone points, in the model's space (0, 0, −1 = down) |
| LightID | a DoodadLightProperties ID; **0 = no light** (with the flag below: only suppress) |
| Flags | 0x1 **suppress the model's own light entries** |

Every model in the client's world scene counts: map doodads, WMO doodad sets, game objects (campfires,
portals) and creatures (a fire elemental's model path gets its light too). A light follows its
attachment while the model animates (a swinging lantern swings its light). The **radius scales with
the model's scale** (a scaled-up fire reaches further). An attachment the model doesn't have puts the
light at the origin + offset; the Light editor marks such rows red.

## AreaClimate.cdbc

The air's baseline per place: temperature through the day and the year, humidity, fogginess, day
length. Field by field.

| Field | Meaning |
|---|---|
| ID | row id |
| ScopeType, ScopeID | the place |
| DayTemp | °C at the warmest time of day (15:00). Signed: −1000 inherits |
| NightTemp | °C at the coldest (05:00). Signed: −1000 inherits |
| SeasonAmplitude | °C between midsummer and midwinter (Elwynn ~12, jungle ~2, Outland 0) |
| SeasonOffset | months the seasons are shifted (6 = flipped, a "southern" continent) |
| Humidity | 0 = dry air .. 1 = saturated: humid places dry slowly |
| Flags | reserved, 0 |
| Fogginess | 0 .. 1: how foggy the place is by nature; drives area fog, boosts mist |
| Comment | for the editor |
| DayLength | hours the sunrise comes earlier (and the sunset later) at midsummer; the other way in midwinter, following the season curve (and SeasonOffset). 0 = stock day length all year; up to 3.5. × the setting `[ImmersiveAzeroth] SkyDayLength` |

No row at all: 18 °C day, 8 °C night, ±6 °C over the year, humidity 0.5, fogginess 0, day length
1.5 h. Blended across area borders.

**Day length** works on the time the client's light curves see, not on the light data: light time =
time + c·sin(2π·time), so midnight and noon stay, mornings run ahead in summer (the stock sunrise comes
earlier) and evenings behind (later sunset). M2 skyboxes and the daily temperature follow it.

**Temperature** = night .. day by the time of day (the sky's clock) + the season (from the sky clock's
date: warmest around 20 July, shifted by SeasonOffset) + the weather (rain −3 °C, snow −6 °C, sandstorm
+2 °C, × its intensity). The Climate panel shows each part, and can override the weather and the time
of day for testing.

## SkyTime.cdbc

The sky's time per place: **pinned** (always night above Karazhan, always noon somewhere) or **shifted**
(a time zone). It moves everything the sky clock drives: the sky, the lighting, the fog, M2 skyboxes,
the climate (a place pinned at night is cold). The on-screen clock and `GetGameTime()` stay on the
server's time. Optional: without the file, stock time everywhere. Field by field.

| Field | Meaning |
|---|---|
| ID | row id |
| ScopeType, ScopeID | the place |
| PinnedHour | 0 .. 24: the sky stays at this hour (23.5 = 23:30). −1 inherits; −2 = **not** pinned (a sub-area under a pinned map runs normally) |
| OffsetHours | hours added to the time where it isn't pinned (−3 = three hours behind). −1000 inherits |
| BlendSeconds | how long the sky takes to move to a new rule when you cross a border (0 = at once). −1 inherits; default 10. Only **area** (zone / sub-area) rules ease in: a map's or the global rule applies at once, as does arriving in the world (login, loading screen, teleport) |
| Flags | reserved, 0 |
| Comment | for the editor |
| PinnedDay | 1 .. 365: the place's **date** is pinned to this day of the year (172 = 21 June; a 365-day year): its season, climate, day length, the lore clock's day and the date Lua shows. The time of day still runs; the calendar and holidays stay on the realm date. −1 = the realm date's day (e.g. a sub-area under a pinned map); 0 inherits |

Blends go the short way round the clock (23:00 → a pin at 01:00 runs forward 2 h). **Map.dbc
`TimeOfDayOverride`** (stock, per map) still wins where a map sets it. The Sky panel shows the rule
where you are.

## Sky bodies: five tables

The sun, the moons and any custom body as data, drawn instead of the client's own. Without the files,
or with `[ImmersiveAzeroth] SkyBodies` off, the client draws its own sun and moons. The **Sky panel**
shows the bodies where you are, scrubs and speeds up the clock, and pins lore time for testing.

### SkyProfile.cdbc

The place's sky, field by field.

| Field | Meaning |
|---|---|
| ID, ScopeType, ScopeID | the row, the place |
| BodySet | SkyBodySet ID; 0 inherits; −1 = the client's own sun and moons here |
| HideZoneSkybox | 1 = hide this place's M2 skybox (the strongest one), 0 = don't; −1 inherits |
| HideAllSkyboxes | 1 = hide every M2 skybox here; −1 inherits |
| NorthOffset | degrees a map's north is turned (custom maps); −1000 inherits |
| Flags, Comment | reserved / for the editor |
| LoreYear | the **lore clock**: the story's year here, whole years on the lore calendar (0 = the Dark Portal opens, 27 = WotLK, 28 = the Embrace, −10000 = long before). −1000000 inherits; nowhere set: `[ImmersiveAzeroth] LoreYear` (27). Only bodies with SkyBody `Clock` 1 read it |
| LoreWobbleYears | lore time swings this many years over the place's year (smooth, back at New Year); −1 inherits, 0 = none |
| LoreWobbleMode | 0 **around** (−A .. +A), 1 **before** (dips to −A at midyear), 2 **after** (rises to +A); −1 inherits |
| LoreYearsPerDay | 0 = lore time frozen at LoreYear (+ the day of the year + the wobble). > 0 = **running** ("cinematic"): lore years per realm day from LoreStartDate on (14.3 = a 430-year journey in a month); −1 inherits |
| LoreStartDate | running lore time: the realm date (yyyymmdd, e.g. 20261101) on which the place is at LoreYear; it counts on from there (and back before it). 0 inherits; set nowhere: 1 January of the current realm year (lore time then jumps back at New Year: set a date to avoid that). Every player sees the same moment: it counts on the server's date |

### SkyBodySet.cdbc and SkyBodySetMember.cdbc

| SkyBodySet | | SkyBodySetMember | |
|---|---|---|---|
| ID, Name | "Azeroth" | ID | |
| ParentSet | a set this one extends (its members plus these); 0 none | SetID, BodyID | |
| Flags, Comment | | Flags | 1 = off (removes a parent's body from this set) |

### SkyBody.cdbc

| Field | Meaning |
|---|---|
| ID, Name | Name is what Lua uses |
| Texture | BLP path; "" = the stock texture of its StockRole |
| StockRole | 0 none, 1 sun, 2 White Lady, 3 Blue Child: the stock body it replaces / starts from |
| Size | angular diameter in degrees (horizon to zenith = 90°); 0 = the client's size (with its size curve) |
| Color | ARGB tint; 0xFFFFFFFF = none (the client's colour and fade; its black Blue Child counts as white) |
| Blend | 0 alpha, 1 **additive** (glows: added to the sky behind it; uses our own draw, Flags 4) |
| Order | draw order: lower first = behind; equal: file order |
| PathID | SkyPath; 0 = the stock path |
| PeriodDays | one trip along the path, in days. 1 = daily: follows the place's time of day (pinned, shifted, day length); other values run on the unpinned base time, so they keep moving in pinned places; 0 = hangs still at PhaseDays |
| PhaseDays | offset along the path, in days (a daily orbit: 0 = lowest at midnight, highest at noon; 0.5 = highest at midnight) |
| VisiblePeriodDays, VisibleDays, VisibleOffsetDays | the visibility cycle on the base time: in the sky VisibleDays out of every VisiblePeriodDays, the window starting VisibleOffsetDays after midnight of day 0 (1970-01-01). "Every third night": 3, 1, 0.5 (noon to noon). 0 period = always |
| LitMode, LitPeriodDays | reserved (moon phases, later) |
| LightColor, LightStrength | with Flags 8: the world light's colour while it drives it: the zone's light colour × `LightColor`, by `LightStrength` (0 = the zone's colour as is, 1 = fully times LightColor) |
| Flags | 1 = **own colour**: drawn in its `Color` as is (white = the texture as painted) instead of tinted by the zone's sun colour band, which the client puts on the sun and the White Lady (bluish at night in most zones); the client's storm fade still applies. 2 = **no client glare**: hides the glare the client draws for its StockRole after the sky (the sun glare; for the White Lady the moon glare). 4 = **our own draw**: drawn right after the sky gradient instead of by the client before it, so the gradient doesn't wash it out (detailed moon textures keep their craters at dusk); by day it fades to its `DayVisibility`; clouds still cover it. Always in front of bodies the client draws (Order only sorts own-drawn bodies among themselves): give both bodies of an overlapping pair the flag. Blend 1 and a rotation imply it. 8 = **drives the world light**: the light's direction (terrain, buildings, doodads and characters are shaded from it) comes from this body while it's up; within 4° of the horizon it hands over to the next driving body in the set (lower `Order` first) or to the stock light (which never follows the sun: always from the NW, 20–37° high). Combinable (3 = 1 + 2). The glares follow the set's sun and White Lady role bodies on their own |
| Comment | |
| VisibleEase | how it arrives / leaves: flags 1 fade, 2 scale, 4 sink (rises from / drops below the horizon), combinable; 0 = pops |
| VisibleEaseSeconds | seconds of sky time the arrival takes at the window's start and the leaving after its end |
| Clock | 0 = the realm clock; 1 = the **lore clock** (SkyProfile LoreYear…): PeriodDays and PhaseDays then count **lore years** (430, −28 = an Embrace cycle with its start in year 28). Visibility windows and path modifiers stay on the realm clock |
| DayVisibility | our own draw only: how much of it shows at full day, 0..1: 0 = gone by day like the stars, 0.25 = a pale day moon, 1 = fully (the sun). It fades between night and day with the client's own sun (−6° to +10°). −1 = auto: 1 for the sun role, 0.25 for anything else |
| Rotation, RotationPerDay | any value but 0 uses our own draw: the picture turned clockwise as you look at it, in degrees, plus degrees per realm day on top (a slowly turning planet). Our own draw keeps the picture's top toward the zenith (the client's turns with the camera) |

### SkyPath.cdbc

| Field | Meaning |
|---|---|
| ID, Name | |
| Type | 0 stock (the client's curves for the body's role, read live); 1 orbit; 2 keyframes (SkyPathKey); 3 follow |
| Frame | reserved (a star frame, later) |
| RiseAzimuth | orbit: compass degrees (0 north, 90 east) where it comes up; the map's NorthOffset is added |
| Lift | orbit: degrees the whole circle is raised (0 = a great circle: up half the trip, sets opposite where it rose; Lift ≥ Height = never sets) |
| Height | orbit: highest elevation before Lift (peaks over RiseAzimuth + 90) |
| FollowBody | follow: the leader's SkyBody ID (any path type, a follower too, up to 4 deep) |
| SeparationMin, SeparationMax, SeparationDays | follow: degrees from the leader, swinging from Min to Max and back over SeparationDays (base time); 0 days = always Min |
| SeparationAngle | follow: which way from the leader, in the sky's own frame as you look at it: 0 = below it (toward the horizon), 90 = to its right, 180 = above, 270 = to its left. Never flips, wherever the leader goes |
| WobbleDegrees, WobbleDays | any path, stock included: swings the body up/down by WobbleDegrees over WobbleDays (base time), and sideways by 0.6× that on an unrelated period, so it traces a slowly changing loop |
| DriftDegreesPerDay | any path: turns the whole path around the zenith, compass degrees per day (base time) |
| SizePulse, SizePulseDays | any path: size × (1 ± SizePulse) over SizePulseDays (0.05 = ±5%) |
| Flags, Comment | |

### SkyPathKey.cdbc

The points of a keyframe path (SkyPath Type 2). The body runs a smooth loop through them (Catmull-Rom
over their times); the last key runs on to the first.

| Field | Meaning |
|---|---|
| ID, PathID | the key; its SkyPath |
| Time | 0..1 along the path (one trip = the body's PeriodDays) |
| Azimuth, Elevation | compass degrees (the map's NorthOffset is added), degrees above the horizon (negative = below) |
| Size | multiplier on the body's Size, interpolated along the same curve; 0 reads as 1 |
| Flags, Comment | |
| LeaderBrightness | follow paths: the leader's brightness at this key (0.8 = dimmed to 80%), interpolated; 0 reads as 1 |
| Order | follow paths: +1 = drawn in front of the leader, −1 = behind, 0 = by the bodies' Order. Changes only at a key (the segment's start key decides): put those keys where the two are apart |

**On a follow path** (SkyPath Type 3) the keys are **relative to the leader**, over the follower's own
cycle (its PeriodDays, on its Clock): `Azimuth` = degrees to the leader's right, `Elevation` = degrees
above it (the sky's own frame, as you look at it; degrees at the leader's base size: they grow and
shrink with its size curve, e.g. the stock White Lady's ×1.5 near the horizon, so the pair keeps its
look at every height), `Size` × the body's Size, or × the leader's current size when the body's Size
is 0 (0.6 = 60% of the leader). With keys, the Separation… columns are ignored. The sample "Azeroth
(lore)" set (BodySet 3) uses this for the Blue Child's Embrace cycle: its own White Lady (stock route,
16°), the Blue Child at 60% of her, leaving to her right after the Embrace and coming home from her
left.

## FogSettings.cdbc

How the ground fog behaves and looks, per place and fog kind (the volumetric fog, Vulkan). Optional:
without the file (or for any field no row sets) the built-in values for the kind apply. The **Fog
panel** shows the values in effect where you stand and can try others live.

| Field | Meaning |
|---|---|
| ID | row id |
| ScopeType, ScopeID | the place |
| Kind | 0 = all kinds, 1 mist, 2 water fog, 3 steam, 4 area fog, 5 haze |
| Strength | multiplies the kind's target from its rules (0 = never here) |
| Density | extinction per yd at the ground at full amount (mist ~0.06) |
| Thickness | yd: layer height at full amount (the pool's depth for heavy fog) |
| BuildUp / BurnOff | half-lives (s) toward a higher / lower target |
| WindThin | wind strength (0..1) above which it can't settle |
| WindDrift | how strongly the wind moves it (0..1) |
| Patchiness | noise amount (0 smooth .. 1 patchy) |
| Weight | 0 = rises and thins upward .. 1 = heavy: flat-topped, flows downhill, pools in hollows |
| ColorMode | 0 = the zone's stock fog colour (Color is ignored), 1 = hue (the stock colour's brightness with Color's hue and saturation: follows day and night), 2 = replace (Color as is); −1 inherits. No row sets a mode but one sets a Color: hue |
| ColorR/G/B | 0 .. 1 |
| Saturation | 0 = grey .. 1 = as is (on top of the mode) |
| Brightness | multiplier |
| Flags | reserved, 0 |
| Comment | for the editor |

All floats: −1 takes the field from the next row. Lookup (most specific wins): place first (the area,
its parent zones, the map, global), and within each place the kind's own row before the Kind 0 row.
Values never add up.

Colour per kind: a kind's own row can set its colour (e.g. whiter steam), the Kind 0 row is the
fallback for every kind. Where kinds mix, the fog shows their colours by each one's share there.
