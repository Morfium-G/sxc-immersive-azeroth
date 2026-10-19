-- Immersive Azeroth: Climate -- a small line under the minimap clock: temperature, humidity, weather.
-- Hover for the details and the sky's time and season. An example for the ImmersiveAzeroth_* functions
-- the mod gives addons. Without the mod (functions missing) it stays hidden.

local WEATHER = { fine = "", rain = " Rain", snow = " Snow", sand = " Sandstorm" }

local frame = CreateFrame("Frame", "ImmersiveAzerothClimateFrame", Minimap)
frame:SetSize(120, 14)
local text = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
text:SetAllPoints()

local function Place()
    frame:ClearAllPoints()
    if TimeManagerClockButton and TimeManagerClockButton:IsShown() then
        frame:SetPoint("TOP", TimeManagerClockButton, "BOTTOM", 0, 2)
    else
        frame:SetPoint("TOP", Minimap, "BOTTOM", 0, -2)
    end
end

local function Update()
    if not ImmersiveAzeroth_GetTemperature then frame:Hide() return end
    local ground, air = ImmersiveAzeroth_GetTemperature()
    if not air then frame:Hide() return end
    local humidity = ImmersiveAzeroth_GetHumidity() or 0
    local weather, intensity = ImmersiveAzeroth_GetWeather()
    local label = WEATHER[weather] or ""
    if label ~= "" and intensity and intensity < 0.34 then label = " Light" .. label:lower() end
    text:SetFormattedText("%d\194\176C  %d%%%s", math.floor(air + 0.5), math.floor(humidity * 100 + 0.5), label)
    frame:Show()
end

-- Hover: the details.
frame:EnableMouse(true)
frame:SetScript("OnEnter", function(self)
    if not ImmersiveAzeroth_GetTemperature then return end
    local ground, air = ImmersiveAzeroth_GetTemperature()
    GameTooltip:SetOwner(self, "ANCHOR_BOTTOMLEFT")
    GameTooltip:AddLine("Climate")
    if air then GameTooltip:AddDoubleLine("Air", string.format("%.1f\194\176C", air), 1, 1, 1, 1, 1, 1) end
    if ground and air and math.abs(ground - air) >= 0.5 then
        GameTooltip:AddDoubleLine("Ground", string.format("%.1f\194\176C", ground), 1, 1, 1, 1, 1, 1)
    end
    GameTooltip:AddDoubleLine("Humidity", string.format("%d%%", (ImmersiveAzeroth_GetHumidity() or 0) * 100), 1, 1, 1, 1, 1, 1)
    -- API version 2 and later: the ground's moisture, snow and the wind.
    if ImmersiveAzeroth_GetGroundMoisture then
        local moisture, rest = ImmersiveAzeroth_GetGroundMoisture()
        if moisture then
            local wet = moisture - rest
            local word = wet > 0.4 and "soaked" or wet > 0.15 and "wet" or wet > 0.03 and "damp" or "as usual"
            GameTooltip:AddDoubleLine("Soil", string.format("%s (%d%%)", word, moisture * 100), 1, 1, 1, 1, 1, 1)
        end
        local snow = ImmersiveAzeroth_GetField("snow")
        if snow and snow >= 0.01 then
            GameTooltip:AddDoubleLine("Snow", string.format("%d cm", snow * 91.44 + 0.5), 1, 1, 1, 1, 1, 1)
        end
    end
    if ImmersiveAzeroth_GetWind then
        local strength, bearing = ImmersiveAzeroth_GetWind()
        if strength and strength >= 0.05 then
            local DIRS = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" }
            local from = DIRS[math.floor(((bearing + 180) % 360) / 45 + 0.5) % 8 + 1]
            local word = strength > 0.75 and "stormy" or strength > 0.45 and "windy" or strength > 0.2 and "breezy" or "light air"
            GameTooltip:AddDoubleLine("Wind", string.format("%s from the %s", word, from), 1, 1, 1, 1, 1, 1)
        end
    end
    if ImmersiveAzeroth_GetSkyTime then
        local hour, minute = ImmersiveAzeroth_GetSkyTime()
        GameTooltip:AddDoubleLine("Sky time", string.format("%02d:%02d", hour, minute), 1, 1, 1, 1, 1, 1)
    end
    if ImmersiveAzeroth_GetSeason then
        local season = ImmersiveAzeroth_GetSeason()
        if season then GameTooltip:AddDoubleLine("Season", season, 1, 1, 1, 1, 1, 1) end
    end
    GameTooltip:Show()
end)
frame:SetScript("OnLeave", function() GameTooltip:Hide() end)

-- The timer runs on a frame that's never hidden (a hidden frame gets no OnUpdate).
local events = CreateFrame("Frame")
local elapsed = 0
events:SetScript("OnUpdate", function(self, dt)
    elapsed = elapsed + dt
    if elapsed < 1 then return end
    elapsed = 0
    Update()
end)
events:RegisterEvent("PLAYER_ENTERING_WORLD")
events:SetScript("OnEvent", function() Place() Update() end)
Place()
