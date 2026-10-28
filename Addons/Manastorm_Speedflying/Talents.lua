-- Manastorm_Speedflying: the speedflying talent panel.
-- Copyright (C) 2026 WarcraftXL
--
-- This program is free software: you can redistribute it and/or modify it under the terms of
-- the GNU General Public License as published by the Free Software Foundation, either version
-- 3 of the License, or (at your option) any later version.
--
-- The tree is a circle: Windborn (the core) at the centre, three trees (Dive, Glide, Wings) of five rings
-- around it, the three big talents on ring 6 (only one may hold a rank) and each one's linked talent beyond.
-- Ring radius and each talent's angle come from the table (RAD, row field 12). Links are dots, not lines.
-- The panel is dressed like the Manastorm transmog window and built from the same art
-- (Manastorm_Transmog/Textures): the window chrome, the framed brown panel, the list/glue buttons and the
-- node frames, all at the transmog window's scale (U, WIN_K, SLOT_SCALE).
-- The client (WoW/Source/Ui/TalentUI.cpp) owns the wire, the rules and the staging queue; it publishes
-- the authoritative state as globals this addon reads back every frame, and reads the globals this
-- addon raises:
--   _G.Manastorm_TalentD       the talent table, pushed once per world enter. Row fields: 1 id, 2 tree (1 dive,
--                        2 glide, 3 wings, 0 none), 3 ring, 4 max rank, 5 client-side flag, 6 name,
--                        7 description, 8 number per rank, 9 its unit, 10 tree a bridge opens, 11 kind
--                        (0 core, 1 node, 2 bridge, 3 big talent, 4 linked), 12 angle, 13-15 parent ids
--                        (any one at full rank opens the talent), 16-17 the two trees a bridge joins,
--                        18-19 a bridge's two reach talents (ids)
--   _G.Manastorm_TalentPoints / Spent / Unspent / Max / Pending / Partial / Need / Status
--   _G.Manastorm_TalentRanks   confirmed+staged rank per talent, in Manastorm_TalentD order
--   _G.Manastorm_TalentCan     whether the client would accept a click, same order
--   _G.Manastorm_TalentPick / Confirm / Reset   raised by this addon, polled by the client
--   _G.Manastorm_SF_Shown      written here, read by the client to pace its re-requests
local TEX = 'Interface\\AddOns\\Manastorm_Transmog\\Textures\\'
local ATLAS, ATLAS_SIZE = TEX .. 'uitransmogrify2x', 2048
local LIST_ATLAS = TEX .. 'listbuttons'
local LIST = { dark = { 0, 0, 395, 48 }, light = { 2, 50, 391, 44 }, selected = { 0, 144, 396, 47 } }
local GLUE_DIR = TEX .. 'glue-panel-button-'
local GLUE = { 5, 3, 138, 40 }
local TABS_PATH = TEX .. 'uiframetabs'
local TAB_ART = {
    on  = { left = { 2, 126, 35, 41 }, mid = { 8, 0, 48, 41 },  right = { 0, 82, 35, 41 } },
    off = { left = { 3, 208, 34, 35 }, mid = { 8, 44, 48, 35 }, right = { 0, 170, 36, 35 } },
}
local WHITE = 'Interface\\Buttons\\WHITE8X8'
-- rectangles of the transmog atlas: x, y, w, h
local RECT = {
    frame = { 11, 1636, 322, 322 },   -- the framed panel of the item list
    slotBronze = { 625, 1923, 110, 110 }, slotDark = { 855, 1923, 110, 110 },
    slotPurple = { 1621, 1129, 110, 110 }, slotSelected = { 1490, 1129, 120, 119 },
}

-- Same metrics as the transmog window: the window is drawn at WIN_K of the art's own pixel size, slots
-- smaller still. U is UI units per layout pixel; it is fixed when the panel is built.
local WIN_K = 0.7
local SLOT_SCALE = 0.53 / WIN_K
local ICON_FILL = 0.82
local BUTTON_EXTRA = 4
local U = 1
local SPX = 1   -- one screen pixel in UI units: the talent art is drawn texel for pixel
local function P(n) return n * U end
local function SP(n) return n * U * SLOT_SCALE end
local function PX() return 2 * U / WIN_K end       -- two screen pixels
local function IconSize() return SP(110) * ICON_FILL + PX() end

local function ScreenPixels()
    local w, h = (GetCVar('gxResolution') or ''):match('(%d+)x(%d+)')
    w, h = tonumber(w), tonumber(h)
    if not w or not h then return 1920, 1080 end
    return w, h
end

-- Draws an atlas piece at its own size (times k).
local function Art(tex, key, k)
    local r = RECT[key]
    tex:SetTexture(ATLAS)
    tex:SetTexCoord(r[1] / ATLAS_SIZE, (r[1] + r[3]) / ATLAS_SIZE, r[2] / ATLAS_SIZE, (r[2] + r[4]) / ATLAS_SIZE)
    tex:SetSize(P(r[3]) * (k or 1), P(r[4]) * (k or 1))
end

-- Nine-slice of a rectangle of a texture: corners keep their shape and are drawn `draw` layout pixels
-- square; edges and middle stretch. Same routine as the transmog window's; Set(path) swaps the art.
local function NineSlice(frame, layer, path, aw, ah, rect, corner, draw)
    local x, y, w, h, c = rect[1], rect[2], rect[3], rect[4], corner
    local d = P(draw)
    local parts = {}
    local function new(px, py, pw, ph)
        local t = frame:CreateTexture(nil, layer)
        t:SetTexture(path)
        local co = { px / aw, (px + pw) / aw, py / ah, (py + ph) / ah }
        t:SetTexCoord(co[1], co[2], co[3], co[4])
        parts[#parts + 1] = { tex = t, coords = co }
        return t
    end
    local tl = new(x, y, c, c) tl:SetPoint('TOPLEFT', frame, 'TOPLEFT', 0, 0) tl:SetSize(d, d)
    local tr = new(x + w - c, y, c, c) tr:SetPoint('TOPRIGHT', frame, 'TOPRIGHT', 0, 0) tr:SetSize(d, d)
    local bl = new(x, y + h - c, c, c) bl:SetPoint('BOTTOMLEFT', frame, 'BOTTOMLEFT', 0, 0) bl:SetSize(d, d)
    local br = new(x + w - c, y + h - c, c, c) br:SetPoint('BOTTOMRIGHT', frame, 'BOTTOMRIGHT', 0, 0) br:SetSize(d, d)
    local top = new(x + c, y, w - 2 * c, c)
    top:SetPoint('TOPLEFT', frame, 'TOPLEFT', d, 0) top:SetPoint('TOPRIGHT', frame, 'TOPRIGHT', -d, 0) top:SetHeight(d)
    local bottom = new(x + c, y + h - c, w - 2 * c, c)
    bottom:SetPoint('BOTTOMLEFT', frame, 'BOTTOMLEFT', d, 0) bottom:SetPoint('BOTTOMRIGHT', frame, 'BOTTOMRIGHT', -d, 0) bottom:SetHeight(d)
    local left = new(x, y + c, c, h - 2 * c)
    left:SetPoint('TOPLEFT', frame, 'TOPLEFT', 0, -d) left:SetPoint('BOTTOMLEFT', frame, 'BOTTOMLEFT', 0, d) left:SetWidth(d)
    local right = new(x + w - c, y + c, c, h - 2 * c)
    right:SetPoint('TOPRIGHT', frame, 'TOPRIGHT', 0, -d) right:SetPoint('BOTTOMRIGHT', frame, 'BOTTOMRIGHT', 0, d) right:SetWidth(d)
    local middle = new(x + c, y + c, w - 2 * c, h - 2 * c)
    middle:SetPoint('TOPLEFT', frame, 'TOPLEFT', d, -d) middle:SetPoint('BOTTOMRIGHT', frame, 'BOTTOMRIGHT', -d, d)

    local slice = {}
    function slice.SetShown(show)
        for _, p in ipairs(parts) do if show then p.tex:Show() else p.tex:Hide() end end
    end
    function slice.SetAlpha(a) for _, p in ipairs(parts) do p.tex:SetAlpha(a) end end
    function slice.SetBlend(m) for _, p in ipairs(parts) do p.tex:SetBlendMode(m) end end
    function slice.Set(newPath)
        for _, p in ipairs(parts) do
            p.tex:SetTexture(newPath)
            p.tex:SetTexCoord(p.coords[1], p.coords[2], p.coords[3], p.coords[4])
        end
    end
    return slice
end

-- The ordinary button: dark list bar, lighter on hover, yellow-outlined while pressed. Sizes in layout pixels.
local function MakeButton(parent, text, w, h, onClick)
    h = h + BUTTON_EXTRA
    local b = CreateFrame('Button', nil, parent)
    b:SetSize(P(w), P(h))
    local corner = math.min(8, h * 0.3)
    b.skin = NineSlice(b, 'BACKGROUND', LIST_ATLAS, 512, 512, LIST.dark, 12, corner)
    b.lit = NineSlice(b, 'ARTWORK', LIST_ATLAS, 512, 512, LIST.selected, 12, corner)
    b.lit.SetShown(false)
    local hover = NineSlice(b, 'HIGHLIGHT', LIST_ATLAS, 512, 512, LIST.light, 12, corner)
    hover.SetBlend('ADD') hover.SetAlpha(0.55)
    local tf = CreateFrame('Frame', nil, b) tf:SetAllPoints() tf:SetFrameLevel(b:GetFrameLevel() + 3)
    b.label = tf:CreateFontString(nil, 'OVERLAY', 'GameFontNormal')
    b.label:SetPoint('CENTER', 0, 0) b.label:SetTextColor(1, 0.82, 0) b.label:SetText(text)
    b:SetScript('OnClick', function(self, ...) if not self.off and onClick then onClick(self, ...) end end)
    local enable, disable = b.Enable, b.Disable
    b.Enable = function(self)
        enable(self) self.off = false
        self.label:SetTextColor(1, 0.82, 0) self.skin.SetAlpha(1)
    end
    b.Disable = function(self)
        disable(self) self.off = true
        self.label:SetTextColor(0.45, 0.42, 0.38) self.skin.SetAlpha(0.45) self.lit.SetShown(false)
    end
    b:SetScript('OnMouseDown', function(self)
        if not self.off then self.lit.SetShown(true) self.label:SetPoint('CENTER', 0, -1) end
    end)
    b:SetScript('OnMouseUp', function(self)
        if self.off then return end
        self.lit.SetShown(false) self.label:SetPoint('CENTER', 0, 0)
    end)
    return b
end

-- The call to action: glue panel button art.
local function GlueButton(parent, text, w, h, onClick)
    h = h + BUTTON_EXTRA
    local b = CreateFrame('Button', nil, parent)
    b:SetSize(P(w), P(h))
    b.skin = NineSlice(b, 'BACKGROUND', GLUE_DIR .. 'up', 256, 64, GLUE, 14, 10)
    local rim = NineSlice(b, 'HIGHLIGHT', GLUE_DIR .. 'highlight', 256, 64, GLUE, 14, 10)
    rim.SetBlend('ADD')
    b.label = b:CreateFontString(nil, 'OVERLAY', 'GameFontNormalLarge')
    b.label:SetPoint('CENTER', 0, P(2)) b.label:SetText(text) b.label:SetTextColor(1, 0.82, 0)
    local face, size = b.label:GetFont()
    b.label:SetFont(face, size - 2)
    b:SetScript('OnClick', function(self, ...) if not self.off and onClick then onClick(self, ...) end end)
    local enable, disable = b.Enable, b.Disable
    b.Enable = function(self)
        enable(self) self.off = false
        self.label:SetTextColor(1, 0.82, 0) self.skin.Set(GLUE_DIR .. 'up')
    end
    b.Disable = function(self)
        disable(self) self.off = true
        self.label:SetTextColor(0.5, 0.5, 0.5) self.skin.Set(GLUE_DIR .. 'disabled')
    end
    b:SetScript('OnMouseDown', function(self)
        if not self.off then self.skin.Set(GLUE_DIR .. 'down') self.label:SetPoint('CENTER', 0, P(2) - 1) end
    end)
    b:SetScript('OnMouseUp', function(self)
        if self.off then return end
        self.skin.Set(GLUE_DIR .. 'up') self.label:SetPoint('CENTER', 0, P(2))
    end)
    return b
end

-- The item list's panel: brown fill inset 8, the atlas frame over it. Placed from its parent's top left.
local function FramedPanel(parent, x, y, w, h)
    local f = CreateFrame('Frame', nil, parent)
    f:SetPoint('TOPLEFT', parent, 'TOPLEFT', P(x), -P(y))
    f:SetSize(P(w), P(h))
    local brown = f:CreateTexture(nil, 'BACKGROUND')
    brown:SetPoint('TOPLEFT', P(8), -P(8)) brown:SetPoint('BOTTOMRIGHT', -P(8), P(8))
    brown:SetTexture(0.1, 0.065, 0.04, 1)
    -- the border art on its own frame, above everything inside the panel (the tree runs under its inner edge)
    local border = CreateFrame('Frame', nil, f)
    border:SetAllPoints(f) border:SetFrameLevel(f:GetFrameLevel() + 20)
    NineSlice(border, 'OVERLAY', ATLAS, ATLAS_SIZE, ATLAS_SIZE, RECT.frame, 48, 28)
    return f
end


-- The circular tree: a core, three trees (Dive, Glide, Wings) of five rings, three big talents and a linked talent
-- each. Manastorm_TalentD rows are in talents.json order; row fields are listed in the client's PushTable.
local TREES = { 'Dive', 'Glide', 'Wings' }
local TREE_COLOR = { { 1, 0.44, 0.3 }, { 0.43, 0.86, 0.35 }, { 0.69, 0.5, 1 } }
-- Ring radius in screen pixels, by ring (0 = the core; the world is laid out in whole screen pixels). Angles come
-- from each row (math degrees, y up).
local RAD = { [0] = 0, 125, 205, 285, 365, 445, 545, 630 }
local PANEL_SIZE = 2 * (630 + 110)
-- the view: zoom steps; 1 is the pixel-perfect one and the one the panel opens at
local ZSTEPS, ZDEF = { 0.4, 0.55, 0.7, 0.85, 1, 1.3, 1.7 }, 3
local KIND_CORE, KIND_NODE, KIND_BRIDGE, KIND_CAP, KIND_LINKED = 0, 1, 2, 3, 4

-- Every Ability_DragonRiding_* icon the client has (36), one per talent, none twice inside a tree (the core and the
-- bridges count as one group); the other trees reuse an icon only because 52 talents outnumber the 36 icons.
-- Generated by the balance scripts; the comment on each line is the talent.
local ICON_BY_ID = {
    [100] = 'HaltTheWinds01',              -- Windborn
    [101] = 'Diving01',                    -- Steep Descent
    [102] = 'BarrelRoll01',                -- Held Breath
    [103] = 'Launch01',                    -- Terminal Velocity
    [104] = 'BronzeRewind01',              -- Sharp Banking
    [105] = 'ThrillChaser01',              -- Dive Yield
    [106] = 'PrepareforLanding01',         -- Shallow Pull
    [107] = 'Compassion01',                -- Aftershock
    [108] = 'DraconicCultivation01',       -- Gravity Well
    [109] = 'DragonRidersInitiative01',    -- Rebound
    [110] = 'DragonRidingLearner01',       -- Dive Bomb
    [111] = 'DragonridingGliding01',       -- Meteor
    [112] = 'Glyph01',                     -- Hammerfall
    [113] = 'DynamicFlight01',             -- Redline
    [114] = 'DrakeandRiderTraining01',     -- Long Glide
    [115] = 'UpwardFlap01',                -- Updraft
    [116] = 'ForwardFlap01',               -- Skyward Path
    [117] = 'YearningfortheSky01',         -- Slipstream
    [118] = 'StaticFlight01',              -- Hover Poise
    [119] = 'RestorativeTravels01',        -- Long Haul
    [120] = 'RideAlong01',                 -- Glide Anchor
    [121] = 'RushoftheFlights01',          -- Jetstream
    [122] = 'SecondSeat01',                -- Draft
    [123] = 'Vigor01',                     -- Updraft Master
    [124] = 'AirborneTumbling01',          -- Eternal Current
    [125] = 'DragonRidingLearner01',       -- Highway
    [126] = 'Compassion01',                -- Skybound
    [127] = 'RushoftheFlights01',          -- Iron Wings
    [128] = 'Vigor01',                     -- Quick Recovery
    [129] = 'AtHomeAloft01',               -- Diving Wings
    [130] = 'Launch01',                    -- Afterburn
    [131] = 'SecondSeat01',                -- Wing Reserve
    [132] = 'BronzeRewind01',              -- Banked Momentum
    [133] = 'DraconicCultivation01',       -- Overcharge
    [134] = 'ForwardFlap01',               -- Surge Cells
    [135] = 'RestorativeTravels01',        -- Fast Cells
    [136] = 'DragonRidersInitiative01',    -- Reserve Charge
    [137] = 'Compassion01',                -- Twin Vault
    [138] = 'DynamicFlight01',             -- Overdrive
    [139] = 'Glyph01',                     -- Last Gasp
    [140] = 'SwapFlightStyles01',          -- Still Air
    [141] = 'AirborneTumbling01',          -- Tucked Wings
    [142] = 'OhnahrasGusts01',             -- Rising Current
    [143] = 'WindsofTheIsles01',           -- Skimmer
    [144] = 'LegStretches01',              -- Windfall
    [145] = 'DraconicHunt01',              -- Thunderhead
    [146] = 'ThrilloftheSkies01',          -- Falcons Stoop
    [147] = 'BeyondInfinity01',            -- Endless Horizon
    [148] = 'EvasiveManeuvers01',          -- Tempest Heart
    [149] = 'DragonRiding01',              -- Stoop Mastery
    [150] = 'AtHomeAloft01',               -- Horizon Mastery
    [151] = 'HelpfromtheAbove01',          -- Tempest Mastery
}
local function IconPath(e)
    return 'Interface\\Icons\\Ability_DragonRiding_' .. (ICON_BY_ID[e[1]] or 'Diving01')
end

local BACKDROP = {
    bgFile = WHITE, edgeFile = WHITE, edgeSize = 1,
    insets = { left = 1, right = 1, top = 1, bottom = 1 },
}

-- Node frames from Interface\speedflying\talents (2048x1024). Each shape has three frames cut to the exact box of
-- the sprite (68 x 68, the hexagon 104 x 80, so no neighbour's glow rides in): the grey stone frame while the
-- talent is locked, the gold one when it can be learnt or has a rank, the green one at full rank. The stone
-- frame's inside is translucent noise, so the picture shows dimmed through it. The picture sits UNDER the frame
-- and runs out beneath the frame's black inner band (`icon`, in sprite pixels, is the hole plus 3 px each side),
-- so no blank gap shows between them, and its edge is cropped by ICON_CROP to lose the dark rim the icons carry.
-- The hexagon has no green frame without a crack through it, so its full rank is the gold frame tinted green.
-- The round frame gets a round picture (portrait mask; 3.3.5 has no mask textures).
local TALENTS = 'Interface\\speedflying\\talents'
-- The core and the ring-6 talents use the two rings of Interface\speedflying\talent-main (256 x 512): the plain ring
-- and the one with the knob at the bottom. They have one frame each (`single`): locked is the same ring grey, full
-- rank the same ring bold. `hole` is the open width in sprite pixels, `iy` how far the ring's centre sits above the
-- sprite's centre (the knob hangs below it).
-- `glow` is a full-rank halo cut from the sheet: x, y, w, h, then the w, h of its black core, which is matched to
-- the node's outer size.
local MAIN = 'Interface\\speedflying\\talent-main'
-- the sprites include each ring's drop shadow (alpha > 2; the two shadows touch at x = 123); `outer` is the ring alone
local PLAIN, ORN = { 131, 4, 100, 101 }, { 3, 4, 119, 118 }
local GLOW_SQ, GLOW_RD, GLOW_HX = { 8, 576, 201, 201, 123, 123 }, { 1094, 174, 109, 110, 62, 62 },
    { 282, 384, 171, 154, 95, 76 }
local SHAPES = {
    sq = { dark = { 1011, 579, 68, 68 }, on = { 1011, 743, 68, 68 }, max = { 1960, 7, 68, 68 }, icon = 60,
           glow = GLOW_SQ },
    rd = { dark = { 1013, 323, 68, 68 }, on = { 1013, 495, 68, 68 }, max = { 199, 881, 68, 68 }, icon = 60,
           round = true, glow = GLOW_RD },
    hx = { dark = { 893, 187, 104, 80 }, on = { 893, 399, 104, 80 }, max = { 893, 399, 104, 80 },
           maxTint = { 0.5, 1, 0.45 }, icon = 60, round = true, glow = GLOW_HX, glowK = 0.75 },
    core = { dark = PLAIN, on = PLAIN, max = PLAIN, tex = MAIN, tw = 256, th = 512, icon = 61, iy = -1.5,
             outer = 78, round = true, single = true, glow = GLOW_RD },
    cap = { dark = ORN, on = ORN, max = ORN, tex = MAIN, tw = 256, th = 512, icon = 78, iy = 0.5, outer = 95,
            round = true, single = true, glow = GLOW_RD },
}
local ICON_CROP = 0.08
-- node size on screen, per kind: NODE_SCALE of the sprite, the core larger, linked smaller
local NODE_SCALE = 0.6
local KIND_SHAPE = {
    [KIND_CORE] = { 'core', 1.09 }, [KIND_NODE] = { 'sq', 0.67 }, [KIND_BRIDGE] = { 'hx', 1 },
    [KIND_CAP] = { 'cap', 0.8 }, [KIND_LINKED] = { 'rd', 0.9 },   -- core, cap: 1.4 and 1 of the ring alone
}
-- the full-rank halo: its black core is drawn at 96% of the node's outer size, ADD-blended so only the glow shows
local function GlowArt(tex, sh, k)
    local g = sh.glow
    local f = (sh.outer or sh.on[3]) * NODE_SCALE * k * SPX * 0.96 / g[5] * (sh.glowK or 1)
    tex:SetTexture(TALENTS)
    tex:SetTexCoord(g[1] / 2048, (g[1] + g[3]) / 2048, g[2] / 1024, (g[2] + g[4]) / 1024)
    tex:SetSize(g[3] * f, g[4] * f)
end
-- key: 'dark' (locked), 'on' or 'max'. SPX is the size of one screen pixel in UI units; k the kind's size factor.
local function ShapeArt(tex, sh, key, k)
    local r = sh[key]
    local tw, th = sh.tw or 2048, sh.th or 1024
    tex:SetTexture(sh.tex or TALENTS)
    tex:SetTexCoord(r[1] / tw, (r[1] + r[3]) / tw, r[2] / th, (r[2] + r[4]) / th)
    tex:SetSize(r[3] * NODE_SCALE * k * SPX, r[4] * NODE_SCALE * k * SPX)
    if key == 'max' and sh.maxTint then tex:SetVertexColor(sh.maxTint[1], sh.maxTint[2], sh.maxTint[3])
    else tex:SetVertexColor(1, 1, 1) end
end

-- Names of a talent's parents, for the "Needs" line of a locked node's tooltip.
local function ParentNames(e)
    local f, names = _G.Manastorm_TalentFrame, {}
    for k = 13, 15 do
        local p = e[k]
        local row = p and p ~= 0 and f and f.id[p]
        if row then names[#names + 1] = f.rows[row][6] end
    end
    return table.concat(names, ' or ')
end

-- Another big talent holds a rank: this one (if it is a big talent) cannot be chosen.
local function CapTaken(e)
    local f, ranks = _G.Manastorm_TalentFrame, _G.Manastorm_TalentRanks or {}
    if e[11] ~= KIND_CAP or not f then return false end
    for k = 1, #f.rows do
        local r = f.rows[k]
        if r[11] == KIND_CAP and r[1] ~= e[1] and (ranks[k] or 0) > 0 then return true end
    end
    return false
end

-- A bridge whose two trees are already joined by another bridge that holds a rank.
local function BridgeTaken(e)
    local f, ranks = _G.Manastorm_TalentFrame, _G.Manastorm_TalentRanks or {}
    if e[11] ~= KIND_BRIDGE or not f then return false end
    for k = 1, #f.rows do
        local r = f.rows[k]
        if r[11] == KIND_BRIDGE and r[1] ~= e[1] and (ranks[k] or 0) > 0
            and ((r[16] == e[16] and r[17] == e[17]) or (r[16] == e[17] and r[17] == e[16])) then
            return true
        end
    end
    return false
end

-- A plain talent whose ring is closed: another plain talent of its tree and ring is at full rank.
local function RingClosed(e)
    local f, ranks = _G.Manastorm_TalentFrame, _G.Manastorm_TalentRanks or {}
    if e[11] ~= KIND_NODE or not f then return false end
    for k = 1, #f.rows do
        local r = f.rows[k]
        if r[11] == KIND_NODE and r[1] ~= e[1] and r[2] == e[2] and r[3] == e[3] and (ranks[k] or 0) >= r[4] then
            return true
        end
    end
    return false
end

-- A talent node: the picture inside a frame of the sheet above; a copy of the "on" frame glows on hover.
local function BuildNode(parent, e, i, cx, cy)
    local ks = KIND_SHAPE[e[11]] or KIND_SHAPE[KIND_NODE]
    local sh, k = SHAPES[ks[1]], ks[2]
    local b = CreateFrame('Button', nil, parent)
    b:SetSize(sh.on[3] * NODE_SCALE * k * SPX, sh.on[4] * NODE_SCALE * k * SPX)
    b:SetPoint('CENTER', parent, 'TOPLEFT', cx * SPX, -cy * SPX)   -- cx, cy: whole screen pixels in the world
    b:RegisterForClicks('LeftButtonUp', 'RightButtonUp')
    b.shape, b.kscale = sh, k

    -- the picture runs out under the frame's black band (see SHAPES), cropped to lose the icon's own dark rim
    b.icon = b:CreateTexture(nil, 'ARTWORK')
    b.icon:SetPoint('CENTER', 0, (sh.iy or 0) * NODE_SCALE * k * SPX)
    b.icon:SetSize(sh.icon * NODE_SCALE * k * SPX, sh.icon * NODE_SCALE * k * SPX)
    local iconPath = IconPath(e)
    if sh.round then SetPortraitToTexture(b.icon, iconPath)
    else
        b.icon:SetTexture(iconPath .. '.blp')
        b.icon:SetTexCoord(ICON_CROP, 1 - ICON_CROP, ICON_CROP, 1 - ICON_CROP)
    end
    b.frame = b:CreateTexture(nil, 'OVERLAY')
    b.frame:SetPoint('CENTER')
    ShapeArt(b.frame, sh, 'dark', k)
    -- full rank: the green frame added over itself, so the border reads bold
    b.bold = b:CreateTexture(nil, 'OVERLAY')
    b.bold:SetPoint('CENTER')
    ShapeArt(b.bold, sh, 'max', k)
    b.bold:SetBlendMode('ADD')
    b.bold:Hide()
    -- and a still yellow halo behind the picture
    b.glow = b:CreateTexture(nil, 'BACKGROUND')
    b.glow:SetPoint('CENTER')
    GlowArt(b.glow, sh, k)
    b.glow:SetBlendMode('ADD')
    b.glow:SetDesaturated(true)   -- the sheet's glows are green: grey them, then tint them yellow
    b.glow:SetVertexColor(1, 0.82, 0.2)
    b.glow:SetAlpha(0.55)
    b.glow:Hide()
    b.hl =b:CreateTexture(nil, 'HIGHLIGHT')
    b.hl:SetPoint('CENTER')
    ShapeArt(b.hl, sh, 'on', k)
    b.hl:SetBlendMode('ADD')
    b.rank = b:CreateFontString(nil, 'OVERLAY', 'GameFontNormalSmall')
    b.rank:SetPoint('TOP', b, 'BOTTOM', 0, -2 * SPX)

    -- a right-button drag pans the tree, so it must not also undo a rank; and a node scrolled out of the
    -- window's view (clipped, not removed) must not take clicks
    b:SetScript('OnClick', function(self, btn)
        local f = _G.Manastorm_TalentFrame
        if f and (f.dragged and btn == 'RightButton' or not MouseIsOver(f.scroll)) then return end
        _G.Manastorm_TalentPick = { self.d[1], btn == 'RightButton' and -1 or 1 }
    end)
    -- b.tip builds the tooltip; Repaint calls it again when the hovered talent's state changes
    b.tip = function(self)
        local f = _G.Manastorm_TalentFrame
        if f and not MouseIsOver(f.scroll) then return end
        local t = self.d
        local r = (_G.Manastorm_TalentRanks or {})[self.ti] or 0
        GameTooltip:SetOwner(self, 'ANCHOR_RIGHT')
        local can = (_G.Manastorm_TalentCan or {})[self.ti]
        GameTooltip:SetText(t[6], 1, 1, 1)
        GameTooltip:AddLine('Rank ' .. r .. '/' .. t[4], 1, 1, 1)
        GameTooltip:AddLine(t[7], 1, 0.82, 0, 1)
        if t[11] == KIND_BRIDGE and t[10] and t[10] ~= 0 then
            GameTooltip:AddLine('Opens the ' .. (TREES[t[10]] or 'next') .. ' tree.', 0.4, 0.9, 1, 1)
        end
        if r < t[4] and can == 1 then GameTooltip:AddLine('Left click to add a point', 0.1, 1, 0.1) end
        if r > 0 then GameTooltip:AddLine('Right click to remove a point', 1, 0.2, 0.2) end
        if BridgeTaken(t) then
            GameTooltip:AddLine('A bridge between these two trees is already chosen.', 1, 0.4, 0.3, 1)
        elseif CapTaken(t) then
            GameTooltip:AddLine('Another big talent was chosen.', 1, 0.4, 0.3, 1)
        elseif RingClosed(t) then
            GameTooltip:AddLine('A talent on this ring is full: go to the next ring.', 1, 0.4, 0.3, 1)
        elseif r == 0 and can ~= 1 and t[11] ~= KIND_CORE then
            local needs = ParentNames(t)
            if needs ~= '' then GameTooltip:AddLine('Needs full rank in: ' .. needs, 1, 0.4, 0.3, 1) end
        end
        -- the sum of what the ranks give so far, rounded to whole numbers
        if r > 0 and t[8] and t[8] ~= 0 then
            local total = math.floor(math.abs(t[8]) * r + 0.5) * (t[8] < 0 and -1 or 1)
            GameTooltip:AddLine('Total: ' .. (total > 0 and '+' or '') .. total .. (t[9] or ''), 0.78, 0.65, 1)
        end
        GameTooltip:Show()
    end
    b:SetScript('OnEnter', function(self)
        local f = _G.Manastorm_TalentFrame
        if f then f.hover, f.hoverSig = self, nil end
        self:tip()
    end)
    b:SetScript('OnLeave', function()
        local f = _G.Manastorm_TalentFrame
        if f then f.hover = nil end
        GameTooltip:Hide()
    end)
    b.d, b.ti = e, i
    return b
end

local function Build()
    local d = _G.Manastorm_TalentD
    if not d or #d == 0 then return end

    local sw, sh = ScreenPixels()
    SPX = UIParent:GetHeight() / sh
    U = SPX * WIN_K

    -- layout pixels, as in the transmog window, and the window is the transmog window's size: the same sum as
    -- its BuildWindow (card rows from the screen height, the stage width from the screen width). Keep in step.
    local M, TITLE, BAND = 14, 62, 68
    local HEAD_H = 56                    -- one row: the points text left, the progress bar right
    local panelY = TITLE + 10 + HEAD_H + 10
    local CELL_W, CELL_H, GRID_Y, TAB_H, LEFT_W, GAP = 212, 270, 88, 48, 300, 14
    local fixedV = GRID_Y + 72
    local roomH = (sh - 48) / WIN_K - TITLE - 2 * M - 68
    local rows = math.max(2, math.min(3, math.floor((roomH - TAB_H - fixedV - 14) / CELL_H)))
    local rightW = 4 * CELL_W + 72
    local roomW = (sw - 40) / WIN_K - 2 * M - LEFT_W - 2 * GAP - rightW
    local stageW = math.max(520, math.min(1100, math.min(920, roomW)))
    local W = 2 * M + LEFT_W + 2 * GAP + stageW + rightW
    local H = fixedV + rows * CELL_H + 14 + TAB_H + TITLE + 2 * M + BAND
    local VIEW_W, VIEW_H = W - 2 * M, H - panelY - M - BAND

    local f = CreateFrame('Frame', 'Manastorm_TalentFrame', UIParent)
    f:SetSize(P(W), P(H))
    f:SetPoint('CENTER', UIParent, 'CENTER', 0, 0)
    f:SetFrameStrata('HIGH') f:SetToplevel(true)
    f:SetMovable(true) f:EnableMouse(true) f:SetClampedToScreen(true)
    f:RegisterForDrag('LeftButton')
    f:SetScript('OnDragStart', f.StartMoving)
    f:SetScript('OnDragStop', f.StopMovingOrSizing)
    f:SetBackdrop({ bgFile = WHITE, edgeFile = 'Interface\\Tooltips\\UI-Tooltip-Border', edgeSize = 14,
        insets = { left = 3, right = 3, top = 3, bottom = 3 } })
    f:SetBackdropColor(0.045, 0.037, 0.033, 0.98)
    f:SetBackdropBorderColor(0.42, 0.32, 0.17, 1)
    -- never taller than the screen
    local fit = (UIParent:GetHeight() - 40) / P(H)
    if fit < 1 then f:SetScale(fit) end
    f:Hide()
    -- ESC closes the panel like any other window; a name in UISpecialFrames is the whole contract.
    -- Inside the built-once guard, so the list cannot grow a duplicate entry.
    tinsert(UISpecialFrames, 'Manastorm_TalentFrame')

    local strip = f:CreateTexture(nil, 'BACKGROUND')
    strip:SetPoint('TOPLEFT', 4, -4) strip:SetPoint('TOPRIGHT', -4, -4) strip:SetHeight(P(TITLE - 6))
    strip:SetTexture(0.02, 0.017, 0.015, 1)
    local rule = f:CreateTexture(nil, 'BORDER')
    rule:SetPoint('TOPLEFT', strip, 'BOTTOMLEFT', 0, 0) rule:SetPoint('TOPRIGHT', strip, 'BOTTOMRIGHT', 0, 0)
    rule:SetHeight(U / WIN_K) rule:SetTexture(0.3, 0.23, 0.13, 0.3)
    local title = f:CreateFontString(nil, 'ARTWORK', 'GameFontNormal')
    title:SetPoint('TOP', 0, -P(18)) title:SetText('Speedflying Talents')
    local close = CreateFrame('Button', nil, f, 'UIPanelCloseButton')
    close:SetPoint('TOPRIGHT', 0, 0)

    -- the bottom band: a darker strip along the foot, the buttons at its right end
    local band = f:CreateTexture(nil, 'BACKGROUND')
    band:SetPoint('BOTTOMLEFT', 4, 4) band:SetPoint('BOTTOMRIGHT', -4, 4) band:SetHeight(P(BAND))
    band:SetTexture(0.02, 0.017, 0.015, 1)
    local bandRule = f:CreateTexture(nil, 'BORDER')
    bandRule:SetPoint('BOTTOMLEFT', band, 'TOPLEFT', 0, 0) bandRule:SetPoint('BOTTOMRIGHT', band, 'TOPRIGHT', 0, 0)
    bandRule:SetHeight(U / WIN_K) bandRule:SetTexture(0.3, 0.23, 0.13, 0.3)
    f.status = f:CreateFontString(nil, 'ARTWORK', 'GameFontNormal')
    f.status:SetPoint('BOTTOMLEFT', P(M + 6 + 240 + 16), P(BAND / 2 - 7)) f.status:SetJustifyH('LEFT')
    f.status:SetWidth(P(400)) f.status:SetTextColor(1, 0.4, 0.3, 1)

    local by = H - BAND + (BAND - 54) / 2 - 2 - 2 / WIN_K
    f.confirm = GlueButton(f, 'No changes', 240, 50, function() _G.Manastorm_TalentConfirm = 1 end)
    f.confirm:SetPoint('TOPLEFT', f, 'TOPLEFT', P(W - M - 240 - 6), -P(by))
    f.reset = GlueButton(f, 'Reset All', 240, 50, function() _G.Manastorm_TalentReset = 1 end)
    f.reset:SetPoint('TOPLEFT', f, 'TOPLEFT', P(M + 6), -P(by))

    -- points row: the points text on the left, the progress bar on the right
    local head = CreateFrame('Frame', nil, f)
    head:SetPoint('TOPLEFT', f, 'TOPLEFT', P(M), -P(TITLE + 10))
    head:SetSize(P(W - 2 * M), P(HEAD_H))
    local badge = CreateFrame('Frame', nil, head)   -- the points text, no frame around it
    badge:SetPoint('TOPLEFT', head, 'TOPLEFT', P(8), 0)
    badge:SetSize(P(300), P(HEAD_H))
    f.unspent = badge
    badge.pu = badge:CreateFontString(nil, 'OVERLAY', 'GameFontNormalLarge')
    badge.pu:SetPoint('TOPLEFT', badge, 'TOPLEFT', 0, -P(6)) badge.pu:SetTextColor(1, 0.82, 0)
    badge.pg = badge:CreateFontString(nil, 'OVERLAY', 'GameFontNormalSmall')
    badge.pg:SetPoint('TOPLEFT', badge.pu, 'BOTTOMLEFT', 0, -P(4)) badge.pg:SetTextColor(0.7, 0.66, 0.55)
    badge.pu:SetText('0 points available') badge.pg:SetText('0 / 103 earned')

    local bar = CreateFrame('Frame', nil, head)
    -- the glide bar's art (Hud.lua): tooltip frame, casting-bar mask, DragonUI flat fill cropped to the fraction
    local BW, BH = P(420), P(24)
    bar:SetSize(BW, BH) bar:SetPoint('RIGHT', head, 'RIGHT', -P(8), 0)
    bar:SetBackdrop({
        bgFile = 'Interface\\Tooltips\\UI-Tooltip-Background', edgeFile = 'Interface\\Tooltips\\UI-Tooltip-Border',
        tile = true, tileSize = 12, edgeSize = 12, insets = { left = 3, right = 3, top = 3, bottom = 3 },
    })
    bar:SetBackdropColor(0.06, 0.03, 0.02, 0.85) bar:SetBackdropBorderColor(0.60, 0.32, 0.06, 1)
    local mask = bar:CreateTexture(nil, 'BACKGROUND')
    mask:SetPoint('TOPLEFT', bar, 'TOPLEFT', 3, -3) mask:SetPoint('BOTTOMRIGHT', bar, 'BOTTOMRIGHT', -3, 3)
    mask:SetTexture('Interface\\castingbar\\uicastingbarfullmask')
    mask:SetTexCoord(0.08984375, 0.91015625, 0.125, 0.875)
    local fill = bar:CreateTexture(nil, 'ARTWORK')
    fill:SetPoint('TOPLEFT', bar, 'TOPLEFT', 3, -3) fill:SetPoint('BOTTOMLEFT', bar, 'BOTTOMLEFT', 3, 3)
    fill:SetTexture('Interface\\speedflying\\CastingBarStandard2') fill:SetWidth(1)
    local spark = bar:CreateTexture(nil, 'OVERLAY')
    spark:SetSize(14, 14) spark:SetTexture('Interface\\CastingBar\\UI-CastingBar-Spark')
    spark:SetBlendMode('ADD') spark:SetVertexColor(1, 0.9, 0.55, 1)
    bar.fill = fill bar.w = BW - 6 bar.q = 0 bar.tq = 0
    bar:SetScript('OnUpdate', function(self, e)
        local k = e * 6 if k > 1 then k = 1 end
        self.q = (self.q or 0) + ((self.tq or 0) - (self.q or 0)) * k
        local q = self.q
        local fw = self.w * q if fw < 1 then fw = 1 end
        self.fill:SetWidth(fw)
        self.fill:SetTexCoord(0, q, 0, 1)
        spark:ClearAllPoints() spark:SetPoint('CENTER', self, 'LEFT', 3 + fw, 0)
        if q > 0.004 and q < 0.996 then spark:Show() else spark:Hide() end
    end)
    f.bar = bar

    -- the tree: a framed window onto the whole circle. A scroll frame clips; the world (a plain frame under its
    -- scroll child) holds every node and link, laid out in whole screen pixels, and the view moves and scales that
    -- frame -- panning is its anchor, zooming its scale -- so nothing is laid out twice. At zoom 1 the art is
    -- pixel for pixel; the other zoom steps scale it.
    local panel = FramedPanel(f, M, panelY, VIEW_W, VIEW_H)
    local INSET = 13   -- the frame art's border is 28 thick: the tree runs 6 px under its inner edge
    local scroll = CreateFrame('ScrollFrame', nil, panel)
    scroll:SetPoint('TOPLEFT', panel, 'TOPLEFT', P(INSET - 1), -P(INSET - 1))
    scroll:SetPoint('BOTTOMRIGHT', panel, 'BOTTOMRIGHT', -P(INSET), P(INSET))
    scroll:EnableMouse(true) scroll:EnableMouseWheel(true)
    local content = CreateFrame('Frame', nil, scroll)
    content:SetSize(P(VIEW_W - 2 * INSET + 1), P(VIEW_H - 2 * INSET + 1))
    scroll:SetScrollChild(content)
    -- the background: uiframedragonflightbackground (256 x 256) tiled over the whole world, one texture per tile
    -- (3.3.5 has no tiled textures). Its own frame, created first so it sits under the world, and scaled with it.
    local bgw = CreateFrame('Frame', nil, content)
    -- wider than the world: at the smallest zoom (0.4) and panned to the limit the window must still be filled
    local TILE = 256
    local TILES = 22
    bgw:SetSize(TILES * TILE * SPX, TILES * TILE * SPX)
    for ty = 0, TILES - 1 do
        for tx = 0, TILES - 1 do
            local tt = bgw:CreateTexture(nil, 'BACKGROUND')
            tt:SetTexture('Interface\\speedflying\\uiframedragonflightbackground')
            tt:SetSize(TILE * SPX, TILE * SPX)
            tt:SetPoint('TOPLEFT', bgw, 'TOPLEFT', tx * TILE * SPX, -ty * TILE * SPX)
        end
    end
    local world = CreateFrame('Frame', nil, content)
    world:SetSize(PANEL_SIZE * SPX, PANEL_SIZE * SPX)
    bgw:SetPoint('CENTER', world, 'CENTER')
    f.scroll, f.world = scroll, world
    local C = PANEL_SIZE / 2
    f.rows, f.id = d, {}
    for i = 1, #d do f.id[d[i][1]] = i end

    -- centre of a talent in world screen pixels, whole numbers: ring radius at its own angle (math degrees, y up)
    local function Pos(e)
        local a = math.rad(e[12] or 0)
        local r = RAD[e[3]] or 0
        return math.floor(C + r * math.cos(a) + 0.5), math.floor(C - r * math.sin(a) + 0.5)
    end

    -- the tree names sit outside the linked talents, at each tree's centre angle, with what the tree is for
    local TREE_ANGLE = { 90, 330, 210 }
    local TREE_ABOUT = { 'Dive speed and dive start', 'Level, climb and glide', 'Boost charges and boost power' }
    for c = 1, 3 do
        local a = math.rad(TREE_ANGLE[c])
        local lx, ly = C + (RAD[7] + 90) * math.cos(a), C - (RAD[7] + 90) * math.sin(a)
        local lbl = world:CreateFontString(nil, 'ARTWORK', 'GameFontNormalHuge')
        lbl:SetPoint('CENTER', world, 'TOPLEFT', lx * SPX, -ly * SPX)
        lbl:SetText(TREES[c])
        lbl:SetTextColor(TREE_COLOR[c][1], TREE_COLOR[c][2], TREE_COLOR[c][3], 1)
        local sub = world:CreateFontString(nil, 'ARTWORK', 'GameFontNormalSmall')
        sub:SetPoint('TOP', lbl, 'BOTTOM', 0, -4 * SPX)
        sub:SetText(TREE_ABOUT[c])
        sub:SetTextColor(0.75, 0.7, 0.6, 1)
    end

    -- the links: each talent is joined to every parent by a line, ends on whole pixels. Built once here;
    -- Repaint only moves the alpha. link_*.blp: 8 x 64 DXT5, the tree's colour, the four middle columns opaque and the
    -- outer four clear; the 8 texels are drawn LINE_W = 6 px wide, so the line is 3 px with soft edges.
    local LINE_TEX = { 'Interface\\speedflying\\link_dive', 'Interface\\speedflying\\link_glide',
                       'Interface\\speedflying\\link_wings' }
    local LINE_W = 6
    f.links = {}
    -- one link from talent pi (row index) to talent i; `extra` ones are a bridge's reach into the other tree's
    -- ring 5, drawn only once the bridge is bought
    local function AddLink(pi, i, extra, gate)
        local e = d[i]
        local x1, y1 = Pos(d[pi])
        local x2, y2 = Pos(e)
        -- the child's tree, else (a bridge) its parent's tree
        local tree = e[2] ~= 0 and e[2] or (d[pi][2] ~= 0 and d[pi][2] or 1)
        -- 3.3.5 has no rotated textures or lines, so one square texture is centred on the segment and its
        -- texcoords are turned (8-value SetTexCoord): the tree's 8 x 64 image is stretched along the segment
        local dx, dy = x2 - x1, y2 - y1
        local L = math.sqrt(dx * dx + dy * dy)
        local c, s = dx / L, -dy / L                -- direction, y up
        local S = L * math.max(math.abs(c), math.abs(s)) -- square side: its edge meets the line end exactly
        local h = S / 2
        local function UV(u, v) return 0.5 + (v * c - u * s) / LINE_W, 0.5 - (u * c + v * s) / L end
        local ulx, uly = UV(-h, h) local llx, lly = UV(-h, -h)
        local urx, ury = UV(h, h) local lrx, lry = UV(h, -h)
        local lt = world:CreateTexture(nil, 'BACKGROUND')
        lt:SetTexture(LINE_TEX[tree])
        lt:SetTexCoord(ulx, uly, llx, lly, urx, ury, lrx, lry)
        lt:SetSize(S * SPX, S * SPX)
        lt:SetPoint('CENTER', world, 'TOPLEFT', math.floor((x1 + x2) / 2 + 0.5) * SPX,
            -math.floor((y1 + y2) / 2 + 0.5) * SPX)
        lt:SetAlpha(extra and 0 or 0.12)
        f.links[#f.links + 1] = { a = pi, b = i, tex = lt, lvl = 0, extra = extra, gate = gate }
    end
    for i = 1, #d do
        local e = d[i]
        for k = 13, 15 do
            local pi = e[k] and e[k] ~= 0 and f.id[e[k]]
            if pi then AddLink(pi, i, false) end
        end
        -- a bridge reaches the one next-ring talent closest to it in each tree it joins (row fields 18, 19),
        -- and only forward: the link to a tree shows once the bridge's parent in the OTHER tree is full
        if e[11] == KIND_BRIDGE and e[18] and e[18] ~= 0 then
            for s = 1, 2 do
                local j, o = f.id[e[17 + s]], f.id[e[20 - s]]
                if j and o then
                    local gate = {}
                    for k = 13, 15 do
                        local gi = e[k] and e[k] ~= 0 and f.id[e[k]]
                        if gi and d[gi][2] == d[o][2] then gate[#gate + 1] = gi end
                    end
                    AddLink(i, j, true, gate)
                end
            end
        end
    end

    -- the nodes, one per talent, at their ring and angle
    f.slots = {}
    for i = 1, #d do
        local x, y = Pos(d[i])
        f.slots[#f.slots + 1] = BuildNode(world, d[i], i, x, y)
    end

    -- the view: zoom step (i) and pan (x, y in the window's units, y up). The world is anchored by its centre, and
    -- an offset on a scaled frame is in that frame's units, hence the division by z.
    local v = { i = ZDEF, z = ZSTEPS[ZDEF], x = 0, y = 0 }
    f.view = v
    local function Apply()
        v.z = ZSTEPS[v.i]
        local lim = PANEL_SIZE * SPX * v.z / 2
        v.x = math.max(-lim, math.min(lim, v.x))
        v.y = math.max(-lim, math.min(lim, v.y))
        world:SetScale(v.z)
        bgw:SetScale(v.z)
        world:ClearAllPoints()
        world:SetPoint('CENTER', content, 'CENTER', v.x / v.z, v.y / v.z)
        -- pixel perfect only at zoom 1: nudge the world so its corner lands on a whole screen pixel
        if v.z == 1 then
            local l, t = world:GetLeft(), world:GetTop()
            if l and t then
                local es = world:GetEffectiveScale()
                local pxPerUnit = sh / (UIParent:GetHeight() * UIParent:GetEffectiveScale())
                local fx, fy = l * es * pxPerUnit, t * es * pxPerUnit
                local dx, dy = fx - math.floor(fx + 0.5), fy - math.floor(fy + 0.5)
                if dx ~= 0 or dy ~= 0 then
                    local u = 1 / (es * pxPerUnit)
                    world:SetPoint('CENTER', content, 'CENTER', v.x - dx * u, v.y - dy * u)
                end
            end
        end
    end
    Apply()
    -- the cursor in the scroll frame's units, from its centre
    local function Cursor()
        local s = scroll:GetEffectiveScale()
        local mx, my = GetCursorPosition()
        local cx, cy = scroll:GetCenter()
        return mx / s - cx, my / s - cy
    end
    -- scroll steps the zoom, about the cursor: the world point under it stays under it
    scroll:SetScript('OnMouseWheel', function(_, delta)
        local ni = math.max(1, math.min(#ZSTEPS, v.i + (delta > 0 and 1 or -1)))
        if ni == v.i then return end
        local cx, cy = Cursor()
        local wx, wy = (cx - v.x) / v.z, (cy - v.y) / v.z
        v.i = ni
        local z = ZSTEPS[ni]
        v.x, v.y = cx - wx * z, cy - wy * z
        Apply()
    end)
    -- right-button drag pans. Polled, not scripted, because a node under the cursor takes the click events; a
    -- press that moves more than a few pixels is a drag (f.dragged), which a node's right-click then ignores.
    f.dragged = false
    scroll:SetScript('OnUpdate', function(self)
        if not IsMouseButtonDown('RightButton') then v.drag = nil return end
        local mx, my = GetCursorPosition()
        if not v.drag then
            if MouseIsOver(self) then v.drag = { mx = mx, my = my, x = v.x, y = v.y } f.dragged = false end
            return
        end
        local s = self:GetEffectiveScale()
        local dx, dy = (mx - v.drag.mx) / s, (my - v.drag.my) / s
        if not f.dragged and dx * dx + dy * dy > 25 then f.dragged = true end
        if f.dragged then v.x, v.y = v.drag.x + dx, v.drag.y + dy Apply() end
    end)

    -- over the window: the controls, above the nodes
    -- a vignette over the whole window: dark at the edges and corners, clear in the middle (vignette.blp, stretched)
    local vig = CreateFrame('Frame', nil, panel)
    vig:SetAllPoints(scroll) vig:SetFrameLevel(scroll:GetFrameLevel() + 11)
    local vt = vig:CreateTexture(nil, 'OVERLAY')
    vt:SetAllPoints(vig) vt:SetTexture('Interface\\speedflying\\vignette')
    local over = CreateFrame('Frame', nil, panel)
    over:SetAllPoints(panel) over:SetFrameLevel(scroll:GetFrameLevel() + 12)
    -- details: a checkbox (minimalcheckbox.blp, as the transmog window's) and, bottom left, the speeds and angles
    f.stats = over:CreateFontString(nil, 'OVERLAY', 'GameFontNormal')
    f.stats:SetPoint('BOTTOMLEFT', over, 'BOTTOMLEFT', P(22), P(56)) f.stats:SetJustifyH('LEFT')
    f.stats:SetTextColor(1, 1, 1)
    local box = CreateFrame('Button', nil, over)
    box:SetSize(P(150), P(30)) box:SetPoint('BOTTOMLEFT', over, 'BOTTOMLEFT', P(22), P(18))
    local function CB(tex, x, y, w, h, layer)
        local t = box:CreateTexture(nil, layer)
        t:SetTexture(TEX .. 'minimalcheckbox') t:SetTexCoord(x / 64, (x + w) / 64, y / 64, (y + h) / 64)
        t:SetSize(P(w * 1.25), P(h * 1.25)) return t
    end
    local bx = CB(nil, 4, 4, 24, 24, 'ARTWORK') bx:SetPoint('LEFT')
    f.detailsCheck = CB(nil, 8, 34, 22, 19, 'OVERLAY') f.detailsCheck:SetPoint('CENTER', bx, 'CENTER')
    local hl = CB(nil, 40, 3, 22, 19, 'HIGHLIGHT') hl:SetPoint('CENTER', bx, 'CENTER') hl:SetAlpha(0.7)
    local bt = box:CreateFontString(nil, 'OVERLAY', 'GameFontNormalSmall')
    bt:SetPoint('LEFT', bx, 'RIGHT', P(8), 0) bt:SetText('Details')
    f.showDetails = false
    f.detailsCheck:Hide() f.stats:Hide()
    box:SetScript('OnClick', function()
        f.showDetails = not f.showDetails
        local m = f.showDetails and 'Show' or 'Hide'
        f.detailsCheck[m](f.detailsCheck) f.stats[m](f.stats)
    end)
    local recentre = MakeButton(over, 'Reset view', 160, 30, function() v.i, v.x, v.y = ZDEF, 0, 0 Apply() end)
    recentre:SetPoint('TOPRIGHT', over, 'TOPRIGHT', -P(22), -P(18))
    _G.Manastorm_TalentFrame = f
end

local function Repaint()
    local f = _G.Manastorm_TalentFrame if not f then return end
    local Pt, Mx = _G.Manastorm_TalentPoints or 0, _G.Manastorm_TalentMax or 103
    local u = _G.Manastorm_TalentUnspent or 0
    f.unspent.pu:SetText(u .. (u == 1 and ' point available' or ' points available'))
    f.unspent.pg:SetText(Pt .. ' / ' .. Mx .. ' earned')

    local np = _G.Manastorm_TalentPending or 0
    if np > 0 then f.confirm:Enable() f.confirm.label:SetText('Confirm (' .. np .. ')')
    else f.confirm:Disable() f.confirm.label:SetText('No changes') end
    if (_G.Manastorm_TalentSpent or 0) > 0 or np > 0 then f.reset:Enable() else f.reset:Disable() end

    if Pt >= Mx then f.bar.tq = 1
    else
        local q = (_G.Manastorm_TalentPartial or 0) / 1000
        if q < 0 then q = 0 elseif q > 1 then q = 1 end
        f.bar.tq = q
    end
    f.status:SetText(_G.Manastorm_TalentStatus or '')
    local function N(k) return _G['Manastorm_Stat' .. k] or 0 end
    local txt = string.format('Level speed: |cffffd100%d%%|r\nClimb speed: |cffffd100%d%%|r\nDive / glide speed: |cffffd100%d%%|r\n'
        .. 'Boost speed: |cffffd100%d%%|r\nClimb angle: |cffffd100%d|r deg\nDive angle: |cffffd100%d|r deg',
        N('Level'), N('Climb'), N('Dive'), N('Boost'), N('ClimbAngle'), N('DiveAngle'))
    if txt ~= f.statsText then f.statsText = txt f.stats:SetText(txt) end

    local ranks, can = _G.Manastorm_TalentRanks or {}, _G.Manastorm_TalentCan or {}

    -- a link has three looks, so the way through the tree reads at a glance: faint, open (its parent is at full
    -- rank, the unlock rule, so the child can be taken) and lit (the child holds a rank too)
    local LINK_ALPHA = { [0] = 0.12, 0.5, 0.9 }
    for k = 1, #f.links do
        local l = f.links[k]
        local open = (ranks[l.a] or 0) >= f.rows[l.a][4]
        if open and l.gate then   -- a bridge's reach: only from a full parent in the tree on the other side
            open = false
            for _, g in ipairs(l.gate) do if (ranks[g] or 0) >= f.rows[g][4] then open = true end end
        end
        -- open only while the child can still take a rank: a closed ring's links fall back to faint
        local lvl = open and ((ranks[l.b] or 0) > 0 and 2 or (can[l.b] == 1 and 1 or 0)) or 0
        if lvl ~= l.lvl then
            l.lvl = lvl
            l.tex:SetAlpha((l.extra and lvl == 0) and 0 or LINK_ALPHA[lvl])
        end
    end

    for k = 1, #f.slots do
        local b = f.slots[k] local i = f.id[b.d[1]]
        local r = (i and ranks[i]) or 0
        local ok = i and can[i] == 1
        -- frame: grey while locked (a big talent with another one chosen stays grey), gold once ranked or
        -- learnable, green at full rank
        local key = (r >= b.d[4]) and 'max' or ((r > 0 or ok) and 'on' or 'dark')
        if b.key ~= key then
            b.key = key
            ShapeArt(b.frame, b.shape, key, b.kscale)
            if key == 'max' then b.bold:Show() b.glow:Show() else b.bold:Hide() b.glow:Hide() end
            if b.shape.single then   -- one ring only: locked is the same ring grey
                b.frame:SetDesaturated(key == 'dark')
                if key == 'dark' then b.frame:SetVertexColor(0.7, 0.7, 0.7) end
            end
        end
        -- border strength: faint while only learnable, full once a point is in, doubled at full rank
        b.frame:SetAlpha((r == 0 and key == 'on' and not b.shape.single) and 0.55 or 1)   -- the rings never fade
        -- a locked picture is grey and dim under the stone frame, the same on every shape
        local idle = r == 0 and not ok
        b.icon:SetDesaturated(idle)
        if idle then b.icon:SetVertexColor(0.7, 0.7, 0.7) else b.icon:SetVertexColor(1, 1, 1) end
        b.rank:SetText(r .. '/' .. b.d[4])
        if r >= b.d[4] then b.rank:SetTextColor(0.4, 0.9, 0.4, 1)
        elseif r > 0 then b.rank:SetTextColor(1, 0.82, 0, 1)
        elseif ok then b.rank:SetTextColor(1, 1, 1, 1)
        else b.rank:SetTextColor(0.55, 0.52, 0.45, 1) end
    end

    -- the tooltip under the cursor follows a click at once: rebuilt when any rank or the point counts change
    if f.hover then
        local sig = table.concat(ranks, ',') .. '|' .. np .. '|' .. (can[f.hover.ti] or 0)
        if sig ~= f.hoverSig then
            if f.hoverSig then f.hover:tip() end
            f.hoverSig = sig
        end
    end
end

-- The client pushes _G.Manastorm_TalentD on the first frame of every world, so the panel builds itself
-- the frame that table lands and repaints from the client's globals every frame after. Manastorm_SF_Shown
-- is the one fact the client cannot read for itself (it cannot call a method), so it is published.
local drv = CreateFrame('Frame', 'Manastorm_SF_Driver')
drv:SetScript('OnUpdate', function()
    if not _G.Manastorm_TalentFrame then Build() end
    if _G.Manastorm_TalentFrame then _G.Manastorm_SF_Shown = _G.Manastorm_TalentFrame:IsShown() Repaint() end
end)
