// Climate: the air's baseline temperature and humidity per place, and the clock it runs on
// (WXL: orchestration docs/r&d/immersion/world-fields.md, environment-state.md).
//
// AreaClimate.cdbc, scoped: Global < Map < Area (zone) < Area (sub-area), the more specific row wins
// field by field. A field of -1 inherits from the next place up; temperatures are signed, so for them
// -1000 and below inherit (-1 degC is a real value).
//
// Temperature (degC) = night .. day by the time of day (coldest ~05:00, warmest ~15:00)
//                    + season (SeasonAmplitude across the year, peak in late July, shifted by
//                      SeasonOffset months) + weather (rain and snow cool, sandstorms warm).
#pragma once

#include <cstdint>
#include "World.h"

namespace ia::climate {
    struct Row {
        float dayTemp = 18.0f;         // degC at the warmest time of day
        float nightTemp = 8.0f;        // degC at the coldest
        float seasonAmplitude = 12.0f; // degC between midsummer and midwinter
        float seasonOffset = 0.0f;     // months the seasons are shifted (6 = flipped)
        float humidity = 0.5f;         // 0 dry air .. 1 saturated
        float fogginess = 0.0f;        // 0 .. 1: how foggy the place is by nature
        float dayLength = 1.5f;        // hours sunrise comes earlier (and sunset later) at midsummer
        uint32_t flags = 0;            // reserved
    };

    // The resolved row for a place (area chain + map), cached per (area, map). A hard step at every
    // area border: for "the climate here", use At().
    const Row& For(uint32_t areaId, int mapId);

    // The climate at a world position, blended across area borders: each terrain chunk (33 yd) takes
    // its area's row, averaged with its 8 neighbours, interpolated between chunk centres (~100 yd).
    // Within ~800 yd of the player; beyond it, or before its chunks are known, the area's own row.
    Row At(float x, float y);
    // The areas At() mixes at a position and their weights (summing to 1). Returns how many.
    int AreaWeightsAt(float x, float y, uint32_t* areas, float* weights, int max);
    // At() where the player is.
    Row Here();

    // The clock the climate runs on: the sky clock's place date and the time of day the sky shows.
    struct Clock {
        bool valid = false;
        int year = 0, month = 0, day = 0, hour = 0, minute = 0; // month 1..12, day 1..31
        float dayFraction = 0.0f;  // 0 = midnight .. 1
        float yearFraction = 0.0f; // 0 = 1 January .. 1
    };
    const Clock& Now();

    struct Breakdown {
        float daily = 0.0f;   // night .. day by the time of day
        float season = 0.0f;  // +- half the amplitude
        float weather = 0.0f; // rain/snow cooling, sand warming
        float test = 0.0f;    // the panel's test offset
        float total = 0.0f;
    };
    Breakdown Temperature(const Row& row);
    Breakdown TemperatureAtTime(const Row& row, float dayFraction);
    // degC of standing water: the day's mean plus the season, without the weather's chill.
    float WaterTemperature(const Row& row);
    float TemperatureAt(uint32_t areaId, int mapId);

    // The sun: the direction toward it (z up) and how much it shines (0 at night .. 1 by day).
    struct Sun {
        float dir[3] = { 0.0f, 0.0f, 1.0f };
        float daylight = 0.0f;
        bool fromClient = false;
    };
    const Sun& SunNow();
    // The world light's direction this frame (from the sky toward the ground), from the sky's lighting.
    void SetLightDirection(const float dir[3]);

    constexpr float kSunDegrees = 4.0f;
    float SunWarming(const float normal[3], float open);

    struct Weather {
        int type = 0; // 0 fine, 1 rain, 2 snow, 3 sand
        float intensity = 0.0f;
    };
    const Weather& CurrentWeather();

    // Once per frame, after world::Refresh() and the sky clock.
    void Update(const world::Snapshot& snap);

    const char* Status();
    uint32_t Generation();

    // --- panel overrides (testing; not saved) ---
    void SetWeatherOverride(int type, float intensity); // type < 0 follows the live weather
    int WeatherOverrideType();
    float WeatherOverrideIntensity();
    void SetTemperatureOffset(float degrees);
    float TemperatureOffset();
}
