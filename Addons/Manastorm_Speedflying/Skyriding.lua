-- Esteria: the retail Skyriding HUD over speedflying -- vigor pips for the boost stacks and the ability bar.
-- Brought back from the old Esteria skyriding HUD (tag skyriding-final, Addons/EsteriaSkyriding). Retail art is read
-- from the Retail CDN source as retail\<FileDataID>.blp (docs/archive/SKYRIDING_RETAIL_ASSETS.md).
--
-- Reads (written by SpeedFlying.cpp and Hud.lua): Manastorm_MountAnimAdvFly, Manastorm_MountAnimStacks/MaxStacks,
--   Manastorm_MountAnimOrbStart/OrbIv (the recharge clock Hud.lua keeps).
-- Writes: Manastorm_SurgeRequest (button 1; SpeedFlying.cpp spends a stack on a boost, as a SPACE press does).
-- Buttons 2-4 cast the skyriding spells (mod-mana-speedflying spell_speedflying_ability). While on a skyriding
-- mount the keys of action buttons 1-4 press them, as on retail.

local function Retail(fileDataID) return 'retail\\' .. fileDataID .. '.blp' end

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

-- spell = the spell the button casts (nil: Surge Forward is speedflying's boost, not a spell). Slot 2 is Skyward
-- Ascent in the air and Lift Off on the ground, like retail; the macro picks in combat too.
local ABILITIES = {
    { name = 'Surge Forward', icon = 4640490, stack = true },
    { name = 'Skyward Ascent', icon = 4640498, groundName = 'Lift Off', groundIcon = 4640492, stack = true,
      macro = '/cast [flying] Skyward Ascent; Lift Off' },
    { name = 'Whirling Surge', icon = 4640477, spell = 'Whirling Surge', stack = true },
    { name = 'Flap', icon = 4640486, spell = 'Flap' },
}

local PIP_SCALE = 0.5
local PIP_STEP = 42           -- pip centres; the frame art is wider than this, so less makes them touch
-- Where each side decoration sits against the outer pips: negative overlaps, because the art has transparent
-- padding on its inner side. Matched by eye to retail's HUD.
local DECOR_GAP = -16
local DECOR_DROP = 13
local BUTTON = 40

local hud = CreateFrame('Frame', 'EsteriaSkyridingHUD', UIParent, 'SecureHandlerStateTemplate')
hud:SetSize(260, 100)
hud:SetPoint('BOTTOM', UIParent, 'BOTTOM', 0, 140)
hud:Hide()
-- Secure frames cannot be shown or hidden in combat by script, so mounting shows the HUD through a state driver;
-- off a skyriding mount it is only faded out (alpha is allowed) and stops taking the mouse once out of combat.
RegisterStateDriver(hud, 'visibility', '[mounted] show; hide')

-- ---- vigor / charges ----------------------------------------------------------------------------------------------

local pipRow = CreateFrame('Frame', nil, hud)
pipRow:SetSize(PIP_STEP * 6, 40)
pipRow:SetPoint('TOP', hud, 'TOP', 0, 10)

local decorLeft = pipRow:CreateTexture(nil, 'ARTWORK')
local decorRight = pipRow:CreateTexture(nil, 'ARTWORK')
-- The atlas art is the right-hand end piece; the left one is its mirror.
SetAtlas(decorLeft, 'vigorDecor', true)
SetAtlas(decorRight, 'vigorDecor')
decorLeft:SetSize(AtlasSize('vigorDecor', PIP_SCALE))
decorRight:SetSize(AtlasSize('vigorDecor', PIP_SCALE))

local pips = {}
local function Pip(i)
    if pips[i] then return pips[i] end
    local pip = CreateFrame('Frame', nil, pipRow)
    pip:SetSize(AtlasSize('vigorFrame', PIP_SCALE))
    pip.bg = pip:CreateTexture(nil, 'BACKGROUND')
    SetAtlas(pip.bg, 'vigorBackground')
    pip.bg:SetSize(AtlasSize('vigorBackground', PIP_SCALE))
    pip.bg:SetPoint('CENTER')
    pip.fill = pip:CreateTexture(nil, 'BORDER')
    pip.fillW, pip.fillH = AtlasSize('vigorFill', PIP_SCALE)
    pip.fill:SetPoint('BOTTOM', pip, 'CENTER', 0, -pip.fillH / 2)
    pip.spark = pip:CreateTexture(nil, 'ARTWORK')
    SetAtlas(pip.spark, 'vigorSpark')
    pip.spark:SetSize(AtlasSize('vigorSpark', PIP_SCALE * 0.6))
    pip.spark:SetBlendMode('ADD')
    pip.frame = pip:CreateTexture(nil, 'OVERLAY')
    SetAtlas(pip.frame, 'vigorFrame')
    pip.frame:SetAllPoints()
    pip.flash = pip:CreateTexture(nil, 'OVERLAY')
    SetAtlas(pip.flash, 'vigorFlash')
    pip.flash:SetAllPoints()
    pip.flash:SetBlendMode('ADD')
    pip.flash:SetAlpha(0)
    pip.flashUntil = 0
    pips[i] = pip
    return pip
end

-- ---- ability bar ---------------------------------------------------------------------------------------------------

local buttons = {}
for i, ability in ipairs(ABILITIES) do
    local template = (ability.spell or ability.macro) and 'SecureActionButtonTemplate' or nil
    local button = CreateFrame('Button', 'EsteriaSkyridingButton' .. i, hud, template)
    button:SetSize(BUTTON, BUTTON)
    button:SetPoint('TOPLEFT', hud, 'TOP', (i - 1 - #ABILITIES / 2) * (BUTTON + 6) + 3, -44)
    if ability.spell then
        button:SetAttribute('type', 'spell')
        button:SetAttribute('spell', ability.spell)
    elseif ability.macro then
        button:SetAttribute('type', 'macro')
        button:SetAttribute('macrotext', ability.macro)
    else
        button:SetScript('OnClick', function() _G.Manastorm_SurgeRequest = 1 end)
    end
    button.slot = button:CreateTexture(nil, 'BACKGROUND')
    SetAtlas(button.slot, 'iconSlot')
    button.slot:SetPoint('CENTER')
    button.slot:SetSize(BUTTON * 64 / 46, BUTTON * 62 / 45)
    button.icon = button:CreateTexture(nil, 'BORDER')
    button.icon:SetPoint('TOPLEFT', 2, -2)
    button.icon:SetPoint('BOTTOMRIGHT', -2, 2)
    button.icon:SetTexCoord(0.07, 0.93, 0.07, 0.93)
    button.icon:SetTexture(Retail(ability.icon))
    button.cooldown = CreateFrame('Cooldown', nil, button, 'CooldownFrameTemplate')
    button.cooldown:SetAllPoints(button.icon)
    button.frame = button:CreateTexture(nil, 'OVERLAY')
    SetAtlas(button.frame, 'iconFrame')
    button.frame:SetAllPoints()
    button.hotkey = button:CreateFontString(nil, 'OVERLAY', 'NumberFontNormalSmallGray')
    button.hotkey:SetPoint('TOPRIGHT', -3, -4)
    local highlight = button:CreateTexture(nil, 'HIGHLIGHT')
    SetAtlas(highlight, 'iconMouseover')
    highlight:SetAllPoints()
    highlight:SetBlendMode('ADD')
    local pushed = button:CreateTexture(nil, 'OVERLAY')
    SetAtlas(pushed, 'iconDown')
    pushed:SetAllPoints()
    button:SetPushedTexture(pushed)
    button:RegisterForClicks('AnyUp')
    button:SetScript('OnEnter', function(self)
        GameTooltip:SetOwner(self, 'ANCHOR_TOP')
        GameTooltip:SetText(self.label or ability.name)
        GameTooltip:Show()
    end)
    button:SetScript('OnLeave', function() GameTooltip:Hide() end)
    button.ability = ability
    buttons[i] = button
end

-- ---- key overrides: action keys 1-4 press the bar while on a skyriding mount (changeable out of combat only) -------

local bound = false
local function SetBound(on, force)
    if (on == bound and not force) or InCombatLockdown() then return end
    ClearOverrideBindings(hud)
    if on then
        for i = 1, #buttons do
            for _, key in ipairs({ GetBindingKey('ACTIONBUTTON' .. i) }) do
                SetOverrideBindingClick(hud, true, key, buttons[i]:GetName())
            end
        end
    end
    bound = on
end

-- ---- refresh -------------------------------------------------------------------------------------------------------

local lastCharges
local mouseOn = true

local function RefreshPips(charges, maxCharges, progress)
    local now = GetTime()
    local width = PIP_STEP * maxCharges
    for i = 1, math.max(maxCharges, #pips) do
        local pip = Pip(i)
        if i > maxCharges then
            pip:Hide()
        else
            pip:Show()
            pip:ClearAllPoints()
            pip:SetPoint('CENTER', pipRow, 'LEFT', (pipRow:GetWidth() - width) / 2 + (i - 0.5) * PIP_STEP, 0)
            local fill = i <= charges and 1 or 0
            if i == charges + 1 then fill = math.min(1, math.max(0.05, progress)) end
            if fill >= 1 then
                SetAtlas(pip.fill, 'vigorFillFull')
                pip.fill:SetSize(pip.fillW, pip.fillH)
            elseif fill > 0 then
                SetAtlas(pip.fill, 'vigorFill', false, fill)
                pip.fill:SetSize(pip.fillW, pip.fillH * fill)
            end
            Show(pip.fill, fill > 0)
            -- The oval narrows at its ends, where the spark would poke out past the ring.
            Show(pip.spark, fill > 0.15 and fill < 0.85)
            pip.spark:ClearAllPoints()
            pip.spark:SetPoint('CENTER', pip, 'CENTER', 0, -pip.fillH / 2 + pip.fillH * fill)
            if lastCharges and i <= charges and i > lastCharges then pip.flashUntil = now + 0.5 end
            pip.flash:SetAlpha(math.max(0, (pip.flashUntil - now) / 0.5))
        end
    end
    lastCharges = charges
    decorLeft:ClearAllPoints()
    decorRight:ClearAllPoints()
    decorLeft:SetPoint('RIGHT', pipRow, 'LEFT', (pipRow:GetWidth() - width) / 2 - DECOR_GAP, -DECOR_DROP)
    decorRight:SetPoint('LEFT', pipRow, 'LEFT', (pipRow:GetWidth() + width) / 2 + DECOR_GAP, -DECOR_DROP)
end

local function RefreshButtons(flying, charges)
    for i, button in ipairs(buttons) do
        local ability = button.ability
        local onGround = ability.groundName and not flying
        button.label = onGround and ability.groundName or ability.name
        button.icon:SetTexture(Retail(onGround and ability.groundIcon or ability.icon))
        local usable = onGround or flying
        if ability.stack and not onGround then usable = usable and charges > 0 end
        button.icon:SetDesaturated(not usable)
        local spell = ability.spell or (ability.macro and button.label)
        if spell then
            -- Only a real cooldown sweeps; the global cooldown (1.5 s) is not one these spells trigger anyway.
            local start, duration = GetSpellCooldown(spell)
            if start and duration and duration > 1.5 then
                button.cooldown:SetCooldown(start, duration)
            else
                button.cooldown:SetCooldown(0, 0)
            end
        end
        local key = GetBindingKey('ACTIONBUTTON' .. i)
        button.hotkey:SetText(key and GetBindingText(key, 'KEY_', 1) or '')
    end
end

-- Manastorm's orb row is the same stacks; the vigor row replaces it. Its speed bar sits above the vigor row and its
-- Talents button below the ability bar. Hud.lua only re-anchors the bar when HudX/HudY change, so this holds.
local function FitManastormHud()
    local f = _G.Manastorm_MountAnimButton
    if f and f.orbs then
        for _, orb in pairs(f.orbs) do orb:Hide() end
    end
    local bar = _G.Manastorm_MountAnimBar
    if bar then
        bar:ClearAllPoints()
        bar:SetPoint('BOTTOM', hud, 'TOP', 0, 20)
    end
    if f and f.talentBtn then
        f.talentBtn:ClearAllPoints()
        f.talentBtn:SetPoint('TOP', hud, 'BOTTOM', 0, 10)
    end
end

local function Refresh()
    local skyriding = _G.Manastorm_MountAnimAdvFly == 1
    SetBound(skyriding)
    if skyriding then FitManastormHud() end

    hud:SetAlpha(skyriding and 1 or 0)
    if skyriding ~= mouseOn and not InCombatLockdown() then
        mouseOn = skyriding
        for _, button in ipairs(buttons) do button:EnableMouse(skyriding) end
    end
    if not skyriding then return end

    local maxCharges = _G.Manastorm_MountAnimMaxStacks or 3
    local charges = math.max(0, math.min(_G.Manastorm_MountAnimStacks or 0, maxCharges))
    local progress = 0
    local start, iv = _G.Manastorm_MountAnimOrbStart, _G.Manastorm_MountAnimOrbIv
    if charges < maxCharges and start and iv and iv > 0 then progress = (GetTime() - start) / iv end

    RefreshPips(charges, maxCharges, progress)
    RefreshButtons(IsFlying(), charges)
end

local sinceRefresh = 0
hud:SetScript('OnUpdate', function(_, elapsed)
    sinceRefresh = sinceRefresh + elapsed
    if sinceRefresh >= 0.03 then
        sinceRefresh = 0
        Refresh()
    end
end)

-- The HUD's OnUpdate stops while it is hidden (dismounted): this keeps the bindings honest, and releases them
-- after a fight that started on the mount.
local watcher = CreateFrame('Frame')
watcher:RegisterEvent('PLAYER_REGEN_ENABLED')
watcher:RegisterEvent('UPDATE_BINDINGS')
watcher:RegisterEvent('COMPANION_UPDATE')
watcher:SetScript('OnEvent', function(_, event)
    if event == 'UPDATE_BINDINGS' then SetBound(bound, true) end
    if not hud:IsShown() then SetBound(false) else Refresh() end
end)
