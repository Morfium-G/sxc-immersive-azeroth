#include "Saved.h"

#include "cover/Cover.h"
#include "env/Shelter.h"
#include "fog/Field.h"
#include "fog/Fog.h"
#include "fog/Wake.h"
#include "lights/Lights.h"

// The panels' settings in SXC.ini. Testing and debug switches stay out on purpose (the extra haze, the
// covers' x depth / breakup / drift, the debug views): they're for trying something now.
namespace ia::saved {
    namespace {
        using fog::Look;
        using FogField = fog::FieldSettings;
        using Wake = fog::wake::Settings;
        using LightSettings = lights::Settings;
        using CoverLook = cover::Look;

        Block<Look> g_fog("ImmersiveAzerothFog", {
            Flag<Look>("Enabled", "Fog: draw it", "The volumetric fog (Vulkan).", &Look::enabled),
            Value<Look>("Brightness", "Fog: brightness", "x every fog colour.", &Look::brightness, 0.0f, 3.0f),
            Value<Look>("DensityScale", "Fog: density x", "x every density.", &Look::densityScale, 0.0f, 5.0f),
            Value<Look>("Patchiness", "Fog: patchiness x", "x the kinds' patchiness.", &Look::noiseAmount, 0.0f, 1.0f),
            Value<Look>("PatchSize", "Fog: patch size", "Yards per noise tile (patches ~1/8 of it).", &Look::noiseTile, 8.0f, 256.0f),
            Value<Look>("Drift", "Fog: drift with the wind x", "How fast the patches drift with the wind.", &Look::drift, 0.0f, 4.0f),
            Value<Look>("Reach", "Fog: reach", "Yards the volume reaches; the sky gets this much fog too.", &Look::farDistance, 50.0f, 800.0f),
            Value<Look>("PoolSoftness", "Fog: pools' soft top", "The pools' soft top, as a share of their depth.", &Look::poolSoftness, 0.0f, 1.0f),
            Whole<Look>("FloorReach", "Fog: floor reach", "Cells around: the light layer starts on the lowest ground this far, so it doesn't drape up walls.",
                        &Look::floorReach, 0, 4),
            Value<Look>("Step", "Fog: step", "Yards: the fog is integrated in steps this long near you. Smaller = smoother thin layers, more GPU.",
                        &Look::stepLength, 0.1f, 4.0f),
            Whole<Look>("MaxSteps", "Fog: max steps", "Per pixel: the most steps a ray takes.", &Look::maxSteps, 16, 512),
            Flag<Look>("Froxels", "Fog: froxels", "The froxel volume instead of a march per pixel (cheap, coarse).", &Look::froxels),
            Flag<Look>("FarFog", "Fog: far fog", "Each zone's typical fog beyond the near field.", &Look::farFog),
            Value<Look>("Handover", "Fog: hand-over band", "Yards: the near fog fades into the far fog over this band.", &Look::handover, 4.0f, 120.0f),
            Value<Look>("FadeIn", "Fog: new cells fade in", "Seconds a near cell that just started takes to fade in over the far fog.", &Look::fadeIn, 0.0f, 5.0f),
            Flag<Look>("Lights", "Fog: our lights light it", "Torches, braziers and lamps glow in the fog around them.", &Look::lights),
            Value<Look>("LightStrength", "Fog: lights' strength", "x their light in the fog.", &Look::lightStrength, 0.0f, 4.0f),
            Value<Look>("LightGlow", "Fog: glow toward a lamp", "Forward scattering: brighter looking toward a lamp (0: the same every way).", &Look::lightGlow, 0.0f, 0.8f),
            Value<Look>("LightRange", "Fog: lights' range", "Yards from you the lights light the fog.", &Look::lightRange, 16.0f, 256.0f),
        });

        Block<FogField> g_fogField("ImmersiveAzerothFog", {
            Value<FogField>("PoolSpeed", "Fog: pools run downhill x", "How fast heavy fog runs downhill (0: it stays put).", &FogField::poolSpeed, 0.0f, 4.0f),
            Value<FogField>("SteepStart", "Fog: steep from", "Degrees: a growing share of the ground-hugging fog doesn't form ...", &FogField::steepStart, 10.0f, 60.0f),
            Value<FogField>("SteepFull", "Fog: steep fully", "... and from here none.", &FogField::steepFull, 10.0f, 80.0f),
            Flag<FogField>("BlendPlaces", "Fog: blend places", "The places' kind settings blend across area borders like the climate.", &FogField::blendPlaces),
        });

        Block<Wake> g_wake("ImmersiveAzerothFogWake", {
            Flag<Wake>("Enabled", "Fog wake: units push the fog aside", "Units part the ground fog as they move.", &Wake::enabled),
            Value<Wake>("Strength", "Fog wake: push while moving", "How much fog a moving unit pushes out at its body.", &Wake::strength, 0.0f, 1.0f),
            Value<Wake>("Standing", "Fog wake: push while standing", "... while standing still.", &Wake::standing, 0.0f, 1.0f),
            Value<Wake>("TrailSeconds", "Fog wake: fills back in", "Seconds the wake takes to fill back in.", &Wake::trailSeconds, 0.5f, 10.0f),
            Value<Wake>("RadiusScale", "Fog wake: radius x", "The wake's radius as a multiple of the unit's.", &Wake::radiusScale, 0.5f, 4.0f),
            Value<Wake>("Spread", "Fog wake: widens", "The wake widens by this x its radius over its lifetime.", &Wake::spread, 0.0f, 3.0f),
            Value<Wake>("HeightScale", "Fog wake: height x", "The wake's height as a multiple of the unit's.", &Wake::heightScale, 0.5f, 3.0f),
            Value<Wake>("Rim", "Fog wake: piled at the rim", "Fog piled up at the wake's rim (share of the push).", &Wake::rim, 0.0f, 1.0f),
            Value<Wake>("Swirl", "Fog wake: swirl", "How far the fog's patches are dragged and curled.", &Wake::swirl, 0.0f, 3.0f),
        });

        Block<LightSettings> g_lights("ImmersiveAzerothLights", {
            Flag<LightSettings>("Enabled", "Lights: ours", "Our point lights (torches, braziers, lamps).", &LightSettings::enabled),
            Flag<LightSettings>("StockOff", "Lights: stock terrain lights off", "The client's own 3 per terrain chunk off while ours are on.", &LightSettings::stockOff),
            Flag<LightSettings>("SeparateIndoor", "Lights: inside and outside apart", "A light inside lights only indoor surfaces, one outside only outdoor ones.",
                                &LightSettings::separateIndoor),
            Flag<LightSettings>("Shadows", "Lights: shadows", "The nearest still lights get a cube shadow map.", &LightSettings::shadows),
            Flag<LightSettings>("ShadowDoodads", "Lights: doodads cast", "The placed doodads cast too.", &LightSettings::shadowDoodads),
            Whole<LightSettings>("ShadowCount", "Lights: lights with shadows", "The nearest this many lights get a shadow map.", &LightSettings::shadowCount, 0, 64),
            Whole<LightSettings>("ShadowUpdates", "Lights: shadow maps per frame", "Shadow maps drawn per frame at most.", &LightSettings::shadowUpdates, 1, 16),
            Value<LightSettings>("Radius", "Lights: default radius", "Yards: a light is exactly 0 here (unless its row says).", &LightSettings::radius, 2.0f, 40.0f),
            Value<LightSettings>("Brightness", "Lights: brightness", "x the lights' colours.", &LightSettings::brightness, 0.0f, 4.0f),
            Value<LightSettings>("Flicker", "Lights: flicker", "How much a flame dips.", &LightSettings::flicker, 0.0f, 1.0f),
            Value<LightSettings>("FlickerPace", "Lights: flicker pace", "x the default pace.", &LightSettings::flickerSpeed, 0.1f, 4.0f),
            Value<LightSettings>("MergeWithin", "Lights: merge within", "Yards: closer lights merge.", &LightSettings::mergeDistance, 0.0f, 3.0f),
            Value<LightSettings>("Range", "Lights: range", "Yards from the camera, fading over the last third.", &LightSettings::range, 32.0f, 512.0f),
            Value<LightSettings>("CellSize", "Lights: cell size", "Yards per grid cell.", &LightSettings::cellSize, 4.0f, 32.0f),
            Whole<LightSettings>("PerCell", "Lights: per cell", "Lights a cell holds (the strongest at its middle).", &LightSettings::maxPerCell, 1, 32),
        });

        Block<CoverLook> g_cover("ImmersiveAzerothCover", {
            Flag<CoverLook>("Enabled", "Covers: on", "Snow, sand, mud and wetness in the terrain.", &CoverLook::enabled),
            Flag<CoverLook>("FallenSnow", "Covers: fallen snow", "Snow from the world fields lies on top.", &CoverLook::fallenSnow),
            Flag<CoverLook>("Depth", "Covers: depth", "The terrain raised onto the covers (tessellation).", &CoverLook::depth),
            Value<CoverLook>("TessDetail", "Covers: detail", "Yards per segment per yard of distance (smaller: finer, more GPU).", &CoverLook::tessDetail, 0.004f, 0.06f),
            Value<CoverLook>("TessReach", "Covers: subdivided within", "Yards.", &CoverLook::tessReach, 30.0f, 600.0f),
            Whole<CoverLook>("TessMost", "Covers: segments per edge at most", "", &CoverLook::tessMost, 1, 32),
            Flag<CoverLook>("Trails", "Covers: trails", "Feet press the cover down.", &CoverLook::trails),
            Whole<CoverLook>("TrailCarve", "Covers: where a foot carves", "0 through the cover, 1 near contact, 2 up to the lift.", &CoverLook::trailCarve, 0, 2),
            Value<CoverLook>("TrailLift", "Covers: carves up to", "Yards above the unit (mode 2).", &CoverLook::trailLift, 0.0f, 1.5f),
            Value<CoverLook>("TrailPersist", "Covers: trails stay", "Seconds the coarse layer keeps a trail.", &CoverLook::trailPersist, 30.0f, 3600.0f),
            Value<CoverLook>("TrailSlur", "Covers: soft edges by depth", "Yards of edge softening per yard of cover depth.", &CoverLook::trailSlur, 0.0f, 2.0f),
            Value<CoverLook>("TrailRim", "Covers: rim x", "x the rows' rims.", &CoverLook::trailRim, 0.0f, 3.0f),
            Value<CoverLook>("TrailReach", "Covers: units within", "Yards: units this close to you leave trails.", &CoverLook::trailReach, 5.0f, 48.0f),
            Value<CoverLook>("Brightness", "Covers: plain cover colour", "A plain cover's colour.", &CoverLook::brightness, 0.3f, 1.0f),
            Value<CoverLook>("WetDarkening", "Covers: wet darkening", "How much darker soaked ground gets.", &CoverLook::wetDarkening, 0.0f, 1.0f),
            Value<CoverLook>("ThinDepth", "Covers: snow hides the ground from", "Yards of fallen snow from which the ground is hidden.", &CoverLook::thinDepth, 0.005f, 0.5f),
        });

        // The wind panel's shelter: functions, not a struct; mirrored here.
        struct Shelter {
            bool roofs = true;
            bool lee = true;
            float reach = shelter::LeeParams{}.reach;
            float angle = shelter::LeeParams{}.angleDeg;
            float strength = shelter::LeeParams{}.strength;
        };

        Block<Shelter> g_shelter("ImmersiveAzerothWind", {
            Flag<Shelter>("Roofs", "Wind: roofs calm it", "Under a roof the wind drops.", &Shelter::roofs),
            Flag<Shelter>("Lee", "Wind: lee behind walls and cliffs", "Behind something upwind the wind drops.", &Shelter::lee),
            Value<Shelter>("LeeReach", "Wind: lee reach", "Yards upwind an obstacle still casts its lee.", &Shelter::reach, 2.0f, 40.0f),
            Value<Shelter>("LeeAngle", "Wind: lee angle", "Degrees: only obstacles steeper than this block.", &Shelter::angle, 0.0f, 45.0f),
            Value<Shelter>("LeeStrength", "Wind: lee strength", "Wind removed right behind an obstacle.", &Shelter::strength, 0.0f, 1.0f),
        });

        void SyncShelter() {
            const shelter::LeeParams p = shelter::GetLeeParams();
            Shelter s;
            s.roofs = shelter::Enabled();
            s.lee = shelter::LeeEnabled();
            s.reach = p.reach;
            s.angle = p.angleDeg;
            s.strength = p.strength;
            g_shelter.Sync(s);

            if (s.roofs != shelter::Enabled()) shelter::SetEnabled(s.roofs);
            if (s.lee != shelter::LeeEnabled()) shelter::SetLeeEnabled(s.lee);

            // Reach and angle re-trace the lee cache: only when they changed.
            if (s.reach != p.reach || s.angle != p.angleDeg || s.strength != p.strength) {
                shelter::LeeParams q = p;
                q.reach = s.reach;
                q.angleDeg = s.angle;
                q.strength = s.strength;
                shelter::SetLeeParams(q);
            }
        }
    }

    void SyncAll() {
        g_fog.Sync(fog::LookSettings());
        g_fogField.Sync(fog::Field());
        g_wake.Sync(fog::wake::Config());
        g_lights.Sync(lights::Config());
        g_cover.Sync(cover::LookSettings());
        SyncShelter();
    }
}
