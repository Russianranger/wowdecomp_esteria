-- Body sliders page (wowdecomp)
-- A second creator page after Appearance: native sliders for every body/face slider the chosen race and sex
-- supports. Flow: Appearance -> Next -> Body -> Accept. Values live in the client (SetBodyShape / GetBodyShape)
-- and show on the creator preview model.
-- Everything here is guarded: if anything fails the creator behaves exactly as before and a small note says why.
local BS = { page = false, rows = {}, built = false, broken = nil, okayText = nil };

local function BS_Log(text)
    if BodyShapeLog then BodyShapeLog("lua: " .. tostring(text)); end
end
BS_Log("page chunk reached; natives=" .. tostring(type(GetBodyShapeNames) == "function"));

local function BS_Retail()
    return type(GetBodyShapeNames) == "function" and EA_IS_RETAIL();
end

local function BS_InPersonalization()
    return CharacterCreate.personalizationMode and CharCreateOkayButton:IsShown();
end

-- True when Accept should lead to the Body page first.
local function BS_FlowAvailable()
    return not BS.broken and BS_InPersonalization() and BS_Retail();
end

function BodyShapePage_Active()
    return BS.page and BS_FlowAvailable();
end

local function BS_Pretty(name)
    return (name:gsub("(%l)(%u)", "%1 %2"));
end

local function BS_Status()
    -- Other executables sharing this archive have no body-slider commands: say nothing there.
    if type(GetBodyShapeNames) ~= "function" then return nil; end
    if BS.broken then return "Body sliders unavailable: " .. tostring(BS.broken); end
    if not EA_IS_RETAIL() then return "Body sliders: not available for this race (not a Retail model)"; end
    return nil;
end

local function BS_Install()
    local BACKDROP = {
        bgFile = "Interface\\Glues\\Common\\Glue-Tooltip-Background",
        edgeFile = "Interface\\Glues\\Common\\Glue-Tooltip-Border",
        tile = true, tileSize = 16, edgeSize = 16,
        insets = { left = 5, right = 5, top = 5, bottom = 5 },
    };

    local panel = CreateFrame("Frame", "BodyShapePanel", CharacterCreateFrame);
    panel:SetAllPoints(CharacterCreateFrame);
    panel:Hide();

    local note = CharacterCreateFrame:CreateFontString(nil, "OVERLAY", "GlueFontHighlightSmall");
    note:SetPoint("BOTTOMLEFT", CharacterCreateFrame, "BOTTOMLEFT", 20, 8);
    note:SetTextColor(1, 0.6, 0.4);
    note:Hide();

    local function MakeButton(name, text, width, x)
        local button = CreateFrame("Button", name, panel);
        button:SetSize(width, 26);
        button:SetPoint("TOP", CharacterCreateFrame, "TOP", x, -64);
        button:SetNormalFontObject(GlueFontNormalSmall);
        button:SetHighlightFontObject(GlueFontHighlightSmall);
        button:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight");
        button:SetBackdrop(BACKDROP);
        button:SetText(text);
        return button;
    end

    local buttonRandom = MakeButton("BodyShapeRandomButton", "Randomize Body", 140, -80);
    local buttonReset = MakeButton("BodyShapeResetButton", "Reset Body", 110, 60);
    buttonRandom:SetScript("OnClick", function()
        PlaySound("gsCharacterCreationLook");
        BodyShapeRandomize();
        if BS.ResetTriangle then BS.ResetTriangle(); end
    end);
    buttonReset:SetScript("OnClick", function()
        PlaySound("gsCharacterCreationCancel");
        ResetBodyShape();
        if BS.ResetTriangle then BS.ResetTriangle(); end
    end);

    local columnTitle = {};
    for _, side in ipairs({ "Body", "Face" }) do
        local title = panel:CreateFontString(nil, "ARTWORK", "GlueFontNormal");
        title:SetText(side);
        columnTitle[side] = title;
    end

    -- Body-type triangle: Muscular (top), Large (bottom left), Thin (bottom right). The knob's
    -- barycentric weights blend three presets over a few body sliders. Its contribution is added on
    -- top of the sliders, so manual fine-tuning survives moving the knob.
    local TRI_W, TRI_H, TRI_SPACE = 180, 156, 200;
    local TRI_PRESETS = {
        { ShoulderWidth = 0.8, ArmGirth = 0.8, ChestSize = 0.7, WaistWidth = -0.2, HipWidth = -0.1, LegGirth = 0.4 },
        { ShoulderWidth = 0.2, ArmGirth = 0.4, ChestSize = 0.5, WaistWidth = 0.9, HipWidth = 0.7, LegGirth = 0.7 },
        { ShoulderWidth = -0.2, ArmGirth = -0.4, ChestSize = -0.3, WaistWidth = -0.35, HipWidth = -0.25, LegGirth = -0.35 },
    };
    local CORNERS = { { TRI_W / 2, TRI_H }, { 0, 0 }, { TRI_W, 0 } };
    BS.tri = { w = { 1 / 3, 1 / 3, 1 / 3 }, applied = {} };

    local tri = CreateFrame("Frame", "BodyShapeTriangle", panel);
    tri:SetSize(TRI_W, TRI_H);
    tri:EnableMouse(true);
    local STRIP = 3;
    for y = 0, TRI_H - STRIP, STRIP do
        local width = TRI_W * (1 - (y + STRIP / 2) / TRI_H);
        local edge = tri:CreateTexture(nil, "BACKGROUND");
        edge:SetTexture(0.75, 0.85, 0.8, 0.55);
        edge:SetSize(width + 4, STRIP);
        edge:SetPoint("BOTTOM", tri, "BOTTOMLEFT", TRI_W / 2, y);
        if width > 6 and y > 1 then
            local fill = tri:CreateTexture(nil, "BORDER");
            fill:SetTexture(0.04, 0.07, 0.09, 0.85);
            fill:SetSize(width - 4, STRIP);
            fill:SetPoint("BOTTOM", tri, "BOTTOMLEFT", TRI_W / 2, y);
        end
    end
    local function CornerLabel(text, point, x, y)
        local label = tri:CreateFontString(nil, "ARTWORK", "GlueFontNormalSmall");
        label:SetText(text);
        label:SetPoint(point, tri, "BOTTOMLEFT", x, y);
    end
    CornerLabel("Muscular", "BOTTOM", TRI_W / 2, TRI_H + 4);
    CornerLabel("Large", "TOPLEFT", -6, -4);
    CornerLabel("Thin", "TOPRIGHT", TRI_W + 6, -4);

    local knob = tri:CreateTexture(nil, "OVERLAY");
    knob:SetTexture("Interface\\Buttons\\UI-RadioButton");
    knob:SetTexCoord(0.25, 0.5, 0, 1);
    knob:SetSize(18, 18);

    local function PlaceKnob()
        local w = BS.tri.w;
        local x = w[1] * CORNERS[1][1] + w[2] * CORNERS[2][1] + w[3] * CORNERS[3][1];
        local y = w[1] * CORNERS[1][2] + w[2] * CORNERS[2][2] + w[3] * CORNERS[3][2];
        knob:ClearAllPoints();
        knob:SetPoint("CENTER", tri, "BOTTOMLEFT", x, y);
    end

    -- Weights for a local point, clamped onto the triangle.
    local function Weights(x, y)
        local ax, ay = CORNERS[1][1], CORNERS[1][2];
        local bx, by = CORNERS[2][1], CORNERS[2][2];
        local cx, cy = CORNERS[3][1], CORNERS[3][2];
        local d = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
        local a = ((by - cy) * (x - cx) + (cx - bx) * (y - cy)) / d;
        local b = ((cy - ay) * (x - cx) + (ax - cx) * (y - cy)) / d;
        local w = { math.max(a, 0), math.max(b, 0), math.max(1 - a - b, 0) };
        local sum = w[1] + w[2] + w[3];
        return { w[1] / sum, w[2] / sum, w[3] / sum };
    end

    local function ApplyTriangle()
        local w = BS.tri.w;
        local wanted = {};
        -- Each corner's preset fades in from the centre (0) to its corner (1), so a corner shows
        -- exactly its own preset rather than also subtracting the other two.
        for k = 1, 3 do
            local share = math.max(0, (w[k] - 1 / 3) * 1.5);
            for name, amount in pairs(TRI_PRESETS[k]) do
                wanted[name] = (wanted[name] or 0) + share * amount;
            end
        end
        for name, value in pairs(wanted) do
            if GetBodyShapeSupport(name) ~= false then
                local lo, hi = GetBodyShapeRange(name);
                local base = GetBodyShape(name) - (BS.tri.applied[name] or 0);
                local target = math.max(lo or -1, math.min(hi or 1, base + value));
                SetBodyShape(name, target);
                BS.tri.applied[name] = target - base;
            end
        end
        PlaceKnob();
    end

    -- Back to the centre without touching the sliders (used after Reset/Randomize/new character).
    function BS.ResetTriangle()
        BS.tri.w = { 1 / 3, 1 / 3, 1 / 3 };
        BS.tri.applied = {};
        PlaceKnob();
    end

    local function TrackCursor()
        local scale = tri:GetEffectiveScale();
        local cx, cy = GetCursorPosition();
        BS.tri.w = Weights(cx / scale - tri:GetLeft(), cy / scale - tri:GetBottom());
        ApplyTriangle();
    end
    tri:SetScript("OnMouseDown", function(self, button)
        if button == "RightButton" then
            BS.tri.w = { 1 / 3, 1 / 3, 1 / 3 };
            ApplyTriangle();
            return;
        end
        self.dragging = true;
        TrackCursor();
    end);
    tri:SetScript("OnMouseUp", function(self) self.dragging = false; end);
    tri:SetScript("OnHide", function(self) self.dragging = false; end);
    tri:SetScript("OnUpdate", function(self)
        if self.dragging then TrackCursor(); end
    end);
    BS.triangle = tri;
    PlaceKnob();

    local function BuildRows()
        local index = 0;
        for _, group in ipairs({ "body", "face" }) do
            for _, name in ipairs({ GetBodyShapeNames(group) }) do
                index = index + 1;
                local row = CreateFrame("Frame", nil, panel);
                row:SetSize(290, 22);
                row.name, row.group = name, group;

                local label = row:CreateFontString(nil, "ARTWORK", "GlueFontHighlightSmall");
                label:SetPoint("LEFT", 0, 0);
                label:SetWidth(100);
                label:SetJustifyH("LEFT");
                label:SetText(BS_Pretty(name));

                local value = row:CreateFontString(nil, "ARTWORK", "GlueFontNormalSmall");
                value:SetPoint("RIGHT", 0, 0);
                value:SetWidth(52);
                value:SetJustifyH("RIGHT");

                local slider = CreateFrame("Slider", "BodyShapeSlider" .. index, row);
                slider:SetOrientation("HORIZONTAL");
                slider:SetSize(135, 14);
                slider:SetPoint("LEFT", 104, 0);
                slider:SetBackdrop({
                    bgFile = "Interface\\Buttons\\UI-SliderBar-Background",
                    edgeFile = "Interface\\Buttons\\UI-SliderBar-Border",
                    tile = true, tileSize = 8, edgeSize = 8,
                    insets = { left = 3, right = 3, top = 6, bottom = 6 },
                });
                slider:SetThumbTexture("Interface\\Buttons\\UI-SliderBar-Button-Horizontal");
                local lo, hi = GetBodyShapeRange(name);
                slider:SetMinMaxValues(lo or -1, hi or 1);
                slider:SetValueStep(0.05);
                -- The client only places the knob once a value has been set; Sync skips unchanged values.
                slider:SetValue(GetBodyShape(name) or 0);
                slider:EnableMouse(true);
                slider:SetScript("OnValueChanged", function(self, v)
                    v = math.floor(v * 20 + 0.5) / 20;
                    value:SetText(string.format("%d%%", math.floor(v * 100 + 0.5)));
                    if not self.syncing then SetBodyShape(name, v); end
                end);
                -- Right click puts a slider back to its default.
                slider:SetScript("OnMouseUp", function(self, button)
                    if button == "RightButton" then self:SetValue(0); end
                end);

                row.slider, row.value = slider, value;
                BS.rows[#BS.rows + 1] = row;
            end
        end
    end

    local function Sync(row)
        local v = GetBodyShape(row.name);
        if math.abs(row.slider:GetValue() - v) > 0.001 then
            row.slider.syncing = true;
            row.slider:SetValue(v);
            row.slider.syncing = false;
        end
        row.value:SetText(string.format("%d%%", math.floor(v * 100 + 0.5)));
    end

    local function Layout()
        local counts = { body = 0, face = 0 };
        local shown = {};
        for _, row in ipairs(BS.rows) do
            if GetBodyShapeSupport(row.name) == false then
                row:Hide();
            else
                counts[row.group] = counts[row.group] + 1;
                shown[#shown + 1] = row;
            end
        end
        local triShown = false;
        for name in pairs({ ShoulderWidth = 1, ArmGirth = 1, ChestSize = 1, WaistWidth = 1, HipWidth = 1, LegGirth = 1 }) do
            if GetBodyShapeSupport(name) ~= false then triShown = true; end
        end
        BS.triangle:ClearAllPoints();
        BS.triangle:SetPoint("TOPLEFT", CharacterCreateFrame, "TOPLEFT", 105, -160);
        if triShown then BS.triangle:Show(); else BS.triangle:Hide(); end
        local bodyTop = triShown and 200 or 0;
        local tallest = math.max(counts.body, counts.face, 1);
        local spacing = math.min(34, math.max(22, math.floor((GetScreenHeight() - 240) / tallest)));
        local bodySpacing = math.min(spacing, math.max(20, math.floor((GetScreenHeight() - 240 - bodyTop) / math.max(counts.body, 1))));
        local placed = { body = 0, face = 0 };
        for _, row in ipairs(shown) do
            local right = row.group == "face";
            local n = placed[row.group];
            placed[row.group] = n + 1;
            row:ClearAllPoints();
            row:SetPoint(right and "TOPRIGHT" or "TOPLEFT", CharacterCreateFrame, right and "TOPRIGHT" or "TOPLEFT",
                right and -55 or 55, -140 - (right and 0 or bodyTop) - n * (right and spacing or bodySpacing));
            Sync(row);
            row:Show();
        end
        for side, group in pairs({ Body = "body", Face = "face" }) do
            local title = columnTitle[side];
            local right = group == "face";
            title:ClearAllPoints();
            title:SetPoint(right and "TOPRIGHT" or "TOPLEFT", CharacterCreateFrame, right and "TOPRIGHT" or "TOPLEFT",
                right and -55 or 55, -116);
            if counts[group] > 0 then title:Show(); else title:Hide(); end
        end
    end

    local function SetPage(body)
        if BS.page == body then return; end
        BS.page = body;
        PlaySound("gsCharacterCreationLook");
        if CharacterCustomization_CloseChoices then CharacterCustomization_CloseChoices(); end
    end

    -- Accept becomes Next on the Appearance page; Back returns from the Body page before leaving customization.
    local okay = CharacterCreate_Okay;
    function CharacterCreate_Okay(...)
        if BS_FlowAvailable() and not BS.page then
            SetPage(true);
            return;
        end
        return okay(...);
    end
    local back = CharacterCreate_Back;
    function CharacterCreate_Back(...)
        if BS.page then
            SetPage(false);
            return;
        end
        return back(...);
    end
    local toggle = CharacterCreate_TogglePersonalization;
    function CharacterCreate_TogglePersonalization(...)
        if BS.page then
            SetPage(false);
            return;
        end
        return toggle(...);
    end
    -- A new character starts with default sliders.
    local onShow = CharacterCreate_OnShow;
    function CharacterCreate_OnShow(...)
        if ResetBodyShape then ResetBodyShape(); end
        if BS.ResetTriangle then BS.ResetTriangle(); end
        BS.page = false;
        local a, b = onShow(...);
        -- Customizing an existing character: start from its saved sliders (the stock handler just selected it).
        if PAID_SERVICE_TYPE and BodyShapeLoadExisting then BodyShapeLoadExisting(); end
        return a, b;
    end

    local function Tick(self)

        if CharCreateOkayButton and not BS.okayText then BS.okayText = CharCreateOkayButton:GetText(); end

        local status = BS_InPersonalization() and BS_Status() or nil;
        if status then note:SetText(status); note:Show(); else note:Hide(); end

        if not BS_FlowAvailable() then
            BS.page = false;
            panel:Hide();
            if BS.okayText and CharCreateOkayButton:GetText() ~= BS.okayText then CharCreateOkayButton:SetText(BS.okayText); end
            return;
        end

        if not BS.built then
            local ok, err = pcall(BuildRows);
            if not ok then
                BS.broken = tostring(err);
                BS.page = false;
                return;
            end
            BS.built = true;
        end

        if BS.page then
            local ok, err = pcall(Layout);
            if not ok then BS.broken = tostring(err); BS.page = false; return; end
            panel:Show();
            if BS.okayText and CharCreateOkayButton:GetText() ~= BS.okayText then CharCreateOkayButton:SetText(BS.okayText); end
        else
            panel:Hide();
            if CharCreateOkayButton:GetText() ~= "Next" then CharCreateOkayButton:SetText("Next"); end
        end
    end

    local driver = CreateFrame("Frame");
    driver:SetScript("OnUpdate", function(self, elapsed)
        self.elapsed = (self.elapsed or 0) + elapsed;
        if self.elapsed < 0.1 then return; end
        self.elapsed = 0;
        self.logAt = (self.logAt or 0) + 1;
        if self.logAt >= 30 then
            self.logAt = 0;
            if BS_InPersonalization() then
                BS_Log("state natives=" .. tostring(type(GetBodyShapeNames) == "function")
                    .. " retail=" .. tostring(EA_IS_RETAIL()) .. " flow=" .. tostring(BS_FlowAvailable())
                    .. " page=" .. tostring(BS.page) .. " built=" .. tostring(BS.built)
                    .. " broken=" .. tostring(BS.broken) .. " okayText=" .. tostring(CharCreateOkayButton:GetText()));
            end
        end
        local ok, err = pcall(Tick, self);
        if not ok then
            BS.broken = tostring(err);
            BS.page = false;
            BS_Log("tick error: " .. BS.broken);
        end
    end);
    BS_Log("page installed");
end

-- The creator's frames do not exist yet while this file loads, so build the page on the first frame that has them.
local installWaiter = CreateFrame("Frame");
installWaiter:SetScript("OnUpdate", function(self)
    if not (CharacterCreate and CharacterCreateFrame and CharCreateOkayButton) then return; end
    self:SetScript("OnUpdate", nil);
    local installed, installError = pcall(BS_Install);
    if not installed then
        BS.broken = tostring(installError);
        BS_Log("install failed: " .. BS.broken);
        pcall(function()
            local note = CharacterCreateFrame:CreateFontString(nil, "OVERLAY", "GlueFontHighlightSmall");
            note:SetPoint("BOTTOMLEFT", CharacterCreateFrame, "BOTTOMLEFT", 20, 8);
            note:SetTextColor(1, 0.4, 0.4);
            note:SetText("Body page failed to load: " .. BS.broken);
        end);
    end
end);
