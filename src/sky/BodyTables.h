// The sky's body tables (WXL: orchestration docs/r&d/immersion/sky/phase2-celestial-bodies.md):
//   SkyProfile          the place's sky (scoped: Global < Map < Area): body set, the two skybox
//                       flags, the map's north, the lore clock
//   SkyBodySet          named sets of bodies ("Azeroth"), a set may extend a parent set
//   SkyBodySetMember    which bodies a set has (a member can switch a parent's body off)
//   SkyBody             one body: texture, size, colour, draw order, path, cycles
//   SkyPath             how a body moves (stock / orbit / keyframes / follow)
//   SkyPathKey          the points of a keyframe path
// All optional: without them the client draws its own sun and moons.
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "World.h"

namespace ia::sky::bodytables {
    enum class StockRole : int { None = 0, Sun = 1, WhiteLady = 2, BlueChild = 3 };

    // SkyBody Flags.
    constexpr uint32_t kBodyOwnColor = 0x1;    // its Color as is, not tinted by the zone's sun colour band
    constexpr uint32_t kBodyNoGlare = 0x2;     // hide the client's glare of its StockRole
    constexpr uint32_t kBodyOwnDraw = 0x4;     // drawn after the sky gradient (not washed out by it); implied by Blend 1 and rotation
    constexpr uint32_t kBodyDrivesLight = 0x8; // the world light comes from it while it's up
    enum class PathType : int { Stock = 0, Orbit = 1, Keyframes = 2, Follow = 3 };

    struct Body {
        uint32_t id = 0;
        std::string name;
        std::string texture; // "" = the stock texture of its role
        StockRole role = StockRole::None;
        float size = 0.0f;   // angular diameter, degrees; 0 = the stock size
        uint32_t color = 0xFFFFFFFFu;
        int blend = 0;       // 0 alpha, 1 additive
        int order = 0;       // lower draws first (behind)
        uint32_t pathId = 0;
        float periodDays = 1.0f, phaseDays = 0.0f;
        float visiblePeriodDays = 0.0f, visibleDays = 0.0f, visibleOffsetDays = 0.0f;
        int visibleEase = 0; // flags: 1 fade, 2 scale, 4 sink; 0 = pops
        float visibleEaseSeconds = 0.0f;
        int litMode = 0;
        float litPeriodDays = 0.0f;
        uint32_t lightColor = 0xFFFFFFFFu;
        float lightStrength = 0.0f;
        int clock = 0;       // 0 realm clock, 1 lore clock (PeriodDays / PhaseDays then count years)
        float dayVisibility = -1.0f; // 0..1 of it showing at full day (own draw); < 0 = auto (sun 1, others 0.25)
        float rotation = 0.0f;       // degrees, the picture turned clockwise as you look at it
        float rotationPerDay = 0.0f; // degrees per realm day on top
        uint32_t flags = 0;
        uint32_t row = 0;    // file order (equal Order: row order)
    };

    struct Path {
        uint32_t id = 0;
        std::string name;
        PathType type = PathType::Stock;
        int frame = 0; // 0 ground, 1 stars
        float riseAzimuth = 90.0f, lift = 0.0f, height = 60.0f;
        uint32_t followBody = 0;
        float separationMin = 0.0f, separationMax = 0.0f, separationDays = 0.0f, separationAngle = 0.0f;
        float wobbleDegrees = 0.0f, wobbleDays = 0.0f, driftDegreesPerDay = 0.0f;
        float sizePulse = 0.0f, sizePulseDays = 0.0f;
        uint32_t flags = 0;
    };

    // One point of a keyframe path. On a follow path the keys are relative to the leader:
    // azimuth = degrees to its right, elevation = degrees above it (the sky's own frame).
    struct PathKey {
        float time = 0.0f;
        float azimuth = 0.0f;
        float elevation = 0.0f;
        float size = 1.0f;
        float leaderBrightness = 1.0f;
        int order = 0; // follow: +1 in front of the leader, -1 behind, 0 by Order
    };

    // The resolved profile where the player is.
    struct Profile {
        int bodySet = -1; // SkyBodySet ID; -1 = stock bodies
        bool hideZoneSkybox = false;
        bool hideAllSkyboxes = false;
        float northOffset = 0.0f;
        // The lore clock (years on the lore calendar: 0 = the Dark Portal, 27 = WotLK).
        int loreYear = 27;
        float loreWobbleYears = 0.0f;
        int loreWobbleMode = 0;   // 0 around, 1 before, 2 after
        float loreYearsPerDay = 0.0f;
        int loreStartDate = 0;    // running: yyyymmdd of the realm date it's at loreYear; 0 = 1 January this realm year
        int scopeType = -1;
        uint32_t scopeId = 0;
    };

    // Reloads when any of the tables changed. Once per frame.
    void Update();
    // The lore year where no SkyProfile row sets one (setting LoreYear, default 27).
    void SetDefaultLoreYear(int year);
    int DefaultLoreYear();
    const char* Status();

    const Profile& ProfileHere(const world::Snapshot& snap);

    // The bodies of a set, its parents' included (members switched off removed), sorted by Order then
    // file row. Cached per set.
    const std::vector<const Body*>& SetBodies(int setId);
    const char* SetName(int setId);

    const Path* FindPath(uint32_t id);
    const Body* FindBody(uint32_t id);
    // A keyframe path's points, sorted by Time (empty if none).
    const std::vector<PathKey>& PathKeys(uint32_t pathId);
    // Bumped whenever the tables were reloaded (cached results keyed by it).
    uint32_t Generation();
}
