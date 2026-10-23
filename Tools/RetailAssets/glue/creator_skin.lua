-- Creator skin (wowdecomp)
-- Restyles the two customization pages (Appearance and Body) without changing how they work: dark edge panels with
-- a gold accent, slim option rows, a step indicator, underlined name fields and flat buttons. It only restyles the
-- existing frames while customization is open and restores the shared ones (Back) when it closes. Any error turns
-- the skin off for the session and leaves the stock look.
-- Its own function: the creator chunk is near Lua's 200-locals-per-function limit.
(function()
local SK ={ on = false, broken = nil, rows = {}, sliders = {}, buttons = {} };

local GOLD = { 0.91, 0.77, 0.42 };
local GOLD_DIM = { 0.54, 0.43, 0.18 };
local INK = { 0.03, 0.04, 0.055 };
local TEXT = { 0.88, 0.86, 0.80 };
local MUTED = { 0.60, 0.59, 0.55 };
-- Everything uses the game font; body text runs a size below headers.
local HEAD_FONT, BODY_FONT = "Fonts\\FRIZQT__.TTF", "Fonts\\FRIZQT__.TTF";
local WHITE = "Interface\\Buttons\\WHITE8X8";
local PANEL_W, EDGE_X = 440, 55; -- rows sit EDGE_X in from each screen edge and are 340 wide

local function SK_Log(text)
    if BodyShapeLog then BodyShapeLog("skin: " .. tostring(text)); end
end

local function Solid(parent, layer, r, g, b, a)
    local tex = parent:CreateTexture(nil, layer or "ARTWORK");
    tex:SetTexture(WHITE);
    tex:SetVertexColor(r, g, b, a or 1);
    return tex;
end

local function Text(parent, font, size, color, layer)
    local fs = parent:CreateFontString(nil, layer or "OVERLAY", "GlueFontHighlightSmall");
    fs:SetFont(font, size);
    fs:SetTextColor(color[1], color[2], color[3]);
    fs:SetShadowOffset(1, -1);
    fs:SetShadowColor(0, 0, 0, 0.8);
    return fs;
end

local function Personalizing()
    return CharacterCreate.personalizationMode and CharCreateOkayButton:IsShown();
end

local function BodyPage()
    return BodyShapePage_Active and BodyShapePage_Active() and BodyShapePanel and BodyShapePanel:IsShown();
end

-- ---- static chrome -------------------------------------------------------------------------------------------

local function BuildChrome()
    local chrome = CreateFrame("Frame", "CreatorSkinChrome", CharacterCreateFrame);
    chrome:SetAllPoints(CharacterCreateFrame);
    chrome:SetFrameLevel(CharacterCreateFrame:GetFrameLevel());
    chrome:Hide();
    SK.chrome = chrome;

    for _, side in ipairs({ "LEFT", "RIGHT" }) do
        local left = side == "LEFT";
        local panel = Solid(chrome, "BACKGROUND", INK[1], INK[2], INK[3], 0.84);
        panel:SetPoint("TOP" .. side, chrome, "TOP" .. side, 0, 0);
        panel:SetPoint("BOTTOM" .. side, chrome, "BOTTOM" .. side, 0, 0);
        panel:SetWidth(PANEL_W);
        -- A soft fade past the panel so it melts into the scene instead of ending in a hard block.
        local fade = chrome:CreateTexture(nil, "BACKGROUND");
        fade:SetTexture(WHITE);
        fade:SetWidth(70);
        fade:SetPoint("TOP" .. (left and "LEFT" or "RIGHT"), panel, "TOP" .. (left and "RIGHT" or "LEFT"), 0, 0);
        fade:SetPoint("BOTTOM" .. (left and "LEFT" or "RIGHT"), panel, "BOTTOM" .. (left and "RIGHT" or "LEFT"), 0, 0);
        if left then
            fade:SetGradientAlpha("HORIZONTAL", INK[1], INK[2], INK[3], 0.5, INK[1], INK[2], INK[3], 0);
        else
            fade:SetGradientAlpha("HORIZONTAL", INK[1], INK[2], INK[3], 0, INK[1], INK[2], INK[3], 0.5);
        end
        local edge = Solid(chrome, "BORDER", GOLD_DIM[1], GOLD_DIM[2], GOLD_DIM[3], 0.9);
        edge:SetWidth(1);
        edge:SetPoint("TOP", panel, "TOP" .. (left and "RIGHT" or "LEFT"), 0, 0);
        edge:SetPoint("BOTTOM", panel, "BOTTOM" .. (left and "RIGHT" or "LEFT"), 0, 0);

        local title = Text(chrome, HEAD_FONT, 16, GOLD);
        local subtitle = Text(chrome, BODY_FONT, 12, MUTED);
        subtitle:SetPoint("TOP" .. side, title, "BOTTOM" .. side, 0, -4);
        local rule = Solid(chrome, "BORDER", GOLD_DIM[1], GOLD_DIM[2], GOLD_DIM[3], 0.6);
        rule:SetSize(340, 1);
        rule:SetPoint("TOP" .. side, chrome, "TOP" .. side, left and EDGE_X or -EDGE_X, -100);
        SK[side] = { title = title, subtitle = subtitle };
    end

    -- Step indicator: Race, Appearance, Body (the last only for races with the Body page).
    local steps, lines = {}, {};
    for index, name in ipairs({ "RACE", "APPEARANCE", "BODY" }) do
        steps[index] = Text(chrome, HEAD_FONT, 13, MUTED);
        steps[index]:SetText(index .. "   " .. name);
    end
    for index = 1, 2 do
        lines[index] = Solid(chrome, "BORDER", GOLD_DIM[1], GOLD_DIM[2], GOLD_DIM[3], 0.9);
        lines[index]:SetSize(40, 1);
    end
    steps[2]:SetPoint("TOP", chrome, "TOP", 0, -24);
    lines[1]:SetPoint("RIGHT", steps[2], "LEFT", -12, 0);
    steps[1]:SetPoint("RIGHT", lines[1], "LEFT", -12, 0);
    lines[2]:SetPoint("LEFT", steps[2], "RIGHT", 12, 0);
    steps[3]:SetPoint("LEFT", lines[2], "RIGHT", 12, 0);
    SK.steps, SK.stepLines = steps, lines;

    local hint = Text(chrome, BODY_FONT, 12, MUTED);
    hint:SetPoint("BOTTOM", chrome, "BOTTOM", 0, 14);
    hint:SetText("Left-drag rotate   \194\183   right-drag pan   \194\183   wheel zoom");
    SK.hint = hint;
end

-- Header text sits after the faction crest on the race page and flush with the rows elsewhere.
local function PlaceHeaders(crest)
    for _, side in ipairs({ "LEFT", "RIGHT" }) do
        local inset = EDGE_X + (crest and 44 or 12);
        local header = SK[side];
        header.title:ClearAllPoints();
        header.title:SetPoint("TOP" .. side, SK.chrome, "TOP" .. side, side == "LEFT" and inset or -inset, -62);
        header.subtitle:ClearAllPoints();
        header.subtitle:SetPoint("TOP" .. side, header.title, "BOTTOM" .. side, 0, -4);
    end
end

-- ---- appearance rows -----------------------------------------------------------------------------------------

local function SkinArrow(button, direction)
    local path = "Interface\\MoneyFrame\\Arrow-" .. direction;
    button:SetNormalTexture(path .. "-Up");
    button:SetPushedTexture(path .. "-Down");
    button:GetNormalTexture():SetTexCoord(0, 1, 0, 1);
    button:GetPushedTexture():SetTexCoord(0, 1, 0, 1);
    button:GetNormalTexture():SetVertexColor(GOLD[1], GOLD[2], GOLD[3]);
    button:SetSize(14, 14);
    local highlight = button:GetHighlightTexture();
    if highlight then highlight:SetAlpha(0); end
end

local function SkinRow(row)
    local name = row:GetName();
    local background = _G[name .. "Background"];
    if background then background:SetAlpha(0); end
    _G[name .. "Text"]:SetAlpha(0);

    local hairline = Solid(row, "BACKGROUND", 1, 1, 1, 0.07);
    hairline:SetHeight(1);
    hairline:SetPoint("TOPLEFT", row, "TOPLEFT", 0, 0);
    hairline:SetPoint("TOPRIGHT", row, "TOPRIGHT", 0, 0);
    local accent = Solid(row, "ARTWORK", GOLD[1], GOLD[2], GOLD[3], 1);
    accent:SetWidth(2);
    accent:SetPoint("TOPLEFT", row, "TOPLEFT", 0, -1);
    accent:SetPoint("BOTTOMLEFT", row, "BOTTOMLEFT", 0, 1);
    accent:Hide();

    local right, left = _G[name .. "RightButton"], _G[name .. "LeftButton"];
    SkinArrow(right, "Right");
    SkinArrow(left, "Left");
    right:ClearAllPoints();
    right:SetPoint("RIGHT", row, "RIGHT", -10, 0);
    local value = Text(row, BODY_FONT, 13, GOLD);
    value:SetWidth(128);
    value:SetJustifyH("CENTER");
    value:SetPoint("RIGHT", right, "LEFT", -4, 0);
    left:ClearAllPoints();
    left:SetPoint("RIGHT", value, "LEFT", -4, 0);
    local label = Text(row, BODY_FONT, 13, TEXT);
    label:SetJustifyH("LEFT");
    label:SetPoint("LEFT", row, "LEFT", 14, 0);
    label:SetPoint("RIGHT", left, "LEFT", -6, 0);

    -- The whole row opens the choice list; the arrows stay on top of it.
    local choice = _G[name .. "ChoiceButton"];
    choice:ClearAllPoints();
    choice:SetAllPoints(row);
    local highlight = choice:GetHighlightTexture();
    if highlight then
        highlight:SetTexture(WHITE);
        highlight:SetVertexColor(GOLD[1], GOLD[2], GOLD[3], 0.10);
        highlight:SetBlendMode("BLEND");
        highlight:SetAllPoints(row);
    end
    choice:HookScript("OnEnter", function() accent:Show(); end);
    choice:HookScript("OnLeave", function() accent:Hide(); end);
    right:SetFrameLevel(choice:GetFrameLevel() + 2);
    left:SetFrameLevel(choice:GetFrameLevel() + 2);

    SK.rows[row] = { label = label, value = value };
end

-- The stock refresh writes "Label  3/36" into the hidden row text; split it, and prefer the choice's own name.
local function UpdateRow(row, skin)
    local text = _G[row:GetName() .. "Text"]:GetText() or "";
    local label, rest = string.match(text, "^(.-)%s%s+(.+)$");
    if not label then label, rest = text, ""; end
    local value, count = string.match(rest, "^(%d+)/(%d+)$");
    local shown = rest;
    if value then
        shown = value .. " / " .. count;
        if EA_IS_RETAIL() then
            local ok, choiceName = pcall(CycleCharCustomization, "EA_CHOICE_NAME", row:GetID(), tonumber(value) - 1);
            if ok and type(choiceName) == "string" and choiceName ~= "" and not string.match(choiceName, "^%d+$") then
                shown = choiceName;
            end
        end
    end
    -- Races on the stock option layout write only the label into the row; read the value from the native selector so
    -- they show the same "n / total" (or the choice's name) as the other races.
    if shown == "" then
        local ok, current, total = pcall(CycleCharCustomization, EA_ACTIVE() and "EA_GET" or "EA_STOCK_GET", row:GetID());
        if ok and type(current) == "number" and type(total) == "number" and total > 0 then
            shown = (current + 1) .. " / " .. total;
            local named, choiceName = pcall(ChoiceText, row:GetID(), current, label);
            if named and type(choiceName) == "string" and choiceName ~= "" and choiceName ~= label .. " " .. (current + 1) then
                shown = choiceName;
            end
        end
    end
    skin.label:SetText(label);
    skin.value:SetText(shown);
end

-- ---- buttons and name fields ---------------------------------------------------------------------------------

-- Back / Next / Accept / Customize already use the stock GlueButtonTemplate that character select uses; the
-- skin only hides their arrow icon so they read exactly like Enter World.
local function SkinButton(button)
    SK.buttons[button] = { icon = _G[button:GetName() .. "Icon"] };
end

local function ApplyButton(button, on)
    local skin = SK.buttons[button];
    if skin and skin.icon then skin.icon:SetAlpha(on and 0 or 1); end
end

-- The same glue button art (GlueButtonTemplate) on buttons that were not built from it.
local GLUE = "Interface\\Glues\\Common\\Glue-Panel-Button-";
local function GlueLook(button)
    for _, state in ipairs({ { "SetNormalTexture", "GetNormalTexture", "Up", 0.578125, 0.75 },
                             { "SetPushedTexture", "GetPushedTexture", "Down", 0.578125, 0.75 },
                             { "SetHighlightTexture", "GetHighlightTexture", "Highlight", 0.625, 0.6875 },
                             { "SetCheckedTexture", "GetCheckedTexture", "Highlight", 0.625, 0.6875 } }) do
        if button[state[1]] then
            button[state[1]](button, GLUE .. state[3]);
            local texture = button[state[2]](button);
            if texture then
                texture:SetTexCoord(0, state[4], 0, state[5]);
                texture:ClearAllPoints();
                texture:SetAllPoints(button);
                texture:SetAlpha(1);
                if state[3] == "Highlight" then texture:SetBlendMode("ADD"); end
            end
        end
    end
end

local function SkinNameEdit(edit)
    for _, region in ipairs({ edit:GetRegions() }) do
        local kind = region:GetObjectType();
        if kind == "Texture" then
            region:SetAlpha(0);
        elseif kind == "FontString" and region:GetText() then -- the "First Name" / "Last Name" caption
            -- Explicit flags and shadow: the template's outline and big drop shadow read as a doubled caption.
            region:SetFont(BODY_FONT, 12, "");
            region:SetShadowOffset(1, -1);
            region:SetShadowColor(0, 0, 0, 0.8);
            region:SetTextColor(MUTED[1], MUTED[2], MUTED[3]);
            region:ClearAllPoints();
            region:SetPoint("BOTTOMLEFT", edit, "TOPLEFT", 15, -8);
            region:SetJustifyH("LEFT");
            region:SetHeight(16);
        end
    end
    edit:SetFont(HEAD_FONT, 15);
    edit:SetTextColor(TEXT[1], TEXT[2], TEXT[3]);
    local underline = Solid(edit, "BORDER", GOLD[1], GOLD[2], GOLD[3], 0.85);
    underline:SetHeight(1);
    underline:SetPoint("BOTTOMLEFT", edit, "BOTTOMLEFT", 12, 6);
    underline:SetPoint("BOTTOMRIGHT", edit, "BOTTOMRIGHT", -10, 6);
end

-- ---- body page -----------------------------------------------------------------------------------------------

local function SkinSlider(slider)
    slider:SetBackdrop({ bgFile = WHITE, tile = false, insets = { left = 0, right = 0, top = 6, bottom = 6 } });
    slider:SetBackdropColor(1, 1, 1, 0.16);
    slider:SetThumbTexture(WHITE);
    local thumb = slider:GetThumbTexture();
    thumb:SetVertexColor(GOLD[1], GOLD[2], GOLD[3], 1);
    thumb:SetSize(3, 12);
    local fill = Solid(slider, "ARTWORK", GOLD[1], GOLD[2], GOLD[3], 0.9);
    fill:SetHeight(2);
    local function Paint()
        local lo, hi = slider:GetMinMaxValues();
        local v = slider:GetValue();
        local width = slider:GetWidth();
        local zero = (0 - lo) / math.max(hi - lo, 0.0001) * width;
        local at = (v - lo) / math.max(hi - lo, 0.0001) * width;
        fill:ClearAllPoints();
        if math.abs(at - zero) < 1 then fill:Hide(); return; end
        fill:SetPoint("LEFT", slider, "LEFT", math.min(zero, at), 0);
        fill:SetWidth(math.abs(at - zero));
        fill:Show();
    end
    slider:HookScript("OnValueChanged", Paint);
    Paint();
    local row = slider:GetParent();
    for _, region in ipairs({ row:GetRegions() }) do
        if region:GetObjectType() == "FontString" then
            local isValue = (region:GetText() or ""):find("%%$");
            region:SetFont(BODY_FONT, 13);
            if isValue then region:SetTextColor(GOLD[1], GOLD[2], GOLD[3]); else region:SetTextColor(TEXT[1], TEXT[2], TEXT[3]); end
        end
    end
    SK.sliders[slider] = Paint;
end

local function SkinBodyPage()
    if SK.bodySkinned or not BodyShapePanel then return; end
    -- The page's own column titles are replaced by the skin's panel headers.
    for _, region in ipairs({ BodyShapePanel:GetRegions() }) do
        if region:GetObjectType() == "FontString" and (region:GetText() == "Body" or region:GetText() == "Face") then
            region:SetAlpha(0);
        end
    end
    if BodyShapeTriangle then
        -- The painted triangle art replaces the strip-drawn triangle and its corner labels; the knob stays.
        for _, region in ipairs({ BodyShapeTriangle:GetRegions() }) do
            local kind = region:GetObjectType();
            if kind == "Texture" then
                if region:GetDrawLayer() == "OVERLAY" then region:SetVertexColor(GOLD[1], GOLD[2], GOLD[3]);
                else region:SetAlpha(0); end
            elseif kind == "FontString" then
                region:SetAlpha(0);
            end
        end
        -- Placement from body_triangle.py placement(): the art's stroke corners land on the knob's
        -- 180 x 156 triangle (CORNERS in body_page.lua), so dragging still lines up with the drawing.
        local art = BodyShapeTriangle:CreateTexture(nil, "ARTWORK");
        art:SetTexture("Interface\\Glues\\CharacterCreate\\EsteriaBodyTriangle");
        art:SetTexCoord(0, 1, 0, 0.888672);
        art:SetSize(217.92, 201.08);
        art:SetPoint("TOPLEFT", BodyShapeTriangle, "BOTTOMLEFT", -17.36, 177.17);
    end
    for _, button in ipairs({ BodyShapeResetButton, BodyShapeRandomButton }) do
        if button then
            button:SetBackdrop(nil);
            GlueLook(button);
            button:SetSize(150, 36);
            button:ClearAllPoints();
        end
    end
    -- Each sits at the inner end of its panel header, clear of the title.
    if BodyShapeResetButton then BodyShapeResetButton:SetPoint("TOPRIGHT", CharacterCreateFrame, "TOPLEFT", EDGE_X + 340, -56); end
    if BodyShapeRandomButton then BodyShapeRandomButton:SetPoint("TOPLEFT", CharacterCreateFrame, "TOPRIGHT", -(EDGE_X + 340), -56); end
    SK.bodySkinned = true;
end

-- ---- per-tick state ------------------------------------------------------------------------------------------

local function RaceLine()
    local race = (GetNameForRace and GetNameForRace()) or "";
    local sex = GetSelectedSex and GetSelectedSex() == SEX_MALE and "male" or "female";
    if race == "" then return ""; end
    return race .. ", " .. sex;
end

-- ---- race page -----------------------------------------------------------------------------------------------

local GRID_COLUMNS, GRID_TOP = 7, -126;
SK.races, SK.toggles = {}, {};

local function RacePage()
    return CharacterCreateRaceButtonsContainer and CharacterCreateRaceButtonsContainer:IsShown()
        and CharacterCreateFrame:IsShown() and not Personalizing();
end

-- Portraits keep their own round rim; the selection ring is the stock round border, tinted gold.
local function SkinRaceButton(button)
    if button.checkedTexture then button.checkedTexture:SetAlpha(0); end
    if button.highlightTexture then button.highlightTexture:SetVertexColor(GOLD[1], GOLD[2], GOLD[3]); end
    if button.staticTexture then button.staticTexture:SetVertexColor(GOLD[1], GOLD[2], GOLD[3]); end
    SK.races[button] = true;
end

-- Both factions as 7-wide grids inside their panel, under a header that carries the faction crest.
local function LayoutRaces()
    if not (SK.on and SK.mode == "race") then return; end
    local spacing = 340 / GRID_COLUMNS;
    local size = spacing - 6;
    local width = CharacterCreateFrame:GetWidth();
    SK.gridBottom = {};
    for _, side in ipairs({ "Alliance", "Horde" }) do
        local indices = side == "Alliance" and CharacterCreate.raceIndicesAlliance or CharacterCreate.raceIndicesHorde;
        local left = side == "Alliance" and EDGE_X or width - EDGE_X - 340;
        local rows = 0;
        for slot, index in ipairs(indices or {}) do
            local button = _G["CharacterCreateRaceButton" .. index];
            if button then
                if not SK.races[button] then SkinRaceButton(button); end
                local column, row = (slot - 1) % GRID_COLUMNS, math.floor((slot - 1) / GRID_COLUMNS);
                rows = math.max(rows, row + 1);
                button:ClearAllPoints();
                button:SetSize(size, size);
                -- Horde fills from the screen edge inward, mirroring the Alliance grid.
                local x = side == "Alliance" and left + spacing / 2 + column * spacing
                    or left + 340 - spacing / 2 - column * spacing;
                button:SetPoint("CENTER", CharacterCreateFrame, "TOPLEFT", x, GRID_TOP - row * spacing);
                button:GetNormalTexture():SetSize(size, size);
                button:GetPushedTexture():SetSize(size, size);
                for _, name in ipairs({ "staticTexture", "highlightTexture", "checkedTexture" }) do
                    if button[name] then button[name]:SetSize(size * 112 / 50, size * 112 / 50); end
                end
            end
        end
        SK.gridBottom[side] = GRID_TOP - math.max(rows - 1, 0) * spacing - size / 2;

        local alliance = side == "Alliance";
        local logo = alliance and CustomizationLogoAlliance or CustomizationLogoHorde;
        local label = alliance and CustomizationTextAlliance or CustomizationTextHorde;
        local hitbox = alliance and AllianceLogoFrame or HordeLogoFrame;
        if logo then
            logo:ClearAllPoints();
            logo:SetSize(52, 52);
            logo:SetPoint("CENTER", CharacterCreateFrame, alliance and "TOPLEFT" or "TOPRIGHT", alliance and EDGE_X + 16 or -(EDGE_X + 16), -74);
            if hitbox then hitbox:ClearAllPoints(); hitbox:SetSize(40, 40); hitbox:SetPoint("CENTER", logo, "CENTER", 0, 0); end
        end
        if label then label:SetAlpha(0); end
    end
end

-- Male / Female / Freeborn: glue buttons whose highlight stays lit when picked. Their clicks are the stock ones.
local function SkinToggle(button, text)
    local normal, pushed = button:GetNormalTexture(), button:GetPushedTexture();
    local skin = { extras = {}, size = { button:GetWidth(), button:GetHeight() },
                   normal = normal and normal:GetTexture(), pushed = pushed and pushed:GetTexture() };
    local own = {};
    for _, getter in ipairs({ "GetNormalTexture", "GetPushedTexture", "GetHighlightTexture", "GetCheckedTexture" }) do
        local texture = button[getter] and button[getter](button);
        if texture then own[texture] = true; end
    end
    -- Everything else drawn on the button: ring borders and the Freeborn plate and caption.
    for _, region in ipairs({ button:GetRegions() }) do
        local kind = region:GetObjectType();
        if (kind == "Texture" or kind == "FontString") and not own[region] then skin.extras[#skin.extras + 1] = region; end
    end
    for _, name in ipairs({ "staticTexture", "highlightTexture", "checkedTexture" }) do
        if button[name] and not own[button[name]] then skin.extras[#skin.extras + 1] = button[name]; end
    end
    skin.label = Text(button, HEAD_FONT, 12, GOLD);
    skin.label:SetPoint("CENTER", button, "CENTER", 0, 2);
    skin.label:SetText(text);
    SK.toggles[button] = skin;
end

local function ApplyToggle(button, on)
    local skin = SK.toggles[button];
    if not skin then return; end
    for _, texture in ipairs(skin.extras) do texture:SetAlpha(on and 0 or 1); end
    if skin.state ~= on then
        skin.state = on;
        if on then
            GlueLook(button);
        elseif skin.normal then
            button:SetNormalTexture(skin.normal);
            button:SetPushedTexture(skin.pushed or skin.normal);
            button:GetNormalTexture():SetTexCoord(0, 1, 0, 1);
            button:GetPushedTexture():SetTexCoord(0, 1, 0, 1);
        end
    end
    if not on then skin.label:Hide(); button:SetSize(skin.size[1], skin.size[2]); return; end
    skin.label:Show();
    -- Glue fonts: gold normally, white when picked.
    if button:GetChecked() then skin.label:SetTextColor(1, 1, 1); else skin.label:SetTextColor(1, 0.82, 0); end
end

local function LayoutToggles()
    if not (SK.on and SK.mode == "race") then return; end
    local male, female, freeborn = CharacterCreateGenderButtonMale, CharacterCreateGenderButtonFemale, CharacterCreateFreebornButton;
    for _, button in ipairs({ male, female }) do
        if button then
            button:SetSize(130, 38);
            button:ClearAllPoints();
            button:SetPoint("BOTTOM", CharacterCreateFrame, "BOTTOM", button == male and -66 or 66, 64);
        end
    end
    if freeborn then
        freeborn:SetSize(150, 38);
        freeborn:ClearAllPoints();
        freeborn:SetPoint("BOTTOM", CharacterCreateFrame, "BOTTOM", 0, 108);
    end
end

-- The selected race: name, description and racials, under that race's own faction grid.
local function BuildCard()
    local card = CreateFrame("Frame", "CreatorSkinRaceCard", SK.chrome);
    card:SetSize(340, 220);
    card.rule = Solid(card, "BORDER", 1, 1, 1, 0.08);
    card.rule:SetHeight(1);
    card.rule:SetPoint("TOPLEFT", card, "TOPLEFT", 0, 0);
    card.rule:SetPoint("TOPRIGHT", card, "TOPRIGHT", 0, 0);
    card.name = Text(card, HEAD_FONT, 15, GOLD);
    card.name:SetPoint("TOPLEFT", card, "TOPLEFT", 0, -12);
    card.description = Text(card, BODY_FONT, 12, TEXT);
    card.description:SetPoint("TOPLEFT", card.name, "BOTTOMLEFT", 0, -6);
    card.description:SetWidth(340);
    card.description:SetJustifyH("LEFT");
    card.traits = {};
    for i = 1, 4 do
        local icon = card:CreateTexture(nil, "ARTWORK");
        icon:SetSize(18, 18);
        local name = Text(card, BODY_FONT, 12, TEXT);
        name:SetPoint("LEFT", icon, "RIGHT", 8, 0);
        card.traits[i] = { icon = icon, name = name };
    end
    card.hint = Text(card, BODY_FONT, 11, MUTED);
    card.hint:SetText("Hover a portrait for details  \194\183  right-click it for every racial");
    SK.card = card;
end

local function UpdateCard()
    local card = SK.card;
    local index = GetSelectedRace and GetSelectedRace();
    local button = index and _G["CharacterCreateRaceButton" .. index];
    if not (button and SK.gridBottom) then card:Hide(); return; end
    local side = "Horde";
    for _, allianceIndex in ipairs(CharacterCreate.raceIndicesAlliance or {}) do
        if allianceIndex == index then side = "Alliance"; end
    end
    local width = CharacterCreateFrame:GetWidth();
    card:ClearAllPoints();
    card:SetPoint("TOPLEFT", CharacterCreateFrame, "TOPLEFT", side == "Alliance" and EDGE_X or width - EDGE_X - 340,
        SK.gridBottom[side] - 20);
    local info = button.raceInfo;
    card.name:SetText(string.upper((info and info.Name) or button.name or ""));
    card.description:SetText(info and info.Description or "");
    local last = card.description;
    for i, trait in ipairs(card.traits) do
        local spell = info and info["Spell_" .. i];
        if spell and spell.name and spell.name ~= "" then
            trait.icon:SetTexture("Interface\\Icons\\" .. (spell.icon or "INV_Misc_QuestionMark"));
            trait.icon:ClearAllPoints();
            trait.icon:SetPoint("TOPLEFT", last, "BOTTOMLEFT", 0, last == card.description and -10 or -6);
            trait.name:SetText(spell.name);
            trait.icon:Show();
            trait.name:Show();
            last = trait.icon;
        else
            trait.icon:Hide();
            trait.name:Hide();
        end
    end
    card.hint:ClearAllPoints();
    card.hint:SetPoint("TOPLEFT", last, "BOTTOMLEFT", 0, -12);
    card:Show();
end

-- After an error: the race page goes back to the stock layout and selection glow.
local function RestoreRace()
    for button in pairs(SK.races) do
        if button.checkedTexture then button.checkedTexture:SetAlpha(1); end
        if button.staticTexture then button.staticTexture:Hide(); end
    end
    for button in pairs(SK.toggles) do ApplyToggle(button, false); end
    if CustomizationTextAlliance then CustomizationTextAlliance:SetAlpha(1); end
    if CustomizationTextHorde then CustomizationTextHorde:SetAlpha(1); end
    if SK.positionRaces then SK.positionRaces(); end
    if SK.positionGender then SK.positionGender(); end
end

-- After an error: give the rows their stock text back (their arrows keep working either way).
local function RestoreRows()
    for row, skin in pairs(SK.rows) do
        local background = _G[row:GetName() .. "Background"];
        if background then background:SetAlpha(1); end
        _G[row:GetName() .. "Text"]:SetAlpha(1);
        skin.label:Hide();
        skin.value:Hide();
    end
end

local function SetSkin(on)
    if SK.on == on then return; end
    SK.on = on;
    if SK.chrome then if on then SK.chrome:Show(); else SK.chrome:Hide(); end end
    for button in pairs(SK.buttons) do ApplyButton(button, on); end
    if not on and CharCreateRandomizeButton then
        CharCreateRandomizeButton:SetAlpha(1);
        CharCreateRandomizeButton:EnableMouse(true);
    end
    if not on then
        CharacterCreateRotateLeft:SetAlpha(1);
        CharacterCreateRotateRight:SetAlpha(1);
    end
end

local function SetSteps(active)
    local body = BodyShapePage_Active and type(GetBodyShapeNames) == "function" and EA_IS_RETAIL();
    for index, step in ipairs(SK.steps) do
        if index == active then step:SetTextColor(GOLD[1], GOLD[2], GOLD[3]); else step:SetTextColor(MUTED[1], MUTED[2], MUTED[3]); end
    end
    -- Only show the Body step when the race has that page.
    if body then SK.steps[3]:Show(); SK.stepLines[2]:Show(); else SK.steps[3]:Hide(); SK.stepLines[2]:Hide(); end
end

local function RaceTick()
    if SK.mode ~= "race" then
        SK.mode = "race";
        LayoutRaces();
        LayoutToggles();
    end
    for button in pairs(SK.buttons) do ApplyButton(button, true); end
    for button in pairs(SK.toggles) do ApplyToggle(button, true); end
    for button in pairs(SK.races) do
        -- Race enumeration re-tints these in faction colours every time; keep them gold.
        if button.highlightTexture then button.highlightTexture:SetVertexColor(GOLD[1], GOLD[2], GOLD[3]); end
        if button.checkedTexture then button.checkedTexture:SetAlpha(0); end
        if button.staticTexture then
            button.staticTexture:SetVertexColor(GOLD[1], GOLD[2], GOLD[3]);
            if button:GetChecked() and button:IsShown() then button.staticTexture:Show(); else button.staticTexture:Hide(); end
        end
    end
    PlaceHeaders(true);
    SK.LEFT.title:SetText(string.upper(ALLIANCE or "Alliance"));
    SK.RIGHT.title:SetText(string.upper(HORDE or "Horde"));
    SK.LEFT.subtitle:SetText(#(CharacterCreate.raceIndicesAlliance or {}) .. " races");
    SK.RIGHT.subtitle:SetText(#(CharacterCreate.raceIndicesHorde or {}) .. " races");
    SetSteps(1);
    UpdateCard();
end

local function Tick()
    if RacePage() then
        SetSkin(true);
        RaceTick();
        return;
    end
    if SK.card then SK.card:Hide(); end
    if not Personalizing() then SK.mode = nil; SetSkin(false); return; end
    SetSkin(true);
    SK.mode = "custom";
    PlaceHeaders(false);

    for i = 1, 64 do
        local row = _G["CharacterCustomizationButtonFrame" .. i];
        if row then
            if not SK.rows[row] then SkinRow(row); end
            if row:IsShown() then UpdateRow(row, SK.rows[row]); end
        end
    end
    for button in pairs(SK.buttons) do ApplyButton(button, true); end

    local body = BodyPage();
    if body then
        SkinBodyPage();
        for i = 1, 128 do
            local slider = _G["BodyShapeSlider" .. i];
            if not slider then break; end
            if not SK.sliders[slider] then SkinSlider(slider); else SK.sliders[slider](); end
        end
    end

    SK.LEFT.title:SetText(body and "BODY" or "APPEARANCE");
    SK.LEFT.subtitle:SetText(body and "Drag the triangle to blend body types" or RaceLine());
    SK.RIGHT.title:SetText(body and "FACE" or "DETAILS");
    SK.RIGHT.subtitle:SetText(body and "Right-click a slider to reset it" or "Click a value for the full list");
    SetSteps(body and 3 or 2);

    -- The hint replaces the rotate arrows; the whole-character randomize sits in the details header.
    CharacterCreateRotateLeft:SetAlpha(0);
    CharacterCreateRotateRight:SetAlpha(0);
    if CharCreateRandomizeButton then
        CharCreateRandomizeButton:ClearAllPoints();
        -- Inner end of the Details header, clear of its title.
        CharCreateRandomizeButton:SetPoint("TOPLEFT", CharacterCreateFrame, "TOPRIGHT", -(EDGE_X + 340), -58);
        if body then CharCreateRandomizeButton:SetAlpha(0); CharCreateRandomizeButton:EnableMouse(false);
        else CharCreateRandomizeButton:SetAlpha(1); CharCreateRandomizeButton:EnableMouse(true); end
    end
end

local function SK_Install()
    BuildChrome();
    SkinButton(CharCreateOkayButton);
    SkinButton(CharCreateBackButton);
    SkinNameEdit(CharacterCreateNameEdit);
    SkinNameEdit(CharacterCreateLastNameEdit);
    if CharCreatePersonalizeButton then SkinButton(CharCreatePersonalizeButton); end
    BuildCard();
    for button, text in pairs({ [CharacterCreateGenderButtonMale or 0] = "MALE", [CharacterCreateGenderButtonFemale or 0] = "FEMALE",
                                [CharacterCreateFreebornButton or 0] = "FREEBORN" }) do
        if button ~= 0 then SkinToggle(button, text); end
    end
    -- The stock code re-lays the portraits and gender picks on enumeration and resize; keep the skin's layout.
    SK.positionRaces, SK.positionGender = CharacterCreate_PositionRaceButtons, CharacterCreate_PositionGenderButtons;
    function CharacterCreate_PositionRaceButtons(...)
        local a, b = SK.positionRaces(...);
        if not SK.broken then pcall(LayoutRaces); end
        return a, b;
    end
    function CharacterCreate_PositionGenderButtons(...)
        local a, b = SK.positionGender(...);
        if not SK.broken then pcall(LayoutToggles); end
        return a, b;
    end
    local driver = CreateFrame("Frame");
    driver:SetScript("OnUpdate", function(self, elapsed)
        self.elapsed = (self.elapsed or 0) + elapsed;
        if self.elapsed < 0.1 then return; end
        self.elapsed = 0;
        local ok, err = pcall(Tick);
        if not ok then
            SK.broken = tostring(err);
            SK_Log("tick error: " .. SK.broken);
            pcall(SetSkin, false);
            pcall(RestoreRows);
            pcall(RestoreRace);
            self:SetScript("OnUpdate", nil);
        end
    end);
    SK_Log("installed");
end

local skinWaiter = CreateFrame("Frame");
skinWaiter:SetScript("OnUpdate", function(self)
    if not (CharacterCreate and CharacterCreateFrame and CharCreateOkayButton and CharacterCreateNameEdit) then return; end
    self:SetScript("OnUpdate", nil);
    local ok, err = pcall(SK_Install);
    if not ok then
        SK.broken = tostring(err);
        SK_Log("install failed: " .. SK.broken);
        SK.on = true;
        pcall(SetSkin, false);
    end
end);
end)();
