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
    end);
    buttonReset:SetScript("OnClick", function()
        PlaySound("gsCharacterCreationCancel");
        ResetBodyShape();
    end);

    local columnTitle = {};
    for _, side in ipairs({ "Body", "Face" }) do
        local title = panel:CreateFontString(nil, "ARTWORK", "GlueFontNormal");
        title:SetText(side);
        columnTitle[side] = title;
    end

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
                value:SetWidth(40);
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
        local tallest = math.max(counts.body, counts.face, 1);
        local spacing = math.min(34, math.max(22, math.floor((GetScreenHeight() - 240) / tallest)));
        local placed = { body = 0, face = 0 };
        for _, row in ipairs(shown) do
            local right = row.group == "face";
            local n = placed[row.group];
            placed[row.group] = n + 1;
            row:ClearAllPoints();
            row:SetPoint(right and "TOPRIGHT" or "TOPLEFT", CharacterCreateFrame, right and "TOPRIGHT" or "TOPLEFT",
                right and -55 or 55, -140 - n * spacing);
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
