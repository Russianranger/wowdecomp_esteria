#include "Cursor.h"
#include "Engine/Source/FrameScript/FrameScript.h"

// Runs as built-in FrameXML after the signed UI loads, on login and /reload.
// Uses the stock panel control lifecycle for Okay, Cancel, and Defaults.
void CursorInitializeOptions() {
    static const char script[] = R"lua(
local panel = InterfaceOptionsMousePanel
if not panel or WowDecompCursorThemeDropDown then return end

local choices = {
    { "Human", "Human (Default)" },
    { "NightElf", "Night Elf" },
    { "Orc", "Orc" },
    { "Undead", "Undead" },
    { "Hearthstone", "Hearthstone" },
    { "Starcraft", "Starcraft (Animated)" },
}
local dropdown = CreateFrame("Frame", "WowDecompCursorThemeDropDown", panel, "UIDropDownMenuTemplate")
dropdown:SetPoint("TOPLEFT", InterfaceOptionsMousePanelWoWMouse, "BOTTOMLEFT", -13, -32)
UIDropDownMenu_SetWidth(dropdown, 180)

local label = dropdown:CreateFontString(nil, "ARTWORK", "GameFontHighlight")
label:SetPoint("BOTTOMLEFT", dropdown, "TOPLEFT", 18, 3)
label:SetText("Cursor appearance")

local note = panel:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
note:SetPoint("TOPLEFT", dropdown, "BOTTOMLEFT", 18, -8)
note:SetText("Applies immediately.")

local function Display(value)
    UIDropDownMenu_SetSelectedValue(dropdown, value)
    for _, choice in ipairs(choices) do
        if string.lower(choice[1]) == string.lower(value) then
            UIDropDownMenu_SetText(dropdown, choice[2])
            return
        end
    end
end

dropdown.type = CONTROLTYPE_DROPDOWN
dropdown.cvar = "cursorTheme"
dropdown.defaultValue = GetCVarDefault("cursorTheme")
dropdown.GetValue = function(self) return GetCVar(self.cvar) end
dropdown.SetValue = function(self, value)
    SetCVar(self.cvar, value)
    self.value = self:GetValue()
    Display(self.value)
end
dropdown.RefreshValue = function(self)
    self.value = self:GetValue()
    self.newValue = nil
    Display(self.value)
end
UIDropDownMenu_Initialize(dropdown, function(self, level)
    for _, choice in ipairs(choices) do
        local info = UIDropDownMenu_CreateInfo()
        info.text = choice[2]
        info.value = choice[1]
        info.checked = string.lower(GetCVar("cursorTheme")) == string.lower(choice[1])
        info.func = function(button)
            SetCVar("cursorTheme", button.value)
            local actual = GetCVar("cursorTheme")
            dropdown.newValue = actual ~= dropdown.value and actual or nil
            Display(actual)
        end
        UIDropDownMenu_AddButton(info, level)
    end
end)
dropdown:RefreshValue()
BlizzardOptionsPanel_RegisterControl(dropdown, panel)
)lua";
    FrameScript_Execute(script, "NativeCursorOptions", nullptr);
}
