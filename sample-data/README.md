# Sample data

Seyri's own tables, as Seyri plays with them: a working setup to start from and to learn the tables by.
The folder mirrors the client's: copy its contents into the client folder (or pack them into a patch
MPQ). `DbFilesClient\` holds the tables; `environments\stars\generic\` the moon textures the sky bodies
use. Then open the overlay's Data view (Ctrl+F9) to read and change the tables: every field explains
itself in its tooltip. The columns are described in [../docs/cdbc-tables.md](../docs/cdbc-tables.md).

Rows whose comment starts with "Example:" show a feature rather than a choice for the world (a red star
every third night, Outland's own sky); delete them freely.

| File | Rows | What's in it |
|---|---|---|
| AreaClimate | 22 | A global baseline and about twenty places: Northrend and Winterspring cold, Tanaris and Burning Steppes hot, the foggy zones (Duskwood, Tirisfal, Wetlands, Zangarmarsh), a sub-area with its own climate (Westfall's Longshore) |
| SkyTime | 3 | Deadwind Pass always at night, Kalimdor two hours behind, Elwynn Forest pinned to mid-January |
| SkyProfile | 6 | The Azeroth set everywhere; examples: Outland's own sky, lore years for the Caverns of Time (Old Hillsbrad, Hyjal), Teldrassil with the lore moons, Darkshore playing the Embrace journey in a month |
| SkyBodySet, SkyBodySetMember | 3, 14 | "Azeroth" (the stock sky, the Blue Child white), "Draenor" (an example), "Azeroth (lore)" (the Blue Child on its Embrace cycle around the White Lady) |
| SkyBody, SkyPath, SkyPathKey | 12, 9, 37 | The sun and both moons; examples of every path type: a stock route, orbits (a red star, a wanderer that never sets, a drifting red moon that lights the world), keyframe loops (a huge planet, a blue shard), followers (a companion of the White Lady, the lore Blue Child) |
| WindProfile | 15 | A global wind, calm in the Deadmines and Orgrimmar, strong across the Barrens, sea winds from a fixed direction in Westfall, Stormwind and Stranglethorn |
| GroundMaterial, GroundMaterialSelector | 22, 22 | Snow (by TerrainType, and named snow textures from Ironforge to Icecrown), desert and beach sands, Elwynn's dirt and the Swamp of Sorrows' muck (mud after rain), Burning Steppes' lava (a heat source), hot ash and hot rock |
| SurfaceCover | 19 | Snow covers, sand covers with their own textures, mud that only forms on wet ground (MoistureMin), thin ash |
| FogSettings | 7 | Fog colours and kinds for Swamp of Sorrows, Duskwood, Zangarmarsh and the Wetlands, whiter steam everywhere |
| DoodadLightProperties, DoodadLightAssignment | 20, 163 | Torches, lanterns, candles, coloured flames, mushrooms and energy lights, on 155 models (torches, lampposts, braziers, wagons, cauldrons...), mostly at their particle emitters (the flame), some at bones or attachment points, a few replacing the model's own light |
| GroundEffectDoodadDensity | 46 | A global 6x out to 1024 yd, then zones and single plant models from 1x to 8x (Darkshore, the Plaguelands, Teldrassil and others) |
| GroundEffectDoodadWind | 37 | Plant models kept out of the wind (19) or out of the wind and the parting (18), several with a stiffness |

Some rows name textures from later expansions (Vashj'ir, Hyjal): they come from a map with those
tilesets and simply match nothing on a stock map.
