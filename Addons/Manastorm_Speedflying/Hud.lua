-- Manastorm_Speedflying: the flying HUD (cast bar, glide label, boost orbs, dive label, stack readout).
-- Copyright (C) 2026 WarcraftXL
--
-- Moved out of the module's C++ chunk literals: the frames are UI, and they were being rebuilt on
-- every script context (a world enter) for no reason -- the client keeps them for the process.
-- The module still owns every *value*: it publishes scalars as globals and this file only paints
-- them. Nothing here decides a speed, a state or a rank.
--
-- Reads (all written by src/SpeedFlying.cpp, any may be nil before the first push):
--   Manastorm_MountAnimHudX/Y        where the readout sits (HudX/HudY)
--   Manastorm_MountAnimFrameOn       the readout frame is up (the mount can fly)
--   Manastorm_MountAnimHudOn         the main HUD is up (Hud setting)
--   Manastorm_MountAnimGlideOn       "bonus glide" is effective -- drives the label fade and the bar glow
--   Manastorm_MountAnimState         the state word ('Climbing', 'Diving', ...), '' when words are off
--   Manastorm_MountAnimSpeedPct      live speed percent, '' to leave the debug line blank
--   Manastorm_MountAnimFill          bar fill 0..1, Manastorm_MountAnimDiveFrac the boost tint boundary
--   Manastorm_MountAnimDiving        1 while diving -- fades the big centre callout
--   Manastorm_MountAnimDbgHud        1 while the debug readout is on
--   Manastorm_MountAnimWords         1 when the flight-state words (label, callout) may show
--   Manastorm_MountAnimMaxStacks/Stacks, StackMs/StackDiveMs, OrbAnchorAt/OrbAccum
--   Manastorm_MountAnimAdvFly        1 on an AdvFly mount -- the whole HUD is for those only
-- Writes back: nothing the module needs (it reads Manastorm_Talent* for the panel only).

local driver = CreateFrame('Frame', 'Manastorm_SF_HudDriver')
-- ---------------------------------------------------------------------------------------------
-- The cast bar (the modern casting bar: dark mask, DragonUI flat fill, spark, glow, boost tint)
-- plus the glide label above it and the debug readout below.
-- Built once, then repainted from the globals. The frame is the same texture the module used so
-- nothing about the look changed in the move.
-- ---------------------------------------------------------------------------------------------
local bar = CreateFrame('Frame', 'Manastorm_MountAnimBar', UIParent)
bar:SetWidth(190)
bar:SetHeight(15)
-- Unanchored frames never draw. The driver below moves it to HudX/HudY once the client publishes them.
bar:SetPoint('CENTER', UIParent, 'CENTER', 0, -200)
bar:SetBackdrop({
    bgFile = 'Interface\\Tooltips\\UI-Tooltip-Background',
    edgeFile = 'Interface\\Tooltips\\UI-Tooltip-Border',
    tile = true, tileSize = 12, edgeSize = 12,
    insets = {left = 3, right = 3, top = 3, bottom = 3},
})
bar:SetBackdropColor(0.06, 0.03, 0.02, 0.85)
bar:SetBackdropBorderColor(0.60, 0.32, 0.06, 1)

local mask = bar:CreateTexture(nil, 'BACKGROUND')
mask:SetPoint('TOPLEFT', bar, 'TOPLEFT', 3, -3)
mask:SetPoint('BOTTOMRIGHT', bar, 'BOTTOMRIGHT', -3, 3)
mask:SetTexture('Interface\\castingbar\\uicastingbarfullmask')
mask:SetTexCoord(0.08984375, 0.91015625, 0.125, 0.875)
bar.mask = mask

local fill = bar:CreateTexture(nil, 'ARTWORK')
fill:SetPoint('TOPLEFT', bar, 'TOPLEFT', 3, -3)
fill:SetPoint('BOTTOMLEFT', bar, 'BOTTOMLEFT', 3, 3)
fill:SetTexture('Interface\\speedflying\\CastingBarStandard2')
bar.fill = fill

local spark = bar:CreateTexture(nil, 'OVERLAY')
spark:SetWidth(14) spark:SetHeight(14)
spark:SetTexture('Interface\\CastingBar\\UI-CastingBar-Spark')
spark:SetBlendMode('ADD')
spark:SetVertexColor(1, 0.9, 0.55, 1)
bar.sp = spark

local glow = bar:CreateTexture(nil, 'ARTWORK', nil, 1)
glow:SetAllPoints(bar)
glow:SetTexture('Interface\\speedflying\\uicastingbar2x')
glow:SetTexCoord(0.0009765625, 0.4169921875, 0.2421875, 0.30078125)
glow:SetBlendMode('ADD')
glow:SetVertexColor(1, 0.92, 0.55, 1)
glow:SetAlpha(0)
bar.glow = glow

-- The boost fill is the same texture as the fill, cropped to its own slice past the dive step and
-- tinted warm, so a boost reads as the same bar lit up rather than a flat white block.
local boost = bar:CreateTexture(nil, 'OVERLAY', nil, 1)
boost:SetTexture('Interface\\speedflying\\CastingBarStandard2')
boost:SetPoint('TOPLEFT', bar, 'TOPLEFT', 3, -3)
boost:SetPoint('BOTTOMLEFT', bar, 'BOTTOMLEFT', 3, 3)
boost:SetVertexColor(1, 0.5, 0.15, 1)
boost:SetWidth(0)
bar.boost = boost

local div = bar:CreateTexture(nil, 'OVERLAY', nil, 2)
div:SetTexture('Interface\\Buttons\\WHITE8X8')
div:SetWidth(1) div:SetHeight(9)
div:SetVertexColor(1, 0.95, 0.6, 0.28)
bar.dm = div

bar.w = 184
bar.t = 0;
bar.q = 0;
bar.tq = 0;
bar.a = 1;
bar.empty = 0;
bar.ga = 0;
bar.dmf = 1

-- The bar's own animation: ease the drawn fill toward the target C++ wrote, pulse the glow while
-- gliding, and track the spark. This is the same body the chunk ran every frame -- it lives here
-- so a UI reload no longer has to rebuild it.
bar:SetScript('OnUpdate', function(self, e)
    self.t = self.t + e
    local w = self.w
    local k = e * 6 if k > 1 then k = 1 end
    self.q = (self.q or 0) + ((self.tq or 0) - (self.q or 0)) * k
    local q = self.q
    -- A bar with nothing to show fades out rather than sitting there empty over a mount that has
    -- no business showing it: the empty clock is what drives the fade, not the state.
    if (self.tq or 0) > 0.004 then self.empty = 0 else self.empty = (self.empty or 0) + e end
    local kf = e * 4 if kf > 1 then kf = 1 end
    self.a = (self.a or 1) + ((self.empty > 0.5 and 0 or 1) - (self.a or 1)) * kf
    self:SetAlpha(self.a)
    local fw = w * q if fw < 1 then fw = 1 end
    self.fill:SetWidth(fw)
    self.fill:SetTexCoord(0, q, 0, 1)
    local dm = self.dmf or 1
    if q > dm then
        self.boost:ClearAllPoints()
        self.boost:SetPoint('TOPLEFT', self, 'TOPLEFT', 3 + w * dm, -3)
        self.boost:SetPoint('BOTTOMLEFT', self, 'BOTTOMLEFT', 3 + w * dm, 3)
        self.boost:SetTexCoord(dm, q, 0, 1)
        self.boost:SetWidth(w * (q - dm))
        self.boost:Show()
    else
        self.boost:Hide()
    end
    self.dm:ClearAllPoints()
    self.dm:SetPoint('CENTER', self, 'LEFT', 3 + w * dm, 0)
    self.sp:ClearAllPoints()
    self.sp:SetPoint('CENTER', self, 'LEFT', 3 + w * q, 3 * math.sin(self.t * 7))
    self.sp:SetAlpha(0.5 + 0.5 * math.sin(self.t * 13))
    -- The glow follows the glide label's *eased* alpha (g.a), not its target, so the two move together.
    local gg = _G.Manastorm_MountAnimGlide
    local gt = gg and (gg.ta or 0) or 0
    local kg = e * 4 if kg > 1 then kg = 1 end
    self.ga = (self.ga or 0) + (gt - (self.ga or 0)) * kg
    self.glow:SetAlpha(self.ga * (0.7 + 0.3 * math.sin(self.t * 5)))
end)

-- ---------------------------------------------------------------------------------------------
-- Two frames, as the module had them: the label frame carries the glide label and the DebugHUD
-- lines and is shown/hidden by the mount's capability; the orb frame carries the boost row, the
-- Talent opener and the SPACE override's click target, and is sized to the orb row.
-- ---------------------------------------------------------------------------------------------
local frame = CreateFrame('Frame', 'Manastorm_MountAnimFrame', UIParent)
frame:Hide()

-- The "Gliding" label. Its alpha is eased here, and the bar's glow reads that eased value (g.a), so
-- the words and the glow move together -- and hiding the words leaves the bar itself untouched.
local g = frame:CreateFontString(nil, 'OVERLAY', 'GameFontNormalHuge')
g:SetTextColor(1, 0.85, 0.1, 1)
g:SetHeight(28)
g:SetPoint('BOTTOM', bar, 'TOP', 0, 6)
g:SetText('Gliding')
g.a = 0
g:SetAlpha(0)
_G.Manastorm_MountAnimGlide = g

local t = frame:CreateFontString(nil, 'OVERLAY', 'GameFontNormalHuge')
t:SetTextColor(1, 0.85, 0.1, 1)
_G.Manastorm_MountAnimText = t

-- The stack line is DebugHUD's: the orb row already draws the count as a fill, so this adds the
-- number and the recovery clock. Its text is written by the orb frame's OnUpdate, where the clock is.
local st = frame:CreateFontString(nil, 'OVERLAY', 'GameFontNormalLarge')
st:SetTextColor(0.85, 0.9, 1, 0.9)
_G.Manastorm_MountAnimStacksText = st

local spd = frame:CreateFontString(nil, 'OVERLAY', 'GameFontNormalLarge')
spd:SetTextColor(1, 1, 1, 0.9)
_G.Manastorm_MountAnimSpeed = spd

-- The label frame only eases its own label; everything that must keep running while the frame is
-- hidden lives on the driver below (an OnUpdate on a hidden frame stops firing).
frame:SetScript('OnUpdate', function(_, e)
    local k = e * 5 if k > 1 then k = 1 end
    g.a = (g.a or 0) + ((g.ta or 0) - (g.a or 0)) * k
    if (g.ta or 0) == 0 and g.a < 0.01 then g.a = 0 end
    -- The words are FlightStateText's; the glow keeps following g.a whatever the words are doing.
    g:SetAlpha(_G.Manastorm_MountAnimWords == 1 and g.a or 0)
end)


-- The orb frame: boost row, Talent opener, and the SPACE override's click target. It follows the
-- cast bar and is sized to the orb row, so it is the anchor the debug lines hang from.
local f = CreateFrame('Frame', 'Manastorm_MountAnimButton', UIParent)
f:SetWidth(110)
f:SetHeight(54)
f:EnableMouse(false)
f:Hide()

-- The talent panel's opener, under the orb row. The frame cannot call into C++, so the click raises
-- the global the module polls (TalentUI.cpp PollButtonClick consumes it).
local talentBtn = CreateFrame('Button', nil, f)
talentBtn:SetWidth(110)
talentBtn:SetHeight(16)
talentBtn:SetPoint('TOP', f, 'BOTTOM', 0, -5)
talentBtn:SetBackdrop({
    bgFile = 'Interface\\Tooltips\\UI-Tooltip-Background',
    edgeFile = 'Interface\\Tooltips\\UI-Tooltip-Border',
    tile = true, tileSize = 8, edgeSize = 8,
    insets = {left = 2, right = 2, top = 2, bottom = 2},
})
talentBtn:SetBackdropColor(0.10, 0.10, 0.13, 0.85)
talentBtn:SetBackdropBorderColor(0.6, 0.45, 0.15, 1)
talentBtn:SetNormalFontObject('GameFontNormalSmall')
talentBtn:SetText('Talents')
talentBtn:SetScript('OnClick', function() _G.Manastorm_TalentToggle = 1 end)
f.talentBtn = talentBtn

-- A 1x1 invisible button that exists only as the SPACE override's click target: the base ascend is
-- swallowed while a stack is held and the boost itself is driven from C++ (UpdateSpaceInput), so
-- nothing on this button does anything by itself.
local icon = CreateFrame('Button', 'Manastorm_MountAnimIcon', f)
icon:SetWidth(1) icon:SetHeight(1)
icon:SetPoint('CENTER', f, 'CENTER')
icon:SetAlpha(0)
f.icon = icon

-- Read-only boost slots: empty is monknopower, held is monklightpower, and the slot charging the
-- next stack pulses a glow ring plus a swirl that spins while the stack is held.
f.orbs = {}
f.adorb = function(self, i)
    local s = CreateFrame('Frame', nil, self)
    s:SetWidth(34) s:SetHeight(34)
    if i == 1 then s:SetPoint('TOPLEFT', self, 'TOPLEFT', 0, 0)
    else s:SetPoint('LEFT', self.orbs[i - 1], 'RIGHT', 4, 0) end

    local ti = s:CreateTexture(nil, 'ARTWORK')
    ti:SetAllPoints(s)
    ti:SetTexture('Interface\\speedflying\\monknopower.blp')
    s.icon = ti

    local act = s:CreateTexture(nil, 'ARTWORK', nil, 1)
    act:SetPoint('CENTER', s, 'CENTER', -1, 1)
    act:SetTexture('Interface\\speedflying\\11fx_arcaneorb01_6310603.blp')
    act:SetBlendMode('ADD')
    act:SetAlpha(0)
    s.act = act

    local ring = s:CreateTexture(nil, 'OVERLAY')
    ring:SetPoint('CENTER', s, 'CENTER', -1, 1)
    ring:SetWidth(26) ring:SetHeight(26)
    ring:SetTexture('Interface\\speedflying\\t_vfx_hero_circle_a.blp')
    ring:SetBlendMode('ADD')
    ring:SetVertexColor(1, 0.92, 0.55, 1)
    ring:SetAlpha(0)
    s.ring = ring

    local w = s:CreateTexture(nil, 'OVERLAY', nil, 2)
    w:SetPoint('CENTER', s, 'CENTER', -1, 1)
    w:SetTexture('Interface\\speedflying\\8fx_speckledwarp_swirl_blend.blp')
    w:SetBlendMode('ADD')
    w:SetWidth(42) w:SetHeight(42)
    w:SetAlpha(0)
    s.swirl = w

    local w2 = s:CreateTexture(nil, 'OVERLAY', nil, 2)
    w2:SetPoint('CENTER', s, 'CENTER', -1, 1)
    w2:SetTexture('Interface\\speedflying\\10pm_primalist_swirlyblob03_4572177.blp')
    w2:SetBlendMode('ADD')
    w2:SetWidth(42) w2:SetHeight(42)
    w2:SetAlpha(0)
    s.swirl2 = w2

    local w3 = s:CreateTexture(nil, 'OVERLAY', nil, 2)
    w3:SetPoint('CENTER', s, 'CENTER', -1, 1)
    w3:SetTexture('Interface\\speedflying\\8fx_speckledwarp_swirl_blend.blp')
    w3:SetBlendMode('ADD')
    w3:SetWidth(48) w3:SetHeight(48)
    w3:SetAlpha(0)
    s.swirl3 = w3

    self.orbs[i] = s
end

-- A slot is built the first time the row needs it: the ceiling is not known when the frame is made.
-- Twin Vault raises MaxStacks to 4 at runtime, and a hardcoded 1..3 row had no orb for the 4th -- it
-- was held server-side and simply never drawn.
f.fitOrbs = function(self, mx)
    for i = 1, mx do if not self.orbs[i] then self.adorb(self, i) end end
    for i = mx + 1, #self.orbs do if self.orbs[i] then self.orbs[i]:Hide() end end
    local w = 34 * mx + 4 * (mx - 1)
    self:SetWidth(w)
    if self.talentBtn then self.talentBtn:SetWidth(w) end
end

-- The big centre callout. The module used to fade it with UIFrameFadeIn/Out across the boundary;
-- it is a UI animation, so it eases in the addon against the Diving flag instead.
local dive = UIParent:CreateFontString('Manastorm_MountAnimDive', 'OVERLAY', 'GameFontNormalHuge')
dive:SetTextColor(1, 0.85, 0.1, 1)
dive:SetText('Diving')
dive:SetAlpha(0)
dive:SetPoint('CENTER', UIParent, 'CENTER', 0, -140)
dive.ta = 0
_G.Manastorm_MountAnimDive = dive


-- The orb row's per-frame body: follow the bar, fit the row to the ceiling, advance the stack clock
-- the server anchors, then paint every orb. The stack clock is the only state kept here -- the count
-- and its cadence come from the module.
f:SetScript('OnUpdate', function(self, e)
    self:ClearAllPoints()
    if bar then self:SetPoint('TOP', bar, 'BOTTOM', 0, -6)
    else self:SetPoint('CENTER', UIParent, 'CENTER', _G.Manastorm_MountAnimHudX or 0, _G.Manastorm_MountAnimHudY or 0) end

    local mx = _G.Manastorm_MountAnimMaxStacks or 3
    local n = _G.Manastorm_MountAnimStacks or 0
    if n > mx then n = mx end
    if n < 0 then n = 0 end
    if self.mx ~= mx then self.mx = mx self.fitOrbs(self, mx) end

    local now = GetTime()
    -- The server sends the ms accrued toward the next stack; without that (an older server) the clock
    -- is local: a granted stack restarts it, a spent one keeps it, and a spend from a full stock starts
    -- fresh because a capped timer accrues nothing.
    local iv = (_G.Manastorm_MountAnimDiveNow == 1) and ((_G.Manastorm_MountAnimStackDiveMs or 3000) / 1000)
                                             or ((_G.Manastorm_MountAnimStackMs or 9000) / 1000)
    if iv <= 0 then iv = 9 end

    local at = _G.Manastorm_MountAnimOrbAnchorAt
    if at and at ~= _G.Manastorm_MountAnimOrbSeen then
        _G.Manastorm_MountAnimOrbSeen = at
        _G.Manastorm_MountAnimOrbStart = at - (_G.Manastorm_MountAnimOrbAccum or 0) / 1000
        _G.Manastorm_MountAnimOrbLast = n
        _G.Manastorm_MountAnimOrbIv = iv
    elseif _G.Manastorm_MountAnimOrbLast ~= n then
        local prev = _G.Manastorm_MountAnimOrbLast
        _G.Manastorm_MountAnimOrbLast = n
        if prev == nil or n > prev or prev >= mx then _G.Manastorm_MountAnimOrbStart = now end
    end
    -- A glide/dive cadence switch keeps the fraction of progress already made.
    if _G.Manastorm_MountAnimOrbIv and _G.Manastorm_MountAnimOrbIv ~= iv and _G.Manastorm_MountAnimOrbStart then
        local q = (now - _G.Manastorm_MountAnimOrbStart) / _G.Manastorm_MountAnimOrbIv
        if q > 1 then q = 1 end
        _G.Manastorm_MountAnimOrbStart = now - q * iv
    end
    _G.Manastorm_MountAnimOrbIv = iv

    local p = 0
    if n < mx then
        local t0 = _G.Manastorm_MountAnimOrbStart or now
        p = (now - t0) / iv
        if p > 1 then p = 1 end
    end

    -- Charging ring pulses; the rate stays slow through most of the fill and turns up hard near the
    -- end, so the phase is integrated (a changing rate stays continuous instead of jumping).
    self.ph = (self.ph or 0) + e * (3 + 20 * p * p * p)
    local pulse = 0.5 + 0.5 * math.sin(self.ph)

    if st and _G.Manastorm_MountAnimDbgHud == 1 then
        if n >= mx then st:SetText('Stacks: ' .. n .. '/' .. mx .. '  (full)')
        else st:SetText(string.format('Stacks: %d/%d  next in %.1fs', n, mx, (1 - p) * iv)) end
    end

    for i = 1, mx do
        local s = self.orbs[i]
        if s then
            s.ring:SetAlpha(0)
            local held = (i <= n)
            local charging = (not held) and n < mx and i == n + 1
            -- A newly held stack fades its filled texture in over 0.1 s instead of snapping on.
            if held and not s.h then s.fade = s.c and 1 or 0 end
            s.h = held
            s.c = charging
            s.fade = (s.fade or 1) + e / 0.1
            if s.fade > 1 then s.fade = 1 end
            if held then s.icon:SetTexture('Interface\\speedflying\\monklightpower.blp')
            else s.icon:SetTexture('Interface\\speedflying\\monknopower.blp') end
            -- The available-stack texture grows linearly with the fill: a cubic curve left it a ~5px
            -- dot until the stack was almost ready.
            local gr = charging and (0.25 + 0.75 * p) or 1
            s.act:SetAlpha(charging and 1 or 0)
            s.act:SetWidth(41 * gr)
            s.act:SetHeight(41 * gr)
            if charging then s.ring:SetAlpha(0.12 + 0.88 * pulse) end
            if held then s.icon:SetAlpha(s.fade) else s.icon:SetAlpha(1) end
            if held then
                s.swirl:SetAlpha(1)  s.swirl:SetRotation(math.fmod(now * 1.6, 6.2831853))
                s.swirl2:SetAlpha(1) s.swirl2:SetRotation(math.fmod(-now * 1.1, 6.2831853))
                s.swirl3:SetAlpha(1) s.swirl3:SetRotation(math.fmod(now * 0.4, 6.2831853))
            else
                s.swirl:SetAlpha(0) s.swirl2:SetAlpha(0) s.swirl3:SetAlpha(0)
            end
        end
    end
end)

-- The driver owns everything that must keep ticking while the frames above are hidden: the readout
-- anchor, the capability show/hide, the dive callout's fade and the two values the label frame writes
-- into the bar. An OnUpdate on a hidden frame stops firing, so none of this can live on `frame`.
local hud = CreateFrame('Frame', 'Manastorm_SF_HudDriver2')
local barX, barY
hud:SetScript('OnUpdate', function()
    -- HudX/HudY are pixels right/down of UIParent's middle (HudY is negative for below).
    local x, y = _G.Manastorm_MountAnimHudX or 0, _G.Manastorm_MountAnimHudY or -200
    if x ~= barX or y ~= barY then
        barX, barY = x, y
        bar:ClearAllPoints()
        bar:SetPoint('CENTER', UIParent, 'CENTER', x, y)
    end

    local hb = _G.Manastorm_MountAnimButton
    local tb = (hb and hb.talentBtn) or UIParent
    t:ClearAllPoints()
    t:SetPoint('TOPLEFT', tb, 'BOTTOMLEFT', 60, -20)
    st:ClearAllPoints()
    st:SetPoint('TOPLEFT', t, 'BOTTOMLEFT', 0, -4)
    spd:ClearAllPoints()
    spd:SetPoint('TOPLEFT', st, 'BOTTOMLEFT', 0, -4)

    -- The readout frame is shown/hidden as one: the label, the state text and the speed line are its
    -- children, and a plain bar hide used to leave them on screen over an incompatible mount.
    if _G.Manastorm_MountAnimFrameOn == 1 then frame:Show() else frame:Hide() end
    if _G.Manastorm_MountAnimHudOn == 1 then g:Show() else g:Hide() end
    g.ta = (_G.Manastorm_MountAnimGlideOn == 1) and 1 or 0
    if _G.Manastorm_MountAnimDbgHud == 1 then st:Show() else st:Hide() end
    t:SetText(_G.Manastorm_MountAnimState or '')
    spd:SetText(_G.Manastorm_MountAnimSpeedPct or '')

    -- The bar's targets. Written here rather than when they change, because the bar's own OnUpdate
    -- reads them every frame either way.
    bar.tq = _G.Manastorm_MountAnimFill or 0
    bar.dmf = _G.Manastorm_MountAnimDiveFrac or 1

    -- The orb frame follows the mount's capability. showState mirrors the real shown/hidden so a
    -- Show/Hide only runs on a change.
    local adv = _G.Manastorm_MountAnimAdvFly == 1
    if f:IsShown() ~= adv then
        if adv then f:Show() else f:Hide() end
    end

    -- The dive callout eases toward the flag rather than calling UIFrameFadeIn/Out across the
    -- boundary. UIFrameFadeIn is used when the flag rises, so the fade still works if this frame is
    -- ever stopped; the easing below is what makes the out-fade smooth.
    local on = _G.Manastorm_MountAnimDiving == 1
    dive.ta = on and 1 or 0
    local a = (dive.a or 0) + (dive.ta - (dive.a or 0)) * 0.15
    if dive.ta == 0 and a < 0.01 then a = 0 end
    dive.a = a
    dive:SetAlpha(a)
end)

-- The SPACE->boost override. The module decides *when* it may change (never mid-press, see
-- UpdateSpaceBinding); this applies it. The override points at the 1x1 icon button, whose own click
-- is irrelevant -- C++ polls the OS key, that is what spends the stack.
local spaceApplied = false
local space = CreateFrame('Frame', 'Manastorm_SF_SpaceDriver')
space:SetScript('OnUpdate', function()
    local want = _G.Manastorm_MountAnimSpaceBound == 1
    if want == spaceApplied then return end
    spaceApplied = want
    if want then
        SetOverrideBindingClick(f, 1, 'SPACE', 'Manastorm_MountAnimIcon')
    else
        ClearOverrideBindings(f)
    end
end)
