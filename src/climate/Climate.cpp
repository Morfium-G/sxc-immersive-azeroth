#include "climate/Climate.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#include "TableWatch.h"
#include "sky/Clock.h"

namespace ia::climate {
    namespace {
        enum Scope : uint32_t { kGlobal = 0, kMap = 1, kArea = 2 };
        enum FieldId { kDay, kNight, kAmplitude, kOffset, kHumidity, kFogginess, kDayLength, kFieldCount };
        constexpr const char* kColumns[kFieldCount] = { "DayTemp", "NightTemp", "SeasonAmplitude", "SeasonOffset", "Humidity", "Fogginess", "DayLength" };
        constexpr bool kSigned[kFieldCount] = { true, true, false, false, false, false, false };

        // Signed columns inherit at -1000 and below, the others at any negative value.
        bool IsSet(int f, float v) { return kSigned[f] ? v > -1000.0f : v >= 0.0f; }

        struct Raw {
            float value[kFieldCount];
            uint32_t flags = 0;
        };

        TableWatch g_table("AreaClimate");
        std::map<std::pair<uint32_t, uint32_t>, Raw> g_rows; // (scope type, scope id) -> row
        std::map<std::pair<uint32_t, int>, Row> g_cache;     // (area, map) -> resolved
        std::string g_status = "not loaded yet";
        uint32_t g_generation = 1;

        Clock g_clock;
        Sun g_sun;
        Weather g_weather;
        float g_lightDir[3] = {};
        bool g_haveLight = false;
        int g_overrideType = -1;
        float g_overrideIntensity = 1.0f;
        float g_temperatureOffset = 0.0f;

        float Wrap01(float v) { return v - std::floor(v); }

        // 0 at the coldest time of day (05:00), 1 at the warmest (15:00); cosine in between, warming
        // over 10 hours and cooling over 14.
        float DailyCurve(float f) {
            constexpr float kMin = 5.0f / 24.0f, kMax = 15.0f / 24.0f, kPi = 3.14159265f;
            const float t = Wrap01(f - kMin);
            const float warm = kMax - kMin;
            return t < warm ? 0.5f - 0.5f * std::cos(kPi * t / warm) : 0.5f + 0.5f * std::cos(kPi * (t - warm) / (1.0f - warm));
        }

        void Load() {
            g_rows.clear();
            g_cache.clear();
            ++g_generation;
            const sxc::data::Table& t = g_table.Table();

            if (!t.Loaded()) {
                g_status = "no AreaClimate.cdbc: built-in climate";
                return;
            }

            const int scopeType = t.Field("ScopeType"), scopeId = t.Field("ScopeID"), flags = t.Field("Flags");
            int columns[kFieldCount];

            for (int f = 0; f < kFieldCount; f++) {
                columns[f] = t.Field(kColumns[f]);
            }

            for (uint32_t i = 0; i < t.RowCount(); i++) {
                const sxc::data::Row r = t.At(i);
                const uint32_t scope = r.Uint(scopeType);
                Raw raw;

                for (int f = 0; f < kFieldCount; f++) {
                    raw.value[f] = r.Float(columns[f]);
                }

                raw.flags = r.Uint(flags);
                g_rows[{ scope, scope == kGlobal ? 0u : r.Uint(scopeId) }] = raw;
            }

            char line[96];
            std::snprintf(line, sizeof(line), "%u row(s)", t.RowCount());
            g_status = line;
        }

        void UpdateBlend(const world::Snapshot& snap);
    }

    const Row& For(uint32_t areaId, int mapId) {
        const auto key = std::make_pair(areaId, mapId);

        if (auto it = g_cache.find(key); it != g_cache.end()) {
            return it->second;
        }

        std::pair<uint32_t, uint32_t> places[world::kMaxAreaChain + 2];
        int n = 0;
        uint32_t chain[world::kMaxAreaChain];
        const int c = world::AreaChain(areaId, chain, world::kMaxAreaChain);

        for (int i = 0; i < c; i++) {
            places[n++] = { kArea, chain[i] };
        }

        if (mapId >= 0) {
            places[n++] = { kMap, static_cast<uint32_t>(mapId) };
        }

        places[n++] = { kGlobal, 0 };

        float value[kFieldCount] = {};
        bool set[kFieldCount] = {};
        uint32_t flags = 0;
        bool flagsSet = false;

        for (int i = 0; i < n; i++) {
            auto it = g_rows.find(places[i]);

            if (it == g_rows.end()) {
                continue;
            }

            if (!flagsSet) {
                flags = it->second.flags;
                flagsSet = true;
            }

            for (int f = 0; f < kFieldCount; f++) {
                if (!set[f] && IsSet(f, it->second.value[f])) {
                    value[f] = it->second.value[f];
                    set[f] = true;
                }
            }
        }

        Row r; // built-in defaults where nothing sets a field
        if (set[kDay]) r.dayTemp = value[kDay];
        if (set[kNight]) r.nightTemp = value[kNight];
        if (set[kAmplitude]) r.seasonAmplitude = value[kAmplitude];
        if (set[kOffset]) r.seasonOffset = value[kOffset];
        if (set[kHumidity]) r.humidity = value[kHumidity] > 1.0f ? 1.0f : value[kHumidity];
        if (set[kFogginess]) r.fogginess = value[kFogginess] > 1.0f ? 1.0f : value[kFogginess];
        if (set[kDayLength]) r.dayLength = value[kDayLength] > 3.5f ? 3.5f : value[kDayLength];
        r.flags = flags;

        if (g_cache.size() > 4096) {
            g_cache.clear();
        }

        return g_cache.emplace(key, r).first->second;
    }

    void Update(const world::Snapshot& snap) {
        if (g_table.Changed()) {
            Load();
        }

        // Date and time from the sky clock: the server's, or the debug override's, with the place's
        // pinned or shifted time -- so a place pinned at night is cold, and a place whose date is
        // pinned (SkyTime PinnedDay) keeps its season.
        Clock c;
        c.valid = snap.inWorld;

        if (snap.inWorld) {
            const sky::clock::Date d = sky::clock::PlaceDate();
            c.minute = d.minute;
            c.hour = d.hour;
            c.day = d.day;
            c.month = d.month;
            c.year = d.year;
            c.yearFraction = d.yearFraction;
            c.dayFraction = static_cast<float>(sky::clock::LightTime());

            if (!(c.dayFraction >= 0.0f && c.dayFraction <= 1.0f)) {
                c.dayFraction = (c.hour * 60 + c.minute) / 1440.0f;
            }
        }

        g_clock = c;
        UpdateBlend(snap);

        // The sun. Daylight from the time of day (0 at 06:00 and 18:00, full from ~08:00 to ~16:00),
        // so the moon (the active light at night) never warms anything.
        constexpr float kPi = 3.14159265f;
        Sun sun;
        const float s = std::sin(2.0f * kPi * (c.dayFraction - 0.25f));
        sun.daylight = s * 3.0f < 0.0f ? 0.0f : (s * 3.0f > 1.0f ? 1.0f : s * 3.0f);

        if (g_haveLight) {
            const float* d = g_lightDir;
            const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);

            if (len > 0.5f && len < 2.0f) {
                const float flip = d[2] < 0.0f ? -1.0f : 1.0f;

                for (int k = 0; k < 3; k++) {
                    sun.dir[k] = flip * d[k] / len;
                }

                sun.fromClient = true;
            }
        }

        if (!sun.fromClient) {
            // East (-y) at 06:00, south (-x) at noon, west (+y) at 18:00; up to ~60 degrees high.
            const float a = 2.0f * kPi * (c.dayFraction - 0.5f);
            const float up = 0.85f * (s > 0.0f ? s : 0.0f);
            const float flat = std::sqrt(1.0f - up * up);
            sun.dir[0] = -std::cos(a) * flat;
            sun.dir[1] = std::sin(a) * flat;
            sun.dir[2] = up;
        }

        g_sun = sun;

        if (g_overrideType >= 0) {
            g_weather.type = g_overrideType;
            g_weather.intensity = g_overrideIntensity;
        } else {
            g_weather.type = snap.weatherType;
            g_weather.intensity = snap.weatherIntensity;
        }
    }

    Breakdown Temperature(const Row& row) {
        return TemperatureAtTime(row, g_clock.dayFraction);
    }

    float WaterTemperature(const Row& row) {
        const Breakdown b = TemperatureAtTime(row, g_clock.dayFraction);
        return 0.5f * (row.dayTemp + row.nightTemp) + b.season + b.test;
    }

    Breakdown TemperatureAtTime(const Row& row, float dayFraction) {
        Breakdown b;
        b.daily = row.nightTemp + (row.dayTemp - row.nightTemp) * DailyCurve(Wrap01(dayFraction));
        // Peak around 20 July (0.55 of the year), shifted by SeasonOffset months.
        constexpr float kPi = 3.14159265f;
        const float y = Wrap01(g_clock.yearFraction - 0.55f - row.seasonOffset / 12.0f);
        b.season = g_clock.valid ? 0.5f * row.seasonAmplitude * std::cos(2.0f * kPi * y) : 0.0f;

        // Weather: rules in code (world-fields.md: rules are code, numbers can move to data).
        switch (g_weather.type) {
            case 1: b.weather = -3.0f * g_weather.intensity; break; // rain
            case 2: b.weather = -6.0f * g_weather.intensity; break; // snow
            case 3: b.weather = 2.0f * g_weather.intensity; break;  // sand
            default: break;
        }

        b.test = g_temperatureOffset;
        b.total = b.daily + b.season + b.weather + b.test;
        return b;
    }

    float TemperatureAt(uint32_t areaId, int mapId) {
        return Temperature(For(areaId, mapId)).total;
    }

    // --- the blend map (At) ---------------------------------------------------------------------------
    namespace {
        // One entry per terrain chunk in a window around the player, world-aligned and toroidal: the
        // chunk's area's row, and that averaged with its 8 neighbours. A slice of chunks is (re)read
        // per frame; a known chunk's blur is redone when it or a neighbour changes.
        constexpr int kBlendSize = 48;          // chunks per side: +-800 yd
        constexpr float kChunk = 100.0f / 3.0f;
        constexpr int kBlendRecentre = 4;       // chunks moved before the window follows
        constexpr int kBlendPerFrame = 96;      // chunk lookups per frame (a full window in ~24 frames)

        struct BlendCell {
            int i = 0x7FFFFFFF, j = 0; // world chunk held
            bool known = false;
            uint32_t area = 0;
            Row raw, blurred;
        };

        std::vector<BlendCell> g_blend(kBlendSize * kBlendSize);
        int g_blendFirstI = 0, g_blendFirstJ = 0, g_blendCursor = 0, g_blendMap = -2;
        bool g_blendPlaced = false;
        uint32_t g_blendGeneration = 0;
        uint32_t g_playerArea = 0;
        float g_playerPos[2] = {};

        int BlendMod(int v) { const int m = v % kBlendSize; return m < 0 ? m + kBlendSize : m; }
        BlendCell& BlendAt(int i, int j) { return g_blend[BlendMod(j) * kBlendSize + BlendMod(i)]; }

        bool InWindow(int i, int j) {
            return i >= g_blendFirstI && i < g_blendFirstI + kBlendSize && j >= g_blendFirstJ && j < g_blendFirstJ + kBlendSize;
        }

        // A chunk's blur: the mean of the known rows among it and its 8 neighbours, flags its own.
        void Blur(int i, int j) {
            BlendCell& c = BlendAt(i, j);

            if (c.i != i || c.j != j || !c.known) {
                return;
            }

            Row sum;
            sum.dayTemp = sum.nightTemp = sum.seasonAmplitude = sum.seasonOffset = sum.humidity = sum.fogginess = sum.dayLength = 0.0f;
            int n = 0;

            for (int dj = -1; dj <= 1; dj++) {
                for (int di = -1; di <= 1; di++) {
                    if (!InWindow(i + di, j + dj)) {
                        continue;
                    }

                    const BlendCell& o = BlendAt(i + di, j + dj);

                    if (o.i != i + di || o.j != j + dj || !o.known) {
                        continue;
                    }

                    sum.dayTemp += o.raw.dayTemp;
                    sum.nightTemp += o.raw.nightTemp;
                    sum.seasonAmplitude += o.raw.seasonAmplitude;
                    sum.seasonOffset += o.raw.seasonOffset;
                    sum.humidity += o.raw.humidity;
                    sum.fogginess += o.raw.fogginess;
                    sum.dayLength += o.raw.dayLength;
                    n++;
                }
            }

            const float inv = 1.0f / n;
            c.blurred.dayTemp = sum.dayTemp * inv;
            c.blurred.nightTemp = sum.nightTemp * inv;
            c.blurred.seasonAmplitude = sum.seasonAmplitude * inv;
            c.blurred.seasonOffset = sum.seasonOffset * inv;
            c.blurred.humidity = sum.humidity * inv;
            c.blurred.fogginess = sum.fogginess * inv;
            c.blurred.dayLength = sum.dayLength * inv;
            c.blurred.flags = c.raw.flags;
        }

        void UpdateBlend(const world::Snapshot& snap) {
            if (!snap.inWorld) {
                return;
            }

            g_playerArea = snap.areaCount > 0 ? snap.areaChain[0] : 0;
            g_playerPos[0] = snap.playerPos[0];
            g_playerPos[1] = snap.playerPos[1];

            if (snap.mapId != g_blendMap || g_generation != g_blendGeneration) {
                for (BlendCell& c : g_blend) {
                    c = BlendCell{};
                }

                g_blendMap = snap.mapId;
                g_blendGeneration = g_generation;
                g_blendPlaced = false;
            }

            const int fi = static_cast<int>(std::floor(snap.playerPos[0] / kChunk)) - kBlendSize / 2;
            const int fj = static_cast<int>(std::floor(snap.playerPos[1] / kChunk)) - kBlendSize / 2;

            if (!g_blendPlaced || std::abs(fi - g_blendFirstI) >= kBlendRecentre || std::abs(fj - g_blendFirstJ) >= kBlendRecentre) {
                g_blendFirstI = fi;
                g_blendFirstJ = fj;
                g_blendPlaced = true;
            }

            for (int n = 0; n < kBlendPerFrame; n++) {
                const int k = g_blendCursor;
                g_blendCursor = (g_blendCursor + 1) % (kBlendSize * kBlendSize);
                const int i = g_blendFirstI + k % kBlendSize, j = g_blendFirstJ + k / kBlendSize;
                BlendCell& c = BlendAt(i, j);
                const bool moved = c.i != i || c.j != j;

                if (!moved && c.known) {
                    continue;
                }

                c.i = i;
                c.j = j;
                c.known = false;
                uint32_t area = 0;

                if (world::ChunkAreaAt((i + 0.5f) * kChunk, (j + 0.5f) * kChunk, area) && area) {
                    c.area = area;
                    c.raw = For(area, snap.mapId);
                    c.known = true;
                }

                for (int dj = -1; dj <= 1; dj++) {
                    for (int di = -1; di <= 1; di++) {
                        if (InWindow(i + di, j + dj)) {
                            Blur(i + di, j + dj);
                        }
                    }
                }
            }
        }

        // A chunk's blended row, or nullptr (outside the window, not read yet, not loaded).
        const Row* Blended(int i, int j) {
            if (!InWindow(i, j)) {
                return nullptr;
            }

            const BlendCell& c = BlendAt(i, j);
            return c.i == i && c.j == j && c.known ? &c.blurred : nullptr;
        }
    }

    Row At(float x, float y) {
        // Between the four nearest chunk centres; chunks not known yet drop out of the mix.
        const float fx = x / kChunk - 0.5f, fy = y / kChunk - 0.5f;
        const int i0 = static_cast<int>(std::floor(fx)), j0 = static_cast<int>(std::floor(fy));
        const float tx = fx - i0, ty = fy - j0;
        const Row* r[4] = { Blended(i0, j0), Blended(i0 + 1, j0), Blended(i0, j0 + 1), Blended(i0 + 1, j0 + 1) };
        const float w[4] = { (1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty };
        Row out;
        out.dayTemp = out.nightTemp = out.seasonAmplitude = out.seasonOffset = out.humidity = out.fogginess = out.dayLength = 0.0f;
        float total = 0.0f, best = -1.0f;

        for (int k = 0; k < 4; k++) {
            if (!r[k]) {
                continue;
            }

            out.dayTemp += w[k] * r[k]->dayTemp;
            out.nightTemp += w[k] * r[k]->nightTemp;
            out.seasonAmplitude += w[k] * r[k]->seasonAmplitude;
            out.seasonOffset += w[k] * r[k]->seasonOffset;
            out.humidity += w[k] * r[k]->humidity;
            out.fogginess += w[k] * r[k]->fogginess;
            out.dayLength += w[k] * r[k]->dayLength;
            total += w[k];

            if (w[k] > best) {
                best = w[k];
                out.flags = r[k]->flags;
            }
        }

        if (total > 1.0e-4f) {
            const float inv = 1.0f / total;
            out.dayTemp *= inv;
            out.nightTemp *= inv;
            out.seasonAmplitude *= inv;
            out.seasonOffset *= inv;
            out.humidity *= inv;
            out.fogginess *= inv;
            out.dayLength *= inv;
            return out;
        }

        // Nothing known around: the area's own row there (else the player's).
        uint32_t area = 0;

        if (!world::ChunkAreaAt(x, y, area) || !area) {
            area = g_playerArea;
        }

        return For(area, g_blendMap);
    }

    int AreaWeightsAt(float x, float y, uint32_t* areas, float* weights, int max) {
        const float fx = x / kChunk - 0.5f, fy = y / kChunk - 0.5f;
        const int i0 = static_cast<int>(std::floor(fx)), j0 = static_cast<int>(std::floor(fy));
        const float tx = fx - i0, ty = fy - j0;
        const float w[4] = { (1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty };
        int count = 0;
        float total = 0.0f;

        for (int c = 0; c < 4; c++) {
            // A corner counts like in At(): only a known chunk, its share spread over the known chunks
            // among it and its 8 neighbours (its blur).
            const int ci = i0 + (c & 1), cj = j0 + (c >> 1);

            if (!Blended(ci, cj)) {
                continue;
            }

            uint32_t around[9];
            int known = 0;

            for (int dj = -1; dj <= 1; dj++) {
                for (int di = -1; di <= 1; di++) {
                    if (!InWindow(ci + di, cj + dj)) {
                        continue;
                    }

                    const BlendCell& o = BlendAt(ci + di, cj + dj);

                    if (o.i == ci + di && o.j == cj + dj && o.known) {
                        around[known++] = o.area;
                    }
                }
            }

            for (int n = 0; n < known; n++) {
                const float share = w[c] / known;
                int at = 0;

                while (at < count && areas[at] != around[n]) {
                    at++;
                }

                if (at == count) {
                    if (count >= max) {
                        continue;
                    }

                    areas[count] = around[n];
                    weights[count] = 0.0f;
                    count++;
                }

                weights[at] += share;
                total += share;
            }
        }

        if (total > 1.0e-6f) {
            for (int n = 0; n < count; n++) {
                weights[n] /= total;
            }
        }

        return total > 1.0e-6f ? count : 0;
    }

    Row Here() {
        return At(g_playerPos[0], g_playerPos[1]);
    }

    const Sun& SunNow() {
        return g_sun;
    }

    void SetLightDirection(const float dir[3]) {
        for (int k = 0; k < 3; k++) {
            g_lightDir[k] = dir[k];
        }

        g_haveLight = true;
    }

    float SunWarming(const float normal[3], float open) {
        const float facing = normal[0] * g_sun.dir[0] + normal[1] * g_sun.dir[1] + normal[2] * g_sun.dir[2];

        if (facing <= 0.0f || g_sun.daylight <= 0.0f || open <= 0.0f) {
            return 0.0f;
        }

        return kSunDegrees * g_sun.daylight * (open > 1.0f ? 1.0f : open) * facing;
    }

    const Clock& Now() { return g_clock; }
    const Weather& CurrentWeather() { return g_weather; }
    const char* Status() { return g_status.c_str(); }
    uint32_t Generation() { return g_generation; }

    void SetWeatherOverride(int type, float intensity) {
        g_overrideType = type;
        g_overrideIntensity = intensity;
    }

    int WeatherOverrideType() { return g_overrideType; }
    float WeatherOverrideIntensity() { return g_overrideIntensity; }
    void SetTemperatureOffset(float degrees) { g_temperatureOffset = degrees; }
    float TemperatureOffset() { return g_temperatureOffset; }
}
