-- Esteria Skyriding HUD (built in; Extensions/Skyriding provides the API):
--   GetSkyridingInfo() -> active, airborne, speed, verticalSpeed, airState, charges, maxCharges, nextChargeSeconds,
--                         resourceProfile, thrill, style
--   GetSkyridingDebug(), GetSkyridingCooldown(name), UseSkyridingAbility(name), event SKYRIDING_UPDATE
-- Retail art (docs/SKYRIDING_RETAIL_ASSETS.md) is read from the Retail CDN source as retail\<FileDataID>.blp, so no
-- alias rows are needed. While skyriding, the keys of action buttons 1-5 press the bar's buttons, as on retail.

if not GetSkyridingInfo then return end

EsteriaSkyridingDB = EsteriaSkyridingDB or {}
local db = EsteriaSkyridingDB

local function Retail(fileDataID) return "retail\\" .. fileDataID .. ".blp" end

-- Atlas regions: file, texture width/height, left, top, right, bottom (pixels). The -2x art is drawn at half size.
local ATLAS = {
    vigorBackground = { 4730866, 512, 512, 373, 120, 434, 191 },
    vigorDecor      = { 4730866, 512, 512, 299, 1, 392, 118 },
    vigorFill       = { 4730866, 512, 512, 394, 1, 466, 73 },
    vigorFillFull   = { 4730866, 512, 512, 299, 120, 371, 192 },
    vigorFlash      = { 4730866, 512, 512, 1, 373, 121, 503 },
    vigorFrame      = { 4730866, 512, 512, 123, 373, 243, 503 },
    vigorSpark      = { 4730866, 512, 512, 394, 75, 487, 95 },
    iconFrame       = { 4613342, 256, 1024, 181, 254, 227, 299 },
    iconSlot        = { 4613342, 256, 1024, 181, 136, 245, 198 },
    iconDown        = { 4613342, 256, 1024, 181, 521, 227, 566 },
    iconFlash       = { 4613342, 256, 1024, 181, 568, 227, 613 },
    iconMouseover   = { 4613342, 256, 1024, 181, 643, 227, 688 },
}

-- Sets an atlas region on a texture; mirror flips it horizontally, keep = fraction of its height kept from the bottom.
local function SetAtlas(texture, name, mirror, keep)
    local a = ATLAS[name]
    local w, h = a[2], a[3]
    local left, right, top, bottom = a[4] / w, a[6] / w, a[5] / h, a[7] / h
    if keep then top = bottom - (bottom - top) * keep end
    if mirror then left, right = right, left end
    texture:SetTexture(Retail(a[1]))
    texture:SetTexCoord(left, right, top, bottom)
end

local function AtlasSize(name, scale)
    local a = ATLAS[name]
    return (a[6] - a[4]) * scale, (a[7] - a[5]) * scale
end

local function Show(region, on)
    if on then region:Show() else region:Hide() end
end

-- Slot 2 is Skyward Ascent in the air and Lift Off on the ground, like retail.
local ABILITIES = {
    { key = "SURGE", name = "Surge Forward", icon = 4640490, charge = true },
    { key = "ASCENT", ground = "LIFTOFF", name = "Skyward Ascent", groundName = "Lift Off", icon = 4640498, groundIcon = 4640492, charge = true },
    { key = "WHIRL", name = "Whirling Surge", icon = 4640477 },
    { key = "HALT", name = "Aerial Halt", icon = 5003205 },
    { key = "STYLE", name = "Switch Flight Style", icon = 5145511 },
}

local PIP_SCALE = 0.5
local PIP_STEP = 32
local hud = CreateFrame("Frame", "EsteriaSkyridingHUD", UIParent)
hud:SetSize(300, 110)
hud:SetPoint("BOTTOM", UIParent, "BOTTOM", 0, 140)
hud:Hide()

-- ---- vigor / charges ----------------------------------------------------------------------------------------------

local pipRow = CreateFrame("Frame", nil, hud)
pipRow:SetSize(PIP_STEP * 6, 40)
pipRow:SetPoint("TOP", hud, "TOP", 0, 0)

local decorLeft = pipRow:CreateTexture(nil, "ARTWORK")
local decorRight = pipRow:CreateTexture(nil, "ARTWORK")
SetAtlas(decorLeft, "vigorDecor")
SetAtlas(decorRight, "vigorDecor", true)
decorLeft:SetSize(AtlasSize("vigorDecor", PIP_SCALE))
decorRight:SetSize(AtlasSize("vigorDecor", PIP_SCALE))

local pips = {}
local function Pip(i)
    if pips[i] then return pips[i] end
    local pip = CreateFrame("Frame", nil, pipRow)
    pip:SetSize(AtlasSize("vigorFrame", PIP_SCALE))
    pip.bg = pip:CreateTexture(nil, "BACKGROUND")
    SetAtlas(pip.bg, "vigorBackground")
    pip.bg:SetSize(AtlasSize("vigorBackground", PIP_SCALE))
    pip.bg:SetPoint("CENTER")
    pip.fill = pip:CreateTexture(nil, "BORDER")
    pip.fillW, pip.fillH = AtlasSize("vigorFill", PIP_SCALE)
    pip.fill:SetPoint("BOTTOM", pip, "CENTER", 0, -pip.fillH / 2)
    pip.spark = pip:CreateTexture(nil, "ARTWORK")
    SetAtlas(pip.spark, "vigorSpark")
    pip.spark:SetSize(AtlasSize("vigorSpark", PIP_SCALE * 0.8))
    pip.spark:SetBlendMode("ADD")
    pip.frame = pip:CreateTexture(nil, "OVERLAY")
    SetAtlas(pip.frame, "vigorFrame")
    pip.frame:SetAllPoints()
    pip.flash = pip:CreateTexture(nil, "OVERLAY")
    SetAtlas(pip.flash, "vigorFlash")
    pip.flash:SetAllPoints()
    pip.flash:SetBlendMode("ADD")
    pip.flash:SetAlpha(0)
    pip.flashUntil = 0
    pips[i] = pip
    return pip
end

-- ---- ability bar ---------------------------------------------------------------------------------------------------

local BUTTON = 40
local buttons = {}
for i, ability in ipairs(ABILITIES) do
    local button = CreateFrame("Button", "EsteriaSkyridingButton" .. i, hud)
    button:SetSize(BUTTON, BUTTON)
    button:SetPoint("TOPLEFT", hud, "TOP", (i - 1 - #ABILITIES / 2) * (BUTTON + 6) + 3, -48)
    button.slot = button:CreateTexture(nil, "BACKGROUND")
    SetAtlas(button.slot, "iconSlot")
    button.slot:SetPoint("CENTER")
    button.slot:SetSize(BUTTON * 64 / 46, BUTTON * 62 / 45)
    button.icon = button:CreateTexture(nil, "BORDER")
    button.icon:SetPoint("TOPLEFT", 2, -2)
    button.icon:SetPoint("BOTTOMRIGHT", -2, 2)
    button.icon:SetTexCoord(0.07, 0.93, 0.07, 0.93)
    button.cooldown = CreateFrame("Cooldown", nil, button)
    button.cooldown:SetAllPoints(button.icon)
    button.frame = button:CreateTexture(nil, "OVERLAY")
    SetAtlas(button.frame, "iconFrame")
    button.frame:SetAllPoints()
    button.hotkey = button:CreateFontString(nil, "OVERLAY", "NumberFontNormalSmallGray")
    button.hotkey:SetPoint("TOPRIGHT", -3, -4)
    local highlight = button:CreateTexture(nil, "HIGHLIGHT")
    SetAtlas(highlight, "iconMouseover")
    highlight:SetAllPoints()
    highlight:SetBlendMode("ADD")
    local pushed = button:CreateTexture(nil, "OVERLAY")
    SetAtlas(pushed, "iconDown")
    pushed:SetAllPoints()
    button:SetPushedTexture(pushed)
    button:RegisterForClicks("AnyUp")
    button:SetScript("OnClick", function(self)
        if ability.key == "STYLE" then
            SetCVar("skyridingStyle", GetCVar("skyridingStyle") == "1" and "0" or "1")
        else
            UseSkyridingAbility(self.current or ability.key)
        end
    end)
    button:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_TOP")
        GameTooltip:SetText(self.label or ability.name)
        GameTooltip:Show()
    end)
    button:SetScript("OnLeave", function() GameTooltip:Hide() end)
    button.ability = ability
    buttons[i] = button
end

local speedBar = CreateFrame("StatusBar", nil, hud)
speedBar:SetSize(200, 6)
speedBar:SetPoint("TOP", hud, "TOP", 0, -38)
speedBar:SetStatusBarTexture("Interface\\TargetingFrame\\UI-StatusBar")
speedBar:SetMinMaxValues(0, 80)
speedBar.text = speedBar:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
speedBar.text:SetPoint("LEFT", speedBar, "RIGHT", 6, 0)

local debugText = hud:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
debugText:SetPoint("BOTTOM", hud, "TOP", 0, 4)

-- ---- key overrides -------------------------------------------------------------------------------------------------

local bound = false
local function SetBound(on, force)
    if (on == bound and not force) or InCombatLockdown() then return end
    ClearOverrideBindings(hud)
    if on and db.bindBar ~= false then
        for i = 1, #buttons do
            for _, key in ipairs({ GetBindingKey("ACTIONBUTTON" .. i) }) do
                SetOverrideBindingClick(hud, true, key, buttons[i]:GetName())
            end
        end
    end
    bound = on
end

-- ---- refresh -------------------------------------------------------------------------------------------------------

local lastCharges, period, lastNext = nil, 12, 0
local cooldownShown = {}

local function RefreshPips(charges, maxCharges, nextCharge)
    -- The server only says when the next pip arrives; the period is learned from when that countdown restarts.
    if nextCharge > lastNext + 0.5 then period = math.max(nextCharge, 1) end
    lastNext = nextCharge
    local now = GetTime()
    local width = PIP_STEP * maxCharges
    for i = 1, math.max(maxCharges, #pips) do
        local pip = Pip(i)
        if i > maxCharges then
            pip:Hide()
        else
            pip:Show()
            pip:ClearAllPoints()
            pip:SetPoint("CENTER", pipRow, "LEFT", (pipRow:GetWidth() - width) / 2 + (i - 0.5) * PIP_STEP, 0)
            local fill = i <= charges and 1 or 0
            if i == charges + 1 and nextCharge > 0 then fill = math.min(1, math.max(0.05, 1 - nextCharge / period)) end
            if fill >= 1 then
                SetAtlas(pip.fill, "vigorFillFull")
                pip.fill:SetSize(pip.fillW, pip.fillH)
            elseif fill > 0 then
                SetAtlas(pip.fill, "vigorFill", false, fill)
                pip.fill:SetSize(pip.fillW, pip.fillH * fill)
            end
            Show(pip.fill, fill > 0)
            Show(pip.spark, fill > 0 and fill < 1)
            pip.spark:ClearAllPoints()
            pip.spark:SetPoint("CENTER", pip, "CENTER", 0, -pip.fillH / 2 + pip.fillH * fill)
            if lastCharges and i <= charges and i > lastCharges then pip.flashUntil = now + 0.5 end
            pip.flash:SetAlpha(math.max(0, (pip.flashUntil - now) / 0.5))
        end
    end
    lastCharges = charges
    decorLeft:ClearAllPoints()
    decorRight:ClearAllPoints()
    decorLeft:SetPoint("RIGHT", pipRow, "LEFT", (pipRow:GetWidth() - width) / 2 + 6, 0)
    decorRight:SetPoint("LEFT", pipRow, "LEFT", (pipRow:GetWidth() + width) / 2 - 6, 0)
end

local function RefreshButtons(airborne, charges)
    for i, button in ipairs(buttons) do
        local ability = button.ability
        local onGround = ability.ground and not airborne
        button.current = onGround and ability.ground or ability.key
        button.label = onGround and ability.groundName or ability.name
        button.icon:SetTexture(Retail(onGround and ability.groundIcon or ability.icon))
        local cooldown = ability.key == "STYLE" and 0 or GetSkyridingCooldown(button.current)
        local usable = ability.key == "STYLE" or airborne or onGround
        if ability.charge then usable = usable and charges > 0 end
        button.icon:SetDesaturated(not usable)
        if cooldown > 0 and not cooldownShown[i] then
            button.cooldown:SetCooldown(GetTime(), cooldown)
        elseif cooldown <= 0 and cooldownShown[i] then
            button.cooldown:SetCooldown(0, 0)
        end
        cooldownShown[i] = cooldown > 0
        local key = GetBindingKey("ACTIONBUTTON" .. i)
        button.hotkey:SetText(key and GetBindingText(key, "KEY_", 1) or "")
        button:Show()
    end
end

local function Refresh()
    local active, airborne, speed, verticalSpeed, airState, charges, maxCharges, nextCharge, profile, thrill, style = GetSkyridingInfo()
    SetBound(active)
    if db.hideHud or not (active or (style == "STEADY" and IsMounted())) then
        hud:Hide()
        return
    end
    hud:Show()
    -- Steady flight keeps only the flight-style button, to switch back.
    Show(pipRow, active)
    if not active then
        for i, button in ipairs(buttons) do Show(button, button.ability.key == "STYLE") end
        speedBar:Hide()
        return
    end
    RefreshPips(charges, maxCharges, nextCharge)
    RefreshButtons(airborne, charges)

    Show(speedBar, db.speed)
    speedBar:SetValue(speed)
    if thrill then speedBar:SetStatusBarColor(0.3, 0.85, 1) else speedBar:SetStatusBarColor(0.9, 0.75, 0.2) end
    speedBar.text:SetFormattedText("%d%%", speed / 7 * 100 + 0.5)

    if db.debug then
        local x, y, z, vx, vy, vz, tick, corrections, last, result = GetSkyridingDebug()
        debugText:SetFormattedText("%s %s  v %.1f (vz %.1f)  pos %.1f %.1f %.1f  tick %d  corr %d  last %s/%d",
            airborne and "AIR" or "GROUND", airState, speed, verticalSpeed, x, y, z, tick, corrections, last, result)
    else
        debugText:SetText("")
    end
end

local elapsedSinceRefresh = 0
hud:SetScript("OnUpdate", function(_, elapsed)
    elapsedSinceRefresh = elapsedSinceRefresh + elapsed
    if elapsedSinceRefresh >= 0.03 then
        elapsedSinceRefresh = 0
        Refresh()
    end
end)

-- The HUD's OnUpdate stops while hidden: a small watcher brings it back.
local watcher = CreateFrame("Frame")
watcher:RegisterEvent("SKYRIDING_UPDATE")
watcher:RegisterEvent("PLAYER_REGEN_ENABLED")
watcher:RegisterEvent("UPDATE_BINDINGS")
watcher:SetScript("OnEvent", function(_, event)
    if event == "UPDATE_BINDINGS" then SetBound(bound, true) end
    Refresh()
end)

SLASH_ESTERIASKYRIDING1 = "/skyriding"
SlashCmdList.ESTERIASKYRIDING = function(message)
    message = string.lower(message or "")
    if message == "steady" then SetCVar("skyridingStyle", "1")
    elseif message == "dynamic" then SetCVar("skyridingStyle", "0")
    elseif message == "debug" then db.debug = not db.debug
    elseif message == "speed" then db.speed = not db.speed
    elseif message == "hud" then db.hideHud = not db.hideHud
    elseif message == "keys" then db.bindBar = db.bindBar == false; SetBound(bound, true)
    else
        DEFAULT_CHAT_FRAME:AddMessage("/skyriding steady | dynamic | speed | debug | hud | keys")
    end
    Refresh()
end
