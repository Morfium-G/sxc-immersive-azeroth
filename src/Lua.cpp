#include "Lua.h"

#include <cmath>
#include <string>
#include "World.h"
#include "climate/Climate.h"
#include "sky/Bodies.h"
#include "sky/Clock.h"
#include "sky/Ephemeris.h"
#include "sky/Season.h"
#include "wxl/services/script.hpp"

namespace ia::lua {
    namespace {
        // Bump when functions are added (addons check it); never change what an existing one returns.
        constexpr int kApiVersion = 1;

        const char* WeatherName(int type) {
            switch (type) {
                case 1: return "rain";
                case 2: return "snow";
                case 3: return "sand";
                default: return "fine";
            }
        }

        // A short string argument, or empty.
        std::string Arg(wxl::ScriptCall& call, uint32_t index) {
            const std::optional<std::string_view> s = call.ArgCount() > index ? call.String(index) : std::nullopt;
            return s && s->size() < 64 ? std::string(*s) : std::string();
        }

        // ImmersiveAzeroth_GetVersion() -> version
        void GetVersion(wxl::ScriptCall& call) {
            call.PushNumber(kApiVersion);
        }

        // ImmersiveAzeroth_GetTemperature() -> ground degC, air degC   (nil, nil outside the world)
        // ground: near the ground where the player stands (the ground fields come later: the air's for now).
        void GetTemperature(wxl::ScriptCall& call) {
            if (!world::Current().inWorld) {
                call.PushNil();
                call.PushNil();
                return;
            }

            const float air = climate::Temperature(climate::Here()).total;
            call.PushNumber(air);
            call.PushNumber(air);
        }

        // ImmersiveAzeroth_GetHumidity() -> 0..1 (the air's, from the place's climate)
        void GetHumidity(wxl::ScriptCall& call) {
            if (!world::Current().inWorld) {
                call.PushNil();
                return;
            }

            call.PushNumber(climate::Here().humidity);
        }

        // ImmersiveAzeroth_GetWeather() -> "fine" | "rain" | "snow" | "sand", intensity 0..1
        void GetWeather(wxl::ScriptCall& call) {
            const climate::Weather& w = climate::CurrentWeather();
            call.PushString(WeatherName(w.type));
            call.PushNumber(w.intensity);
        }

        // ImmersiveAzeroth_GetSkyTime() -> hour, minute, day fraction 0..1, day number, overridden (1/0)
        // The time the sky and the world's light show. GetGameTime() stays the server's time.
        void GetSkyTime(wxl::ScriptCall& call) {
            const double minutes = sky::clock::SkyMinutes();
            const double frac = sky::clock::DayFractionOf(minutes);
            const int m = static_cast<int>(frac * 1440.0) % 1440;
            call.PushNumber(m / 60);
            call.PushNumber(m % 60);
            call.PushNumber(frac);
            call.PushNumber(sky::clock::DayOf(minutes));
            call.PushNumber(sky::clock::Overridden() ? 1 : 0);
        }

        // ImmersiveAzeroth_GetSkyDate() -> year, month, day, day of year (0-based), year fraction 0..1
        void GetSkyDate(wxl::ScriptCall& call) {
            const sky::clock::Date d = sky::clock::PlaceDate();
            call.PushNumber(d.year);
            call.PushNumber(d.month);
            call.PushNumber(d.day);
            call.PushNumber(d.dayOfYear);
            call.PushNumber(d.yearFraction);
        }

        // ImmersiveAzeroth_GetSeason() -> "spring" | "summer" | "autumn" | "winter", phase 0..1
        // (0 midsummer, 0.5 midwinter), warmth -1..1. nil outside the world.
        void GetSeason(wxl::ScriptCall& call) {
            if (!world::Current().inWorld) {
                call.PushNil();
                call.PushNil();
                call.PushNil();
                return;
            }

            const sky::season::Season s = sky::season::Here(sky::clock::PlaceDate().yearFraction);
            call.PushString(s.name);
            call.PushNumber(s.phase);
            call.PushNumber(s.warmth);
        }

        // ImmersiveAzeroth_GetSkyBodies() -> name1, name2, ...
        void GetSkyBodies(wxl::ScriptCall& call) {
            for (int i = 0; i < sky::bodies::BodyCount(); i++) {
                call.PushString(sky::bodies::BodyName(i));
            }
        }

        // ImmersiveAzeroth_GetSkyBody(name) -> azimuth, elevation, visible (1/0), lit 0..1, size
        void GetSkyBody(wxl::ScriptCall& call) {
            const std::string name = Arg(call, 0);
            sky::bodies::Now b;

            if (name.empty() || !sky::bodies::Query(name.c_str(), b)) {
                call.PushNil();
                return;
            }

            call.PushNumber(b.azimuth);
            call.PushNumber(b.elevation);
            call.PushNumber(b.visible ? 1 : 0);
            call.PushNumber(b.lit);
            call.PushNumber(b.sizeDegrees);
        }

        // ImmersiveAzeroth_GetSkyBodyNext(name, "rise" | "set") -> minutes, nil if not within 3 days
        void GetSkyBodyNext(wxl::ScriptCall& call) {
            const std::string name = Arg(call, 0);
            const std::string what = Arg(call, 1);
            const bool rise = _stricmp(what.c_str(), "rise") == 0, set = _stricmp(what.c_str(), "set") == 0;

            if (name.empty() || (!rise && !set)) {
                call.PushNil();
                return;
            }

            const double minutes = sky::bodies::MinutesUntil(name.c_str(), rise);

            if (minutes < 0.0) {
                call.PushNil();
            } else {
                call.PushNumber(minutes);
            }
        }

        // ImmersiveAzeroth_GetSkyBodyCycle(name) -> position 0..1, untilNext, period, "years" | "days", realDays
        void GetSkyBodyCycle(wxl::ScriptCall& call) {
            const std::string name = Arg(call, 0);
            sky::bodies::Cycle c;

            if (name.empty() || !sky::bodies::CycleOf(name.c_str(), c)) {
                call.PushNil();
                return;
            }

            call.PushNumber(c.position);
            call.PushNumber(c.untilNext);
            call.PushNumber(c.period);
            call.PushString(c.lore ? "years" : "days");

            if (c.realDays < 0.0) {
                call.PushNil();
            } else {
                call.PushNumber(c.realDays);
            }
        }

        // ImmersiveAzeroth_GetLoreTime() -> loreTime, year, day, yearsPerDay
        void GetLoreTime(wxl::ScriptCall& call) {
            const double lore = sky::ephemeris::LoreYearsAt(sky::clock::BaseMinutes());
            call.PushNumber(lore);
            call.PushNumber(std::floor(lore));
            call.PushNumber(sky::clock::PlaceDate().dayOfYear + 1);
            call.PushNumber(sky::ephemeris::CurrentLoreClock().yearsPerDay);
        }
    }

    void Register() {
        const wxl::ScriptService script;
        script.RegisterFunction<&GetVersion>("ImmersiveAzeroth_GetVersion");
        script.RegisterFunction<&GetTemperature>("ImmersiveAzeroth_GetTemperature");
        script.RegisterFunction<&GetHumidity>("ImmersiveAzeroth_GetHumidity");
        script.RegisterFunction<&GetWeather>("ImmersiveAzeroth_GetWeather");
        script.RegisterFunction<&GetSkyTime>("ImmersiveAzeroth_GetSkyTime");
        script.RegisterFunction<&GetSkyDate>("ImmersiveAzeroth_GetSkyDate");
        script.RegisterFunction<&GetSeason>("ImmersiveAzeroth_GetSeason");
        script.RegisterFunction<&GetSkyBodies>("ImmersiveAzeroth_GetSkyBodies");
        script.RegisterFunction<&GetSkyBody>("ImmersiveAzeroth_GetSkyBody");
        script.RegisterFunction<&GetSkyBodyNext>("ImmersiveAzeroth_GetSkyBodyNext");
        script.RegisterFunction<&GetSkyBodyCycle>("ImmersiveAzeroth_GetSkyBodyCycle");
        script.RegisterFunction<&GetLoreTime>("ImmersiveAzeroth_GetLoreTime");
    }
}
