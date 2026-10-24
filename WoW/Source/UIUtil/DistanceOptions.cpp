#include "Cursor.h"
#include "Engine/Source/FrameScript/FrameScript.h"

// Runs as built-in FrameXML after the signed UI loads, on login and /reload.
// Adds an "Esteria" category to the Video options with the horizon and fog controls. It uses the stock
// panel lifecycle, so Okay, Cancel, Defaults and the category list treat it like any other video panel.
void DistanceInitializeOptions() {
    static const char script[] = R"lua(
if not VideoOptionsFrame or WowDecompEsteriaVideoPanel then return end

local panel = CreateFrame("Frame", "WowDecompEsteriaVideoPanel", VideoOptionsFramePanelContainer)
panel:Hide()
panel.name = "Esteria"
panel.options = {}

local title = panel:CreateFontString(nil, "ARTWORK", "GameFontNormalLarge")
title:SetPoint("TOPLEFT", 16, -16)
title:SetText("Esteria")

local sub = panel:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
sub:SetPoint("TOPLEFT", title, "BOTTOMLEFT", 0, -8)
sub:SetPoint("RIGHT", -32, 0)
sub:SetJustifyH("LEFT")
sub:SetText("Draw distance, horizon and fog. Drag a slider, use the mouse wheel for small steps, or type an exact value.")

local stock = VideoOptionsEffectsPanelViewDistance

local function MakeSlider(name, cvar, y, entry, low, high, format, wheel)
    local slider = CreateFrame("Slider", name, panel, "OptionsSliderTemplate")
    slider:SetSize(300, 17)
    slider:SetPoint("TOPLEFT", 24, y)
    slider.type = CONTROLTYPE_SLIDER
    slider.cvar = cvar
    slider.SetDisplayValue = slider.SetValue
    slider.SetValue = function(self, value)
        self:SetDisplayValue(value)
        BlizzardOptionsPanel_SetCVarSafe(self.cvar, value)
    end
    _G[name .. "Low"]:SetText(low)
    _G[name .. "High"]:SetText(high)

    -- The readout is an edit box, so an exact value can be typed.
    local readout = CreateFrame("EditBox", name .. "Edit", slider, "InputBoxTemplate")
    readout:SetSize(64, 20)
    readout:SetPoint("LEFT", slider, "RIGHT", 16, 0)
    readout:SetAutoFocus(false)
    readout:SetJustifyH("CENTER")
    readout:SetScript("OnEnterPressed", function(self)
        local value = tonumber(self:GetText())
        if value then slider:SetDisplayValue(value) end
        self:ClearFocus()
        self:SetText(format(slider:GetValue()))
    end)
    readout:SetScript("OnEscapePressed", function(self)
        self:ClearFocus()
        self:SetText(format(slider:GetValue()))
    end)

    slider:EnableMouseWheel(true)
    slider:SetScript("OnMouseWheel", function(self, delta)
        self:SetDisplayValue(self:GetValue() + delta * wheel)
    end)
    slider:SetScript("OnValueChanged", function(self, value)
        self.newValue = value
        if not readout:HasFocus() then readout:SetText(format(value)) end
        if cvar == "farclip" and stock and stock.SetDisplayValue then
            stock:SetValueStep(1) -- the stock slider re-snaps itself to tenths of its range on login
            stock:SetDisplayValue(value)
        end
    end)
    panel.options[cvar] = entry
    BlizzardOptionsPanel_RegisterControl(slider, panel)
    return slider
end

local distance = MakeSlider("WowDecompDistanceSlider", "farclip", -96,
    { text = "View Distance", minValue = 177, maxValue = 10000, valueStep = 1,
      tooltip = "How far terrain and objects are drawn, in yards. Mouse wheel nudges by 25; type an exact value in the box." },
    "177", "10000", function(v) return string.format("%d", v + 0.5) end, 25)

MakeSlider("WowDecompHorizonSlider", "horizonFarclipScale", -160,
    { text = "Horizon distance", minValue = 3, maxValue = 12, valueStep = 0.1,
      tooltip = "How far the low detail horizon terrain reaches, as a multiple of view distance. Mouse wheel nudges by 0.5." },
    "3x", "12x", function(v) return string.format("%.1fx", v) end, 0.5)

MakeSlider("WowDecompFogSlider", "fogDistanceScale", -224,
    { text = "Distance fog", minValue = 0, maxValue = 1.5, valueStep = 0.01,
      tooltip = "Where distance fog ends relative to view distance. 0 turns distance fog off; above 100% shows the edge of the world. Mouse wheel nudges by 5%." },
    "Off", "150%", function(v)
        if v <= 0.004 then return "Off" end
        return string.format("%d%%", v * 100 + 0.5)
    end, 0.05)

-- The Effects page keeps its slider (the quality presets drive it) but it is hidden; close the gap it leaves.
if stock then
    stock:Hide()
    VideoOptionsEffectsPanelTerrainDetail:ClearAllPoints()
    VideoOptionsEffectsPanelTerrainDetail:SetPoint("TOPLEFT", VideoOptionsEffectsPanelQuality, "BOTTOMLEFT", 2, -20)
end

panel:SetScript("OnShow", function() distance:SetDisplayValue(tonumber(GetCVar("farclip")) or 350) end)

local note = panel:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
note:SetPoint("TOPLEFT", 24, -274)
note:SetWidth(380)
note:SetJustifyH("LEFT")
note:SetText("Large view distances load much more terrain: expect higher memory use and longer streaming. Fog above 100% shows the far clip edge of the world.")

VideoOptionsPanel_OnLoad(panel)
)lua";
    FrameScript_Execute(script, "NativeDistanceOptions", nullptr);
}
