-- /bodyshape opens the panel. Presets move named bones; the probe moves one raw bone (and its mirror
-- partner) so you can find out what the face and helper bones do. Values are local to this client.
local frame = CreateFrame("Frame", "WowDecompBodyShapeFrame", UIParent)
frame:SetSize(660, 600)
frame:SetPoint("CENTER")
frame:SetBackdrop({
    bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
    edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
    tile = true, tileSize = 32, edgeSize = 32,
    insets = { left = 11, right = 12, top = 12, bottom = 11 },
})
frame:SetMovable(true)
frame:EnableMouse(true)
frame:RegisterForDrag("LeftButton")
frame:SetScript("OnDragStart", frame.StartMoving)
frame:SetScript("OnDragStop", frame.StopMovingOrSizing)
frame:SetFrameStrata("DIALOG")
frame:Hide()

local title = frame:CreateFontString(nil, "ARTWORK", "GameFontNormalLarge")
title:SetPoint("TOP", 0, -16)
title:SetText("Body Shape")

local close = CreateFrame("Button", nil, frame, "UIPanelCloseButton")
close:SetPoint("TOPRIGHT", -6, -6)

local function Store()
    WowDecompBodyShapeDB = WowDecompBodyShapeDB or {}
    local key = (GetRealmName() or "?") .. "-" .. (UnitName("player") or "?")
    WowDecompBodyShapeDB[key] = WowDecompBodyShapeDB[key] or { values = {}, probe = {} }
    return WowDecompBodyShapeDB[key]
end

local function MakeSlider(name, parent, x, y, width, minV, maxV, step, label, onChange)
    local s = CreateFrame("Slider", name, parent, "OptionsSliderTemplate")
    s:SetPoint("TOPLEFT", x, y)
    s:SetWidth(width)
    s:SetMinMaxValues(minV, maxV)
    s:SetValueStep(step)
    _G[name .. "Low"]:SetText("")
    _G[name .. "High"]:SetText("")
    s:SetScript("OnValueChanged", function(self, value)
        value = math.floor(value / step + 0.5) * step
        _G[name .. "Text"]:SetText(string.format("%s: %.2f", label, value))
        if not self.refreshing then onChange(value) end
    end)
    function s:Set(value)
        self.refreshing = true
        self:SetValue(value)
        _G[name .. "Text"]:SetText(string.format("%s: %.2f", label, value))
        self.refreshing = false
    end
    return s
end

-- Presets -----------------------------------------------------------------------------------------------
local sliders = {}
local pages = { body = {}, face = {} }
for _, group in ipairs({ "body", "face" }) do
    for i, n in ipairs({ GetBodyShapeNames(group) }) do
        local col, row = (i - 1) % 2, math.floor((i - 1) / 2)
        local lo, hi = GetBodyShapeRange(n)
        local s = MakeSlider("WDBS_" .. n, frame, 28 + col * 170, -92 - row * 38, 150, lo or -1, hi or 1, 0.05, n, function(v)
            SetBodyShape(n, v)
            Store().values[n] = v
        end)
        s:Set(0)
        sliders[n] = s
        pages[group][#pages[group] + 1] = s
    end
end

local function ShowPage(group)
    for name, list in pairs(pages) do
        for _, s in ipairs(list) do
            if name == group then s:Show() else s:Hide() end
        end
    end
end

local bodyTab = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
bodyTab:SetSize(80, 22)
bodyTab:SetPoint("TOPLEFT", 28, -40)
bodyTab:SetText("Body")
bodyTab:SetScript("OnClick", function() ShowPage("body") end)

local faceTab = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
faceTab:SetSize(80, 22)
faceTab:SetPoint("LEFT", bodyTab, "RIGHT", 6, 0)
faceTab:SetText("Face")
faceTab:SetScript("OnClick", function() ShowPage("face") end)

ShowPage("body")

-- Bone probe --------------------------------------------------------------------------------------------
local probeBox = CreateFrame("Frame", nil, frame)
probeBox:SetSize(280, 360)
probeBox:SetPoint("TOPLEFT", 380, -50)

local probeTitle = probeBox:CreateFontString(nil, "ARTWORK", "GameFontNormal")
probeTitle:SetPoint("TOPLEFT", 0, 0)
probeTitle:SetText("Bone probe (faces, helpers)")

local probe = { index = -1, mirror = true, s = { 1, 1, 1 }, m = { 0, 0, 0 } }
local info = probeBox:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
info:SetPoint("TOPLEFT", 0, -70)
info:SetWidth(270)
info:SetJustifyH("LEFT")

local function PushProbe()
    BodyShapeProbe(probe.index, probe.mirror, probe.s[1], probe.s[2], probe.s[3], probe.m[1], probe.m[2], probe.m[3])
    local store = Store().probe
    store.index, store.mirror, store.s, store.m = probe.index, probe.mirror, { unpack(probe.s) }, { unpack(probe.m) }
    if probe.index < 0 then
        info:SetText("Probe off. Pick a bone number.")
        return
    end
    local key, parent, x, y, z = BodyShapeBoneInfo(probe.index)
    if key then
        info:SetText(string.format("bone %d of %d   key %d   parent %d\npivot x %.3f  y %.3f  z %.3f",
            probe.index, BodyShapeBoneInfo() or 0, key, parent, x, y, z))
    else
        info:SetText("bone " .. probe.index .. ": no such bone (model not loaded?)")
    end
end

local box

local function SetIndex(i)
    local count = BodyShapeBoneInfo() or 0
    if count > 0 then i = math.max(-1, math.min(count - 1, i)) end
    probe.index = i
    if box then box:SetText(tostring(i)) end
    PushProbe()
end

local prev = CreateFrame("Button", nil, probeBox, "UIPanelButtonTemplate")
prev:SetSize(40, 22)
prev:SetPoint("TOPLEFT", 0, -22)
prev:SetText("<")
prev:SetScript("OnClick", function() SetIndex(probe.index - 1) end)

local nextB = CreateFrame("Button", nil, probeBox, "UIPanelButtonTemplate")
nextB:SetSize(40, 22)
nextB:SetPoint("LEFT", prev, "RIGHT", 4, 0)
nextB:SetText(">")
nextB:SetScript("OnClick", function() SetIndex(probe.index + 1) end)

box = CreateFrame("EditBox", "WDBS_Index", probeBox, "InputBoxTemplate")
box:SetSize(50, 22)
box:SetPoint("LEFT", nextB, "RIGHT", 14, 0)
box:SetAutoFocus(false)
box:SetScript("OnEnterPressed", function(self)
    SetIndex(tonumber(self:GetText()) or -1)
    self:ClearFocus()
end)

local goLabel = probeBox:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
goLabel:SetPoint("LEFT", box, "RIGHT", 6, 0)
goLabel:SetText("bone # (Enter)")

local mirror = CreateFrame("CheckButton", "WDBS_Mirror", probeBox, "OptionsCheckButtonTemplate")
mirror:SetPoint("TOPLEFT", -4, -44)
mirror:SetChecked(true)
_G["WDBS_MirrorText"]:SetText("Move the mirrored bone too")
mirror:SetScript("OnClick", function(self)
    probe.mirror = self:GetChecked() and true or false
    PushProbe()
end)

local axis = { "X", "Y", "Z" }
local probeSliders = {}
for i = 1, 3 do
    probeSliders["s" .. i] = MakeSlider("WDBS_S" .. i, probeBox, 4, -112 - (i - 1) * 36, 250, 0.3, 2.5, 0.05,
        "Scale " .. axis[i], function(v) probe.s[i] = v PushProbe() end)
    probeSliders["s" .. i]:Set(1)
    probeSliders["m" .. i] = MakeSlider("WDBS_M" .. i, probeBox, 4, -224 - (i - 1) * 36, 250, -0.15, 0.15, 0.005,
        "Move " .. axis[i], function(v) probe.m[i] = v PushProbe() end)
    probeSliders["m" .. i]:Set(0)
end

local regionBox = CreateFrame("EditBox", "WDBS_Region", probeBox, "InputBoxTemplate")
regionBox:SetSize(90, 22)
regionBox:SetPoint("TOPLEFT", 8, -350)
regionBox:SetAutoFocus(false)
regionBox:SetScript("OnEnterPressed", function(self)
    local list = { BodyShapeRegion(self:GetText()) }
    print("Region " .. self:GetText() .. ": " .. (#list > 0 and table.concat(list, ", ") or "none on this model"))
    self:ClearFocus()
end)
local regionLabel = probeBox:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
regionLabel:SetPoint("LEFT", regionBox, "RIGHT", 6, 0)
regionLabel:SetText("region name (Brow, Eyes, Nose...) + Enter")

local hint = probeBox:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
hint:SetPoint("TOPLEFT", 0, -376)
hint:SetWidth(270)
hint:SetJustifyH("LEFT")
hint:SetText("X = forward, Y = left, Z = up (model space). Step through the bones and watch what moves; note the numbers that matter.")

-- Buttons / persistence ---------------------------------------------------------------------------------
local meshInfo = frame:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
meshInfo:SetPoint("BOTTOMLEFT", 160, 24)

local function Refresh()
    local searched, name, count, active = BodyShapeMeshStatus()
    if not searched then
        meshInfo:SetText("Mesh: not checked yet (move any slider)")
    elseif name and name ~= "" then
        meshInfo:SetText(string.format("Mesh: %s, %d vertices%s", name, count, active and ", reshaped" or ""))
    else
        meshInfo:SetText(string.format("Mesh: no profile matched (%d vertices)", count))
    end
    for n, s in pairs(sliders) do
        s:Set(GetBodyShape(n))
        -- Dim sliders this race's skeleton cannot drive (nil = model not loaded yet: leave them usable).
        local ok = GetBodyShapeSupport(n)
        s:SetAlpha(ok == false and 0.35 or 1)
        if ok == false then s:Disable() else s:Enable() end
    end
    for i = 1, 3 do
        probeSliders["s" .. i]:Set(probe.s[i])
        probeSliders["m" .. i]:Set(probe.m[i])
    end
    mirror:SetChecked(probe.mirror)
    box:SetText(tostring(probe.index))
    PushProbe()
end

local reset = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
reset:SetSize(120, 24)
reset:SetPoint("BOTTOMLEFT", 24, 20)
reset:SetText("Reset all")
reset:SetScript("OnClick", function()
    ResetBodyShape()
    probe = { index = -1, mirror = true, s = { 1, 1, 1 }, m = { 0, 0, 0 } }
    local store = Store()
    store.values, store.probe = {}, {}
    Refresh()
end)

local function Load()
    -- Slider values come from the server now (saved per character); only the bone probe is restored here.
    local store = Store()
    local p = store.probe
    if p and p.index then
        probe = { index = p.index, mirror = p.mirror ~= false, s = p.s or { 1, 1, 1 }, m = p.m or { 0, 0, 0 } }
    end
    Refresh()
end

frame:RegisterEvent("PLAYER_ENTERING_WORLD")
frame:SetScript("OnEvent", function() Load() end)
frame:SetScript("OnShow", Refresh)

SLASH_WOWDECOMPBODYSHAPE1 = "/bodyshape"
SLASH_WOWDECOMPBODYSHAPE2 = "/bs"
SlashCmdList["WOWDECOMPBODYSHAPE"] = function()
    if frame:IsShown() then frame:Hide() else frame:Show() end
end
