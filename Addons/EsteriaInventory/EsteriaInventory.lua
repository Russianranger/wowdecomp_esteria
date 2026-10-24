-- Two windows share one implementation: Windows[0] is the bag window (with a remote Bank tab) and
-- Windows[1] is the banker's Bank window. A window's "view" says which storage it is showing.
local Inv = {}
Inv.__index = Inv
local Windows = {}
local State = { unlocked = false, atBanker = false, atGuildBank = false, cost = 0 }
local GEOMETRY = { [0] = { "size", "position" }, [1] = { "bankSize", "bankPosition" }, [2] = { "guildSize", "guildPosition" } }
local WIDTH, HEIGHT, DEFAULT_PAGE, MAX_PAGE, BASE_TILE = 640, 380, 20, 255, 53
local MIN_W, MIN_H = 590, 300
local ACTION = { browse = 0, deposit = 1, depositAll = 2, withdraw = 3, split = 4, merge = 5,
    expand = 6, favorite = 7, activate = 8, use = 9, equip = 10, sell = 11, setTag = 12, unlock = 13 }
local sorts = { "Name", "Quality", "Count", "Category", "Tag" }
local categories = { "All", "Equipment", "Consumables", "Materials", "Other", "Favorites" }
local tabWidths = { 36, 87, 104, 78, 54, 76 }
local border = { bgFile = "Interface\\Tooltips\\UI-Tooltip-Background", edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
    tile = true, tileSize = 16, edgeSize = 12, insets = { left = 3, right = 3, top = 3, bottom = 3 } }
local dialog = { bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
    edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Gold-Border", tile = true, tileSize = 32, edgeSize = 32,
    insets = { left = 11, right = 12, top = 12, bottom = 11 } }
local DEFAULTS = { replaceBags = true, tileScale = 1, gap = 6, maxTiles = MAX_PAGE, scale = 1, alpha = 1,
    lockPosition = false, lockSize = false, showNames = false, qualityBorders = true, sounds = true,
    hints = true, remember = true, showGold = true, contextMenu = false, bagButton = true, showBankTab = true, guildBank = true }
local ART = "Interface\\EsteriaInventory\\"
local tileBorder = { edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border", edgeSize = 10,
    insets = { left = 2, right = 2, top = 2, bottom = 2 } }

-- Saved settings with missing keys filled in; hiddenCategories/hiddenSorts are sets of disabled options.
local function db()
    EsteriaInventoryDB = EsteriaInventoryDB or {}
    local d = EsteriaInventoryDB
    for key, value in pairs(DEFAULTS) do if d[key] == nil then d[key] = value end end
    d.hiddenCategories, d.hiddenSorts = d.hiddenCategories or {}, d.hiddenSorts or {}
    d.currencies = d.currencies or {}
    return d
end

local function skin(frame, r, g, b)
    frame:SetBackdrop(border)
    frame:SetBackdropColor(r or 0, g or 0, b or 0, 0.9)
    frame:SetBackdropBorderColor(0.4, 0.4, 0.4, 1)
end

local function text(parent, font, value, point, x, y)
    local label = parent:CreateFontString(nil, "OVERLAY", font or "GameFontHighlightSmall")
    label:SetPoint(point or "TOPLEFT", x or 0, y or 0)
    label:SetText(value)
    return label
end

local function button(parent, name, label, width, point, x, y, callback)
    local b = CreateFrame("Button", name, parent, "UIPanelButtonTemplate")
    b:SetSize(width, 26); b:SetPoint(point, x, y); b:SetText(label)
    b.label = { SetText = function(_, value) b:SetText(value) end }
    b:SetScript("OnClick", callback)
    return b
end

local function enabled()
    local carried = Windows[0]
    return carried and carried.active and EsteriaInventoryDB and EsteriaInventoryDB.replaceBags ~= false
end

local function sound(name)
    if db().sounds then PlaySound(name) end
end

-- The stock frame may be created or shown after our login-time setup, so this runs again whenever the vault opens.
local function silenceStockGuildBank()
    if not db().guildBank then return end
    if not GuildBankFrame and LoadAddOn then LoadAddOn("Blizzard_GuildBankUI") end
    local stock = GuildBankFrame
    if not stock then return end
    stock:UnregisterAllEvents()
    stock:SetScript("OnShow", nil); stock:SetScript("OnHide", nil)
    if stock:IsShown() then stock:Hide() end
    if GuildBankPopupFrame and GuildBankPopupFrame:IsShown() then GuildBankPopupFrame:Hide() end
end

local function applyAll()
    for scope = 0, 2 do if Windows[scope] then Windows[scope]:Apply() end end
end

local function newWindow(scope)
    local window = setmetatable({ scope = scope, view = scope,
        prefix = scope == 0 and "EsteriaInventory" or (scope == 1 and "EsteriaBank" or "EsteriaGuildBank"), category = 0, sort = 0, active = false,
        buttons = {}, hooked = false, search = "" }, Inv)
    window.snaps = { [0] = { rows = {}, revision = 0, offset = 0, total = 0 },
        [1] = { rows = {}, revision = 0, offset = 0, total = 0 },
        [2] = { rows = {}, revision = 0, offset = 0, total = 0 } }
    window.snapshot = window.snaps[scope]
    Windows[scope] = window
    return window
end

function Inv:Discover()
    if not EsteriaInventory_Request or self.serverReady then return end
    local now, name = GetTime(), UnitName("player")
    if not name or (self.nextDiscovery and now < self.nextDiscovery) then return end
    self.nextDiscovery, self.discoveryDeadline = now + 8, now + 8
    -- Old cores safely echo HELLO; only a matched core generates READY.
    SendAddonMessage("ESTERIADI", "HELLO:1", "WHISPER", name)
    self.frame.status:SetText("Checking realm support. Normal bags remain available.")
end

-- scope overrides the storage a request targets (a bag-window deposit goes to the bank).
function Inv:Request(action, row, count, offset, text, scope)
    if self.scope == 2 then
        -- Guild bank data comes from the stock client API, so browsing is local.
        if action == ACTION.browse then self:GuildRefresh(offset) end
        return
    end
    if not EsteriaInventory_Request then
        self.frame.status:SetText("The matched Esteria native client is required. Normal bags remain available.")
        return
    end
    local carried = Windows[0]
    if not carried.serverReady then carried:Discover(); return end
    if self.pending then
        -- Keep only the newest browse so tab/page/sort clicks are never lost; mutations are never queued.
        if action == ACTION.browse then self.queued = { offset = offset } end
        return
    end
    scope = scope or self.view
    local snap = self.snaps[scope]
    local vendor = action == ACTION.sell and UnitGUID("npc") or nil
    -- Browse carries the wanted tile count; the server answers with at most that many rows.
    local id = EsteriaInventory_Request(action, snap.revision or 0, row and row.guid or 0,
        count or (action == ACTION.browse and self.page or 0), offset or snap.offset or 0,
        self.category, self.sort, text or self.search, vendor, scope)
    if id then
        self.pending = { id = id, action = action, started = GetTime() }
        self.frame.status:SetText(action == ACTION.browse and "Searching..." or "Updating inventory...")
    else self.frame.status:SetText("No realm connection. Open inventory again after reconnecting.") end
end

function Inv:Refresh()
    if self.active and not self.pending then self:Request(ACTION.browse) end
end

function Inv:Page(offset)
    self.targetOffset = offset
    self:Request(ACTION.browse, nil, nil, offset)
end

function Inv:Flush()
    local queued = self.queued
    if queued and not self.pending then
        self.queued = nil
        self:Request(ACTION.browse, nil, nil, queued.offset)
    end
end

function Inv:Show()
    self.frame:Show(); self.frame:Raise()
    if self.scope == 2 then self:GuildOpen(); return end
    if self.scope == 1 then
        if self.snaps[1].active then self:Page(0) else self:Request(ACTION.activate) end
    elseif not self.active then self:Request(ACTION.activate)
    elseif self.view == 1 then self:Page(0)
    else self:Refresh() end
end

function Inv:Toggle()
    if self.frame:IsShown() then self.frame:Hide() else self:Show() end
end

-- Switches the bag window between carried items (0) and the account bank (1).
function Inv:SetView(view)
    if self.scope ~= 0 or self.view == view then return end
    self.view = view
    self.snapshot = self.snaps[view]
    self.targetOffset = nil
    self:Layout()
    self:Render()
    if view == 1 and not self.snaps[1].active then self:Request(ACTION.activate) else self:Page(0) end
end

local function fullLink(row)
    local _, _, _, color = GetItemQualityColor(row.quality)
    return "|c" .. (color or "ffffffff") .. "|H" .. row.link .. "|h[" .. row.name .. "]|h|r"
end

local function tooltip(self)
    local row = self.row
    if not row then return end
    local hints, bank = db().hints, self.window.view == 1
    GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
    if row.guild then
        GameTooltip:SetGuildBankItem(row.guild.tab, row.guild.slot)
        GameTooltip:Show()
        return
    end
    if row.digital then
        GameTooltip:SetHyperlink(row.link)
        if row.bound then GameTooltip:AddLine(ITEM_SOULBOUND or "Soulbound", 1, 1, 1) end
        if row.maxDurability > 0 then
            GameTooltip:AddLine("Durability " .. row.durability .. " / " .. row.maxDurability, 0.8, 0.8, 0.8)
        end
        GameTooltip:AddLine(bank and "Account bank" or "Digital storage", 0.6, 0.6, 0.6)
        if row.tag ~= "" then GameTooltip:AddLine("Tag: " .. row.tag, 0.4, 0.8, 1) end
        if row.split then GameTooltip:AddLine("Split stack: stays separate until you press Stack", 1, 0.82, 0, true) end
        if hints then
            if bank then
                GameTooltip:AddLine("Left-click: move to your bags   Alt-click: favorite", 0.8, 0.8, 0.8, true)
            else
                GameTooltip:AddLine("Right-click: use, equip or sell at a merchant", 0.8, 0.8, 0.8, true)
                GameTooltip:AddLine("Left-click: withdraw   Alt-click: favorite", 0.8, 0.8, 0.8, true)
            end
            if row.count > 1 then GameTooltip:AddLine("Shift-click: split stack", 0.8, 0.8, 0.8) end
        end
    else
        GameTooltip:SetBagItem(row.bag, row.slot)
        GameTooltip:AddLine("Carried action slot", 0.6, 0.6, 0.6)
    end
    GameTooltip:Show()
end

local function carriedSlots()
    local found = {}
    for bag = 0, NUM_BAG_SLOTS or 4 do
        for slot = 1, GetContainerNumSlots(bag) do
            local link = GetContainerItemLink(bag, slot)
            if link then found[bag .. ":" .. slot] = tonumber(link:match("item:(%d+)")) end
        end
    end
    return found
end

-- Run fn(bag, slot) once the row is a carried item: immediately, or after withdrawing it from storage.
function Inv:Physical(row, fn)
    if not row.digital then fn(row.bag, row.slot); return end
    self.followUp = { before = carriedSlots(), entry = row.entry, fn = fn, expires = GetTime() + 10 }
    self:Request(ACTION.withdraw, row)
    if not self.pending then self.followUp = nil end
end

function Inv:RunFollowUp()
    local follow = self.followUp
    if not follow then return end
    if GetTime() > follow.expires then self.followUp = nil; return end
    for key, entry in pairs(carriedSlots()) do
        if entry == follow.entry and not follow.before[key] then
            local bag, slot = key:match("(%d+):(%d+)")
            self.followUp = nil
            follow.fn(tonumber(bag), tonumber(slot))
            return
        end
    end
end

local popupWindow, popupRow
local menuFrame
function Inv:OpenMenu(tile)
    local I, row = self, tile.row
    if not row then return end
    menuFrame = menuFrame or CreateFrame("Frame", "EsteriaInventoryMenu", UIParent, "UIDropDownMenuTemplate")
    local function entry(level, label, func, extra)
        local info = extra or {}
        info.text, info.func, info.notCheckable = label, func, 1
        UIDropDownMenu_AddButton(info, level)
    end
    local bank = I.view == 1
    UIDropDownMenu_Initialize(menuFrame, function(_, level)
        entry(level, row.name, nil, { isTitle = 1 })
        local equipment = row.inventoryType > 0
        if bank then
            entry(level, "Move to bags", function() I:Request(ACTION.withdraw, row) end)
        else
            if MerchantFrame and MerchantFrame:IsShown() then
                entry(level, "Sell", function() I:Request(ACTION.sell, row) end)
            end
            entry(level, equipment and "Equip" or "Use", function()
                if row.digital then I:Request(equipment and ACTION.equip or ACTION.use, row)
                else ContainerFrameItemButton_OnClick(tile, "RightButton") end
            end)
            if State.atBanker or State.unlocked then
                entry(level, "Send to bank", function() I:Request(ACTION.deposit, row, nil, nil, nil, 1) end)
            end
            if row.digital then
                entry(level, "Withdraw", function() I:Request(ACTION.withdraw, row) end)
            end
        end
        if row.digital then
            entry(level, row.favorite and "Remove favorite" or "Favorite", function() I:Request(ACTION.favorite, row) end)
            if row.count > 1 then
                entry(level, "Split stack...", function()
                    OpenStackSplitFrame(row.count - 1, tile, "BOTTOMLEFT", "TOPLEFT")
                end)
            end
            entry(level, "Set tag...", function()
                popupWindow, popupRow = I, row
                StaticPopup_Show("ESTERIA_INVENTORY_TAG", row.name)
            end)
        end
        if not bank then
            if equipment and row.quality >= 2 and row.quality <= 4 then
                entry(level, "Disenchant", function()
                    I:Physical(row, function(bag, slot) CastSpellByName("Disenchant"); UseContainerItem(bag, slot) end)
                end)
            end
            if equipment then
                entry(level, "Socket gems...", function()
                    I:Physical(row, function(bag, slot)
                        if LoadAddOn then LoadAddOn("Blizzard_ItemSocketingUI") end
                        SocketContainerItem(bag, slot)
                    end)
                end)
            end
        end
        entry(level, "Link in chat", function() ChatEdit_InsertLink(fullLink(row)) end)
        if not bank then
            entry(level, "|cffff4040Destroy...|r", function()
                popupWindow, popupRow = I, row
                StaticPopup_Show("ESTERIA_INVENTORY_DESTROY", row.name)
            end)
        end
        entry(level, "Cancel", nil)
    end, "MENU")
    ToggleDropDownMenu(1, nil, menuFrame, "cursor", 0, 0)
end

-- Guild bank tiles: left-click picks up/drops, right-click withdraws, shift splits (stock behavior).
local function guildClick(self, mouse)
    local I, row = self.window, self.row
    if not row then
        -- An empty tile takes a held item into the first free slot of the tab.
        if CursorHasItem and CursorHasItem() and I.guildFree then PickupGuildBankItem(GetCurrentGuildBankTab(), I.guildFree) end
        return
    end
    local g = row.guild
    if IsControlKeyDown() then DressUpItemLink(fullLink(row)); return end
    if IsShiftKeyDown() and ChatEdit_InsertLink(fullLink(row)) then return end
    if mouse == "RightButton" then AutoStoreGuildBankItem(g.tab, g.slot)
    elseif IsShiftKeyDown() and row.count > 1 then OpenStackSplitFrame(row.count, self, "BOTTOMLEFT", "TOPLEFT")
    else PickupGuildBankItem(g.tab, g.slot) end
end

local function click(self, mouse)
    local I, row = self.window, self.row
    if I.scope == 2 then return guildClick(self, mouse) end
    if not row then return end
    if IsControlKeyDown() then DressUpItemLink(fullLink(row)); return end
    if IsAltKeyDown() and row.digital then I:Request(ACTION.favorite, row); return end
    if IsShiftKeyDown() and ChatEdit_InsertLink(fullLink(row)) then return end
    if mouse == "RightButton" and db().contextMenu then I:OpenMenu(self); return end
    if I.view == 1 then
        if IsShiftKeyDown() and row.count > 1 then
            OpenStackSplitFrame(row.count - 1, self, "BOTTOMLEFT", "TOPLEFT")
        else I:Request(ACTION.withdraw, row) end
        return
    end
    -- With the banker's window open, right-click sends an item to the bank like the stock UI.
    if mouse == "RightButton" and State.atGuildBank and Windows[2].frame:IsShown() then
        -- With the guild bank open, right-click deposits into the selected guild tab like the stock UI.
        I:Physical(row, function(bag, slot) UseContainerItem(bag, slot) end)
        return
    end
    local teller = Windows[1]
    if mouse == "RightButton" and State.atBanker and teller.frame:IsShown() then
        I:Request(ACTION.deposit, row, nil, nil, nil, 1)
        return
    end
    if row.digital then
        if IsShiftKeyDown() and row.count > 1 then
            OpenStackSplitFrame(row.count - 1, self, "BOTTOMLEFT", "TOPLEFT")
        elseif mouse == "RightButton" then
            local action = MerchantFrame and MerchantFrame:IsShown() and ACTION.sell
                or (row.inventoryType > 0 and ACTION.equip or ACTION.use)
            I:Request(action, row)
        else I:Request(ACTION.withdraw, row) end
    else
        ContainerFrameItemButton_OnClick(self, mouse)
        I.refreshAt = GetTime() + 0.25
    end
end

function Inv:Render()
    local snapshot, frame, settings = self.snapshot, self.frame, db()
    local page = self.page or DEFAULT_PAGE
    local bank = self.view == 1
    local guild = self.scope == 2
    frame.title:SetText(guild and ("Esteria Guild Bank" .. (self.guildTabName and (" - " .. self.guildTabName) or ""))
        or (bank and (self.scope == 1 and "Esteria Bank" or "Esteria Bank (Remote)") or "Esteria Inventory"))
    local locked = bank and not State.unlocked and not State.atBanker
    if locked then frame.lock:Show() else frame.lock:Hide() end
    if frame.depositAll then if bank then frame.depositAll:Hide() else frame.depositAll:Show() end end
    if self.scope == 0 and frame.bankTab then
        if bank then frame.bankTab:LockHighlight() else frame.bankTab:UnlockHighlight() end
    end
    frame.capacity:SetText((snapshot.used or 0) .. " / " .. (snapshot.capacity or 0))
    local ratio = snapshot.capacity and snapshot.capacity > 0 and snapshot.used / snapshot.capacity or 0
    frame.bar:SetValue(math.min(ratio, 1))
    frame.percent:SetText(string.format("%.1f%% full", ratio * 100))
    local pages = math.max(1, math.ceil((snapshot.total or 0) / page))
    frame.page:SetText((math.floor((snapshot.offset or 0) / page) + 1) .. " / " .. pages)
    frame.expand.label:SetText(guild and "Expand Tab" or (snapshot.upgrade and snapshot.upgrade > 0
        and (bank and "Expand Bank" or "Expand Storage") or "At capacity limit"))
    if guild then if self.canExpand then frame.expand:Show() else frame.expand:Hide() end end
    frame.sort.label:SetText("Sort: " .. sorts[self.sort + 1])
    for index = 1, page do
        local b = self.buttons[index]
        local row = snapshot.rows[index]
        b.row = row
        if row then row.tag = row.tag or "" end
        local sig = row and row.digital and (row.guid .. ":" .. row.count .. ":" .. row.quality
            .. ":" .. (row.favorite and 1 or 0) .. ":" .. row.tag .. ":" .. (row.split and 1 or 0)) or nil
        if sig and sig == b.sig then
            -- Stored stack unchanged since last render: keep icon, text and border as they are.
        elseif row then
            b:GetParent():SetID(row.bag); b:SetID(row.slot)
            b.hasItem, b.readable = 1, nil
            SetItemButtonTexture(b, row.texture or ("Interface\\Icons\\" .. row.icon))
            SetItemButtonCount(b, row.count)
            b.name:SetText(row.name)
            b.favorite:SetText(row.favorite and "*" or "")
            local r, g, blue = GetItemQualityColor(row.quality)
            local lockedItem = false
            if not row.digital then
                local _, _, isLocked = GetContainerItemInfo(row.bag, row.slot)
                lockedItem = isLocked
            end
            if row.guild then lockedItem = row.locked end
            SetItemButtonDesaturated(b, lockedItem)
            b.name:SetTextColor(r, g, blue)
            b.tagMark:SetText(row.tag ~= "" and "#" or "")
            if settings.qualityBorders and row.quality > 1 then b:SetBackdropBorderColor(r, g, blue, 1)
            else b:SetBackdropBorderColor(0.4, 0.4, 0.4, 0) end
            local start, duration, enable = GetItemCooldown(row.entry)
            CooldownFrame_SetTimer(_G[b:GetName() .. "Cooldown"], start or 0, duration or 0, enable or 0)
            if not row.digital then ContainerFrame_UpdateCooldown(row.bag, b) end
            b:Enable()
            b.sig = sig
        else
            b.sig = nil
            SetItemButtonTexture(b, nil); SetItemButtonCount(b, 0)
            b.hasItem = nil; b.name:SetText(""); b.favorite:SetText("")
            b.tagMark:SetText("")
            b:SetBackdropBorderColor(0.25, 0.25, 0.25, 0)
            CooldownFrame_SetTimer(_G[b:GetName() .. "Cooldown"], 0, 0, 0)
            -- Guild bank empty tiles stay live so a held item can be dropped on them.
            if guild then b:Enable() else b:Disable() end
        end
        local quest = _G[b:GetName() .. "IconQuestTexture"]
        if quest then quest:Hide() end
        if sig ~= b.tipSig or not sig then
            b.tipSig = sig
            if GameTooltip:IsOwned(b) then tooltip(b) end
        end
    end
    for category, tab in pairs(frame.tabs) do
        if category == self.category then tab:LockHighlight() else tab:UnlockHighlight() end
    end
    self:UpdateMoney()
end

-- Gold plus any currencies the user ticked in the Currencies options page.
function Inv:UpdateMoney()
    local settings, frame = db(), self.frame
    if self.scope == 2 then
        frame.gold:SetText("Guild bank: " .. GetCoinTextureString(GetGuildBankMoney and GetGuildBankMoney() or 0))
        frame.currency:SetText(self.guildInfo or "")
        return
    end
    frame.gold:SetText(settings.showGold and GetCoinTextureString(GetMoney()) or "")
    local parts = {}
    if next(settings.currencies) and GetCurrencyListSize then
        for index = 1, GetCurrencyListSize() do
            local name, header, _, _, _, count, _, icon = GetCurrencyListInfo(index)
            if not header and settings.currencies[name] then
                parts[#parts + 1] = "|T" .. (icon or "") .. ":14|t " .. (count or 0)
            end
        end
    end
    frame.currency:SetText(table.concat(parts, "   "))
end

function Inv:FooterHeight()
    local settings = db()
    if self.scope == 2 then return 118 end
    return 96 + ((settings.showGold or next(settings.currencies)) and 22 or 0)
end

-- Draws the Blizzard bag bar (DragonUI) as one button: our icon on the backpack, bag slots and fold arrow gone.
function Inv:SkinBagButtons()
    if not db().bagButton or not enabled() then return end
    for _, slot in ipairs({ CharacterBag0Slot, CharacterBag1Slot, CharacterBag2Slot, CharacterBag3Slot,
        _G.DragonUI_BagsToggle }) do
        if slot then
            if not slot.esteriaHooked then
                slot.esteriaHooked = true
                slot:HookScript("OnShow", function(self) if db().bagButton and enabled() then self:Hide() end end)
            end
            if slot:IsShown() then slot:Hide() end
        end
    end
    local icon = _G.MainMenuBarBackpackButtonIconTexture
    if icon and icon:GetTexture() ~= ART .. "MenuIcon" then
        icon:SetTexture(ART .. "MenuIcon"); icon:SetTexCoord(0, 1, 0, 1)
    end
end

function Inv:InstallHooks()
    local I = self
    if self.hooked then return end
    self.hooked = true
    local functions = { ToggleBackpack = "toggle", ToggleAllBags = "toggle", ToggleBag = "toggle",
        OpenBackpack = "open", OpenAllBags = "open", OpenBag = "open",
        CloseBackpack = "close", CloseAllBags = "close", CloseBag = "close" }
    self.originals = {}
    for name, action in pairs(functions) do
        local original = _G[name]
        if original then
            self.originals[name] = original
            local method = action
            _G[name] = function(bag, ...)
                if not enabled() or (type(bag) == "number" and (bag < 0 or bag > 4)) then
                    return original(bag, ...)
                end
                if method == "toggle" then I:Toggle()
                elseif method == "open" then I:Show()
                else I.frame:Hide() end
            end
        end
    end
    -- The existing large-bag addon retains its implementation for /inventory legacy.
    if EsteriaSatchel then
        -- Satchel polls its hook installer every half-second. Keep the negotiated inventory
        -- wrappers in place; their captured originals still serve /inventory legacy.
        local install = EsteriaSatchel.InstallHooks
        if install then
            EsteriaSatchel.InstallHooks = function(satchel, ...)
                if I.hooked then return end
                return install(satchel, ...)
            end
        end
        for _, name in ipairs({ "Show", "ShowAll", "Toggle" }) do
            local original = EsteriaSatchel[name]
            if original then
                EsteriaSatchel[name] = function(satchel, bag, ...)
                    if enabled() and not (type(bag) == "number" and bag > 4) then return end
                    return original(satchel, bag, ...)
                end
            end
        end
    end
end

-- One pooled item tile; created on demand when a larger window or tile limit needs more of them.
function Inv:MakeTile(index)
    local I, frame = self, self.frame
    local proxy = CreateFrame("Frame", self.prefix .. "ContainerFrame" .. index, frame)
    proxy:Show()
    local name = self.prefix .. "ContainerFrameItem" .. index
    local b = CreateFrame("Button", name, proxy, "ContainerFrameItemButtonTemplate")
    b.window = I
    b:ClearAllPoints(); b:SetAllPoints(proxy); b:SetBackdrop(tileBorder); b:RegisterForClicks("LeftButtonUp", "RightButtonUp")
    local slotArt = b:CreateTexture(nil, "BACKGROUND")
    slotArt:SetTexture(ART .. "SlotBackdrop"); slotArt:SetAllPoints(b)
    b:RegisterForDrag("LeftButton"); b:SetNormalTexture(""); b:SetPushedTexture("")
    b:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight", "ADD")
    -- The bag template refreshes its tooltip from bag/slot IDs, which blanks digital rows after ~0.2s.
    b.UpdateTooltip = nil
    local icon = _G[name .. "IconTexture"]
    icon:ClearAllPoints(); icon:SetPoint("CENTER", b, "CENTER", 0, 0)
    local count = _G[name .. "Count"]
    count:ClearAllPoints(); count:SetPoint("BOTTOMRIGHT", icon, "BOTTOMRIGHT", 4, -3)
    b.name = text(b, "GameFontHighlightSmall", "", "BOTTOM", 0, 4)
    b.name:SetHeight(26); b.name:SetJustifyH("CENTER"); b.name:Hide()
    b.favorite = text(b, "GameFontNormal", "", "TOPLEFT", 4, -3)
    b.tagMark = text(b, "GameFontNormalSmall", "", "TOPRIGHT", -4, -3)
    b.SplitStack = function(tile, amount)
        if not tile.row then return end
        if tile.row.guild then SplitGuildBankItem(tile.row.guild.tab, tile.row.guild.slot, amount)
        else I:Request(ACTION.split, tile.row, amount) end
    end
    b:SetScript("OnEnter", tooltip)
    b:SetScript("OnLeave", function() GameTooltip:Hide() end)
    b:SetScript("OnClick", click)
    b:SetScript("OnDragStart", function(tile)
        if not tile.row then return end
        if tile.row.guild then PickupGuildBankItem(tile.row.guild.tab, tile.row.guild.slot); return end
        if tile.row.digital then
            I:Request(ACTION.withdraw, tile.row)
            frame.status:SetText(I.view == 1 and "Moving item to your bags."
                or "Withdrawing item. Drag its carried icon after the inventory updates.")
        else ContainerFrameItemButton_OnDrag(tile) end
    end)
    b:SetScript("OnReceiveDrag", function(tile)
        if I.scope == 2 then guildClick(tile, "LeftButton")
        elseif tile.row and not tile.row.digital then ContainerFrameItemButton_OnClick(tile, "LeftButton") end
    end)
    -- Bag templates can inherit hidden="true"; the custom container owns their visibility.
    b:Show(); self.buttons[index] = b
end

function Inv:GridTop() return self.scope == 2 and 165 or 137 end

-- Fit as many tiles as the window holds (bounded by Max Tiles and what the server can send), then
-- stretch them to span the width.
function Inv:Layout()
    local f, W, H, settings = self.frame, self.w, self.h, db()
    local gap = settings.gap
    f:SetSize(W, H)
    local top = self:GridTop()
    local availW, availH = W - 52, H - top - self:FooterHeight()
    local tile = BASE_TILE * settings.tileScale
    local cols = math.max(1, math.floor((availW + gap) / (tile + gap)))
    local cell = (availW - (cols - 1) * gap) / cols
    local rows = math.max(1, math.floor((availH + gap) / (cell + gap)))
    local page = math.max(1, math.min(cols * rows, settings.maxTiles, self.cap or MAX_PAGE, MAX_PAGE))
    cols = math.min(cols, page)
    local left = 26 + (availW - (cols * cell + (cols - 1) * gap)) / 2
    for index = #self.buttons + 1, page do self:MakeTile(index) end
    local names = settings.showNames and cell >= 64
    local icon = math.max(12, math.min(cell * (names and 0.5 or 0.65), 56))
    for index, b in ipairs(self.buttons) do
        local proxy = b:GetParent()
        if index <= page then
            proxy:SetSize(cell, cell); proxy:ClearAllPoints()
            proxy:SetPoint("TOPLEFT", left + ((index - 1) % cols) * (cell + gap),
                -top - math.floor((index - 1) / cols) * (cell + gap))
            local art = _G[b:GetName() .. "IconTexture"]
            art:SetSize(icon, icon); art:ClearAllPoints(); art:SetPoint("CENTER", b, "CENTER", 0, names and 8 or 0)
            b.name:SetWidth(cell - 6)
            if names then b.name:Show() else b.name:Hide() end
            proxy:Show()
        else proxy:Hide() end
    end
    f.status:SetWidth(W - 52)
    if self.page and self.page ~= page and (self.active or self.view == 1) then self.pageAt = GetTime() + 0.15 end
    self.page = page
end

-- Guild bank tabs replace the category buttons; unviewable tabs are disabled.
function Inv:LayoutGuildTabs()
    local I, frame = self, self.frame
    for _, tab in pairs(frame.tabs) do tab:Hide() end
    if type(frame.guildTabs) ~= "table" then frame.guildTabs = {} end
    local numTabs = GetNumGuildBankTabs and GetNumGuildBankTabs() or 0
    local current, left = GetCurrentGuildBankTab and GetCurrentGuildBankTab() or 1, 26
    for index = 1, 6 do
        local tab = frame.guildTabs[index]
        if index <= numTabs then
            local name, _, viewable = GetGuildBankTabInfo(index)
            if not tab then
                tab = button(frame, nil, "", 84, "TOPLEFT", left, -99, function()
                    SetCurrentGuildBankTab(index); QueryGuildBankTab(index)
                    I:GuildRefresh(0)
                    I:SetMode(I.mode or "items")
                end)
                frame.guildTabs[index] = tab
            end
            tab:ClearAllPoints(); tab:SetPoint("TOPLEFT", left, -99)
            tab:SetText(string.sub(name or ("Tab " .. index), 1, 11))
            if viewable then tab:Enable() else tab:Disable() end
            if index == current then tab:LockHighlight() else tab:UnlockHighlight() end
            tab:Show()
            left = left + 89
        elseif tab then tab:Hide() end
    end
    local buy = frame.buyTab
    if type(buy) == "table" then
        if self.canBuyTab and numTabs < 6 then
            buy:ClearAllPoints(); buy:SetPoint("TOPLEFT", left, -99); buy:Show()
        else buy:Hide() end
    end
end

function Inv:LayoutTabs()
    if self.scope == 2 then self:LayoutGuildTabs(); return end
    local settings = db()
    local hidden, left = settings.hiddenCategories, 26
    for category = 0, 5 do
        local tab = self.frame.tabs[category]
        if category == 0 or not hidden[category] then
            tab:ClearAllPoints(); tab:SetPoint("TOPLEFT", left, -99); tab:Show()
            left = left + tabWidths[category + 1] + 5
        else tab:Hide() end
    end
    local bankTab = self.scope == 0 and self.frame.bankTab
    if bankTab then
        if settings.showBankTab then
            bankTab:ClearAllPoints(); bankTab:SetPoint("TOPLEFT", left + 10, -99); bankTab:Show()
        else
            bankTab:Hide()
            if self.view == 1 then self:SetView(0) end
        end
    end
end

-- Re-applies every setting after a change.
function Inv:Apply()
    local settings = db()
    if settings.hiddenCategories[self.category] then self.category = 0 end
    local anySort = false
    for s = 1, #sorts do if not settings.hiddenSorts[s] then anySort = true end end
    if not anySort then settings.hiddenSorts[1] = nil end
    if settings.hiddenSorts[self.sort + 1] then
        for s = 1, #sorts do if not settings.hiddenSorts[s] then self.sort = s - 1; break end end
    end
    local frame = self.frame
    if not frame then return end
    frame:SetBackdropColor(1, 1, 1, settings.alpha)
    if settings.lockSize then frame.grip:Hide() else frame.grip:Show() end
    self:Layout(); self:LayoutTabs()
    if self.scope == 0 then self:SkinBagButtons() end
    for _, b in ipairs(self.buttons) do b.sig = nil end
    if frame:IsShown() then
        frame:SetScale(math.min(settings.scale, (UIParent:GetHeight() - 35) / self.h,
            (UIParent:GetWidth() - 35) / self.w))
    end
    self:Render()
end

function Inv:ResetGeometry()
    self.w, self.h, self.cap = WIDTH, HEIGHT, nil
    self.frame:ClearAllPoints()
    if self.scope == 0 then self.frame:SetPoint("CENTER")
    else self.frame:SetPoint("TOPRIGHT", Windows[0].frame, "TOPLEFT", -10, 0) end
end

local function resetAll()
    local d = db()
    for key, value in pairs(DEFAULTS) do d[key] = value end
    d.hiddenCategories, d.hiddenSorts = {}, {}
    for _, keys in pairs(GEOMETRY) do d[keys[1]], d[keys[2]] = nil, nil end
    for scope = 0, 2 do if Windows[scope] then Windows[scope]:ResetGeometry() end end
    applyAll()
    if Windows[0].refreshOptions then Windows[0].refreshOptions() end
end

function Inv:Create()
    local I = self
    local frame = CreateFrame("Frame", self.prefix .. "Frame", UIParent)
    I.frame = frame
    local settings = db()
    local sizeKey, positionKey = GEOMETRY[self.scope][1], GEOMETRY[self.scope][2]
    local size = settings[sizeKey]
    I.w = size and tonumber(size[1]) and math.max(MIN_W, size[1]) or WIDTH
    I.h = size and tonumber(size[2]) and math.max(MIN_H, size[2]) or HEIGHT
    if settings.remember and self.scope == 0 then I.category, I.sort = settings.lastCategory or 0, settings.lastSort or 0 end
    frame:Hide(); frame:SetSize(I.w, I.h); frame:SetPoint("CENTER")
    frame:SetFrameStrata("HIGH"); frame:SetToplevel(true); frame:SetMovable(true)
    frame:EnableMouse(true); frame:SetClampedToScreen(true); frame:RegisterForDrag("LeftButton")
    frame:SetBackdrop(dialog)
    frame:SetScript("OnDragStart", function(f) if not db().lockPosition then f:StartMoving() end end)
    frame:SetScript("OnDragStop", function(f)
        f:StopMovingOrSizing()
        local point, _, relativePoint, x, y = f:GetPoint()
        EsteriaInventoryDB[positionKey] = { point, relativePoint, x, y }
    end)
    frame:SetScript("OnShow", function(f)
        f:SetScale(math.min(db().scale, (UIParent:GetHeight() - 35) / I.h, (UIParent:GetWidth() - 35) / I.w))
        sound("igBackPackOpen")
    end)
    frame:SetScript("OnHide", function()
        GameTooltip:Hide(); sound("igBackPackClose")
        -- Closing the banker's window ends the bank interaction like closing the stock bank frame.
        if I.scope == 1 and State.atBanker and CloseBankFrame then CloseBankFrame() end
        if I.scope == 2 and State.atGuildBank then
            State.atGuildBank = false
            if CloseGuildBankFrame then CloseGuildBankFrame() end
        end
    end)
    frame:EnableMouseWheel(true)
    frame:SetScript("OnMouseWheel", function(_, delta)
        local page = I.page or DEFAULT_PAGE
        local offset = math.max(0, (I.targetOffset or I.snapshot.offset or 0) - delta * page)
        if offset < math.max(page, I.snapshot.total or 0) then I:Page(offset) end
    end)
    local header = frame:CreateTexture(nil, "ARTWORK")
    header:SetTexture("Interface\\DialogFrame\\UI-DialogBox-Header")
    header:SetSize(300, 64); header:SetPoint("TOP", 0, 12)
    frame.title = text(frame, "GameFontNormal", self.scope == 0 and "Esteria Inventory" or "Esteria Bank", "TOP", 0, -2)
    frame.capacity = text(frame, "GameFontHighlightSmall", "Connecting...", "TOPRIGHT", -44, -22)
    local close = CreateFrame("Button", nil, frame, "UIPanelCloseButton")
    close:SetPoint("TOPRIGHT", -6, -6)
    local search = CreateFrame("EditBox", self.prefix .. "Search", frame, "InputBoxTemplate")
    search:SetPoint("TOPLEFT", 26, -52); search:SetPoint("TOPRIGHT", -26, -52); search:SetHeight(24)
    search:SetAutoFocus(false); search:SetMaxLetters(80)
    search:SetTextInsets(22, 8, 0, 0)
    local magnifier = search:CreateTexture(nil, "OVERLAY")
    magnifier:SetTexture("Interface\\Common\\UI-Searchbox-Icon")
    magnifier:SetSize(14, 14); magnifier:SetPoint("LEFT", 4, -1)
    local placeholder = text(search, "GameFontDisable", "Search inventory...", "LEFT", 22, 0)
    search:SetScript("OnTextChanged", function(box)
        local value = box:GetText()
        if value == "" then placeholder:Show() else placeholder:Hide() end
        I.search, I.searchAt = value, GetTime() + 0.12
    end)
    search:SetScript("OnEscapePressed", function(box) box:ClearFocus(); frame:Hide() end)
    search:SetScript("OnEnterPressed", function(box) box:ClearFocus(); I.searchAt = GetTime() end)
    frame.tabs = {}
    for category = 0, 5 do
        frame.tabs[category] = button(frame, nil, categories[category + 1], tabWidths[category + 1],
            "TOPLEFT", 26, -99, function()
                I.category = category
                if I.scope == 0 then settings.lastCategory = category end
                I:Render(); I:Page(0)
            end)
    end
    if self.scope == 0 then
        frame.bankTab = button(frame, nil, "Bank", 64, "TOPLEFT", 26, -99, function()
            I:SetView(I.view == 1 and 0 or 1)
        end)
    end
    local bar = CreateFrame("StatusBar", nil, frame)
    bar:SetHeight(12); bar:SetPoint("BOTTOMLEFT", 26, 76); bar:SetPoint("BOTTOMRIGHT", -100, 76)
    skin(bar, 0.1, 0.1, 0.1)
    bar:SetStatusBarTexture("Interface\\TargetingFrame\\UI-StatusBar")
    bar:SetStatusBarColor(1, 0.82, 0); bar:SetMinMaxValues(0, 1); bar:SetValue(0)
    frame.bar = bar
    frame.percent = text(frame, "GameFontNormalSmall", "0.0% full", "BOTTOMRIGHT", -26, 76)
    frame.sort = button(frame, nil, "Sort: Name", 95, "BOTTOMLEFT", 26, 46, function()
        for step = 1, #sorts do
            local next = (I.sort + step) % #sorts
            if not settings.hiddenSorts[next + 1] and not (I.scope == 2 and next == 4) then I.sort = next; break end
        end
        if I.scope == 0 then settings.lastSort = I.sort end
        I:Render(); I:Page(0)
    end)
    local guildWindow = self.scope == 2
    frame.depositAll = button(frame, nil, guildWindow and "Deposit Gold" or "Deposit All", guildWindow and 100 or 92,
        "BOTTOMLEFT", 126, 46, function()
            if guildWindow then StaticPopup_Show("GUILDBANK_DEPOSIT") else I:Request(ACTION.depositAll) end
        end)
    button(frame, nil, guildWindow and "Withdraw" or "Stack", guildWindow and 74 or 52, "BOTTOMLEFT",
        guildWindow and 230 or 224, 46, function()
            if guildWindow then StaticPopup_Show("GUILDBANK_WITHDRAW") else I:Request(ACTION.merge) end
        end)
    button(frame, nil, "<", 25, "BOTTOMLEFT", 303, 46, function()
        I:Page(math.max(0, (I.targetOffset or I.snapshot.offset or 0) - I.page))
    end)
    frame.page = text(frame, "GameFontDisableSmall", "1 / 1", "BOTTOMLEFT", 334, 54)
    button(frame, nil, ">", 25, "BOTTOMLEFT", 400, 46, function()
        local offset = (I.targetOffset or I.snapshot.offset or 0) + I.page
        if offset < (I.snapshot.total or 0) then I:Page(offset) end
    end)
    frame.expand = button(frame, nil, "Expand Storage", 130, "BOTTOMRIGHT", -26, 46, function()
        if I.scope == 2 then I:GuildExpandPrompt(); return end
        if I.pending or not I.snapshot.upgrade or I.snapshot.upgrade == 0 then return end
        I.expandOffer = { price = I.snapshot.price, upgrade = I.snapshot.upgrade, scope = I.view }
        popupWindow = I
        StaticPopup_Show("ESTERIA_INVENTORY_EXPAND", I.expandOffer.upgrade, GetCoinTextureString(I.expandOffer.price))
    end)
    frame.gold = text(frame, "GameFontHighlightSmall", "", "BOTTOMRIGHT", -26, 96)
    frame.currency = text(frame, "GameFontHighlightSmall", "", "BOTTOMLEFT", 26, 96)
    frame.status = text(frame, "GameFontDisableSmall", "Connecting to digital inventory...", "BOTTOMLEFT", 26, 12)
    frame.status:SetWidth(I.w - 52); frame.status:SetHeight(32); frame.status:SetJustifyH("LEFT")
    frame.status:SetJustifyV("TOP")
    -- Covers the grid while the remote bank is locked and not standing at a banker.
    local lock = CreateFrame("Frame", nil, frame)
    lock:SetPoint("TOPLEFT", 20, -130); lock:SetPoint("BOTTOMRIGHT", -20, 118)
    lock:SetBackdrop({ bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background" })
    lock:SetFrameLevel((frame:GetFrameLevel() or 1) + 40); lock:EnableMouse(true); lock:Hide()
    text(lock, "GameFontNormalLarge", "Remote bank access is locked", "CENTER", 0, 34)
    text(lock, "GameFontHighlightSmall", "Visit a banker for free access, or unlock it for your whole account.", "CENTER", 0, 8)
    button(lock, nil, "Unlock Bank", 160, "CENTER", 0, -26, function()
        popupWindow = I
        StaticPopup_Show("ESTERIA_INVENTORY_UNLOCK", GetCoinTextureString(State.cost))
    end)
    frame.lock = lock
    if self.scope == 2 then self:BuildGuildExtras() end
    -- Corner grip: drag reshapes the window, Shift-drag scales it. Top-left corner stays pinned.
    local grip = CreateFrame("Button", nil, frame)
    frame.grip = grip
    grip:SetSize(16, 16); grip:SetPoint("BOTTOMRIGHT", -8, 8)
    grip:SetNormalTexture("Interface\\ChatFrame\\UI-ChatIM-SizeGrabber-Up")
    grip:SetPushedTexture("Interface\\ChatFrame\\UI-ChatIM-SizeGrabber-Down")
    grip:SetHighlightTexture("Interface\\ChatFrame\\UI-ChatIM-SizeGrabber-Highlight")
    grip:SetScript("OnEnter", function(g)
        GameTooltip:SetOwner(g, "ANCHOR_RIGHT")
        GameTooltip:SetText("Drag: resize")
        GameTooltip:AddLine("Shift-drag: scale", 0.8, 0.8, 0.8)
        GameTooltip:Show()
    end)
    grip:SetScript("OnLeave", function() GameTooltip:Hide() end)
    grip:SetScript("OnMouseDown", function(g)
        if db().lockSize then return end
        local scale, shift = frame:GetScale(), IsShiftKeyDown()
        local left, top = frame:GetLeft() * scale, frame:GetTop() * scale
        frame:ClearAllPoints()
        frame:SetPoint("TOPLEFT", UIParent, "BOTTOMLEFT", left / scale, top / scale)
        g:SetScript("OnUpdate", function()
            local ui = UIParent:GetEffectiveScale()
            local x, y = GetCursorPosition()
            x, y = x / ui, y / ui
            if shift then
                local wanted = math.max((x - left) / I.w, (top - y) / I.h)
                wanted = math.max(0.5, math.min(wanted, 2, (UIParent:GetHeight() - 35) / I.h,
                    (UIParent:GetWidth() - 35) / I.w))
                frame:SetScale(wanted)
                frame:ClearAllPoints()
                frame:SetPoint("TOPLEFT", UIParent, "BOTTOMLEFT", left / wanted, top / wanted)
            else
                I.w = math.max(MIN_W, math.min((x - left) / scale, (UIParent:GetWidth() - left) / scale))
                I.h = math.max(MIN_H, math.min((top - y) / scale, top / scale))
                I:Layout()
            end
        end)
    end)
    grip:SetScript("OnMouseUp", function(g)
        g:SetScript("OnUpdate", nil)
        if db().lockSize then return end
        EsteriaInventoryDB.scale = frame:GetScale()
        EsteriaInventoryDB[sizeKey] = { I.w, I.h }
        local point, _, relativePoint, x, y = frame:GetPoint()
        EsteriaInventoryDB[positionKey] = { point, relativePoint, x, y }
        if Windows[0].refreshOptions then Windows[0].refreshOptions() end
    end)
    I:Apply()
    tinsert(UISpecialFrames, self.prefix .. "Frame")
end

local function createPopups()
    StaticPopupDialogs.ESTERIA_INVENTORY_TAG = {
        text = "Tag for %s (blank clears it):", button1 = ACCEPT, button2 = CANCEL, hasEditBox = 1, maxLetters = 24,
        OnShow = function(self)
            self.editBox:SetText(popupRow and popupRow.tag or ""); self.editBox:HighlightText()
        end,
        OnAccept = function(self)
            if popupRow then popupWindow:Request(ACTION.setTag, popupRow, nil, nil, self.editBox:GetText()) end
        end,
        EditBoxOnEnterPressed = function(self)
            local popup = self:GetParent()
            if popupRow then popupWindow:Request(ACTION.setTag, popupRow, nil, nil, self:GetText()) end
            popup:Hide()
        end,
        EditBoxOnEscapePressed = function(self) self:GetParent():Hide() end,
        timeout = 0, whileDead = 1, hideOnEscape = 1, preferredIndex = 3,
    }
    StaticPopupDialogs.ESTERIA_INVENTORY_DESTROY = {
        text = "Permanently destroy %s?", button1 = ACCEPT, button2 = CANCEL,
        OnAccept = function()
            local row = popupRow
            if row then popupWindow:Physical(row, function(bag, slot) PickupContainerItem(bag, slot); DeleteCursorItem() end) end
        end,
        timeout = 0, whileDead = 1, hideOnEscape = 1, showAlert = 1, preferredIndex = 3,
    }
    StaticPopupDialogs.ESTERIA_INVENTORY_EXPAND = {
        text = "Unlock %d more inventory positions for %s?", button1 = ACCEPT, button2 = CANCEL,
        OnAccept = function()
            local I = popupWindow
            if I and I.expandOffer then I:Request(ACTION.expand, nil, I.expandOffer.price, nil, nil, I.expandOffer.scope) end
        end,
        timeout = 0, whileDead = false,
        hideOnEscape = true, preferredIndex = 3,
    }
    local function iconIndex(icon)
        local wanted = icon and string.lower(icon:match("([^\\/]+)$") or "")
        local count = GetNumMacroItemIcons and GetNumMacroItemIcons() or 0
        if wanted and wanted ~= "" then
            for index = 1, count do
                local name = GetMacroItemIconInfo(index)
                if name and string.lower(name:match("([^\\/]+)$") or "") == wanted then return index end
            end
        end
        return 1
    end
    local function rename(text)
        local tab = GetCurrentGuildBankTab()
        local _, icon = GetGuildBankTabInfo(tab)
        text = text and text:gsub("^%s+", ""):gsub("%s+$", "") or ""
        if text ~= "" then
            SetGuildBankTabInfo(tab, text, iconIndex(icon))
            QueryGuildBankTab(tab)
        end
    end
    StaticPopupDialogs.ESTERIA_GUILD_RENAME = {
        text = "Rename this guild bank tab:", button1 = ACCEPT, button2 = CANCEL, hasEditBox = 1, maxLetters = 16,
        OnShow = function(self)
            local name = GetGuildBankTabInfo(GetCurrentGuildBankTab())
            self.editBox:SetText(name or ""); self.editBox:HighlightText()
        end,
        OnAccept = function(self) rename(self.editBox:GetText()) end,
        EditBoxOnEnterPressed = function(self) local popup = self:GetParent(); rename(self:GetText()); popup:Hide() end,
        EditBoxOnEscapePressed = function(self) self:GetParent():Hide() end,
        timeout = 0, whileDead = 1, hideOnEscape = 1, preferredIndex = 3,
    }
    StaticPopupDialogs.ESTERIA_GUILD_EXPAND = {
        text = "Add %d slots to this guild bank tab for %s?", button1 = ACCEPT, button2 = CANCEL,
        OnAccept = function()
            local I = popupWindow
            local tab = GetCurrentGuildBankTab() or 1
            local info = I and I.meta and I.meta.tabs[tab - 1]
            if info then I:GuildMeta(ACTION.expand, info.expandPrice, tab - 1) end
        end,
        timeout = 0, whileDead = false, hideOnEscape = true, preferredIndex = 3,
    }
    StaticPopupDialogs.ESTERIA_GUILD_BUYTAB = {
        text = "Buy a new guild bank tab for %s?", button1 = ACCEPT, button2 = CANCEL,
        OnAccept = function() BuyGuildBankTab() end,
        timeout = 0, whileDead = false, hideOnEscape = true, preferredIndex = 3,
    }
    StaticPopupDialogs.ESTERIA_INVENTORY_UNLOCK = {
        text = "Unlock remote bank access for every character on your account for %s?",
        button1 = ACCEPT, button2 = CANCEL,
        OnAccept = function()
            if popupWindow then popupWindow:Request(ACTION.unlock, nil, State.cost, nil, nil, 1) end
        end,
        timeout = 0, whileDead = false, hideOnEscape = true, preferredIndex = 3,
    }
end

-- Interface Options panel (also /inventory config).
local function buildOptions()
    local carried = Windows[0]
    local panel = CreateFrame("Frame", "EsteriaInventoryOptions", UIParent)
    panel.name = "Esteria Inventory"
    local controls = {}
    local function label(control, value)
        local fontString = control:GetName() and _G[control:GetName() .. "Text"]
        if fontString then fontString:SetText(value) end
    end
    local function slider(key, title, low, high, step, x, y, percent)
        local s = CreateFrame("Slider", "EsteriaInventoryOpt" .. key, panel, "OptionsSliderTemplate")
        s:SetPoint("TOPLEFT", x, y); s:SetWidth(240)
        s:SetMinMaxValues(low, high); s:SetValueStep(step)
        local name = s:GetName()
        if _G[name .. "Low"] then _G[name .. "Low"]:SetText(percent and (low * 100) .. "%" or low) end
        if _G[name .. "High"] then _G[name .. "High"]:SetText(percent and (high * 100) .. "%" or high) end
        local function caption(value) label(s, title .. ": " .. (percent and math.floor(value * 100 + 0.5) .. "%" or value)) end
        s:SetScript("OnValueChanged", function(_, value)
            value = math.floor(value / step + 0.5) * step
            caption(value)
            if panel.loading then return end
            db()[key] = value
            applyAll()
        end)
        controls[#controls + 1] = function() s:SetValue(db()[key]); caption(db()[key]) end
    end
    local function check(title, x, y, get, set)
        local c = CreateFrame("CheckButton", "EsteriaInventoryChk" .. #controls, panel, "InterfaceOptionsCheckButtonTemplate")
        c:SetPoint("TOPLEFT", x, y)
        label(c, title)
        c:SetScript("OnClick", function(self)
            if panel.loading then return end
            set(self:GetChecked() and true or false)
            applyAll()
        end)
        controls[#controls + 1] = function() c:SetChecked(get()) end
    end
    local title = panel:CreateFontString(nil, "ARTWORK", "GameFontNormalLarge")
    title:SetPoint("TOPLEFT", 16, -16); title:SetText("Esteria Inventory")
    slider("tileScale", "Tile scale", 0.6, 2, 0.05, 20, -60, true)
    slider("gap", "Tile spacing", 0, 16, 1, 20, -110)
    slider("maxTiles", "Max tiles per page", 1, 255, 1, 20, -160)
    slider("scale", "Window scale", 0.5, 1.5, 0.05, 20, -210, true)
    slider("alpha", "Window opacity", 0.3, 1, 0.05, 20, -260, true)
    local function flag(key) return function() return db()[key] end, function(value) db()[key] = value end end
    local y = -50
    for _, option in ipairs({ { "lockPosition", "Lock window position" }, { "lockSize", "Lock window size and scale" },
        { "replaceBags", "Bag keys open this window" }, { "showNames", "Item names on large tiles" },
        { "qualityBorders", "Quality-coloured tile borders" }, { "sounds", "Open/close sounds" },
        { "hints", "Extra tooltip hints" }, { "remember", "Remember last category and sort" }, { "showGold", "Show gold" },
        { "contextMenu", "Right-click item menu (Equip, Disenchant, Destroy...)" },
        { "bagButton", "Single bag button (hide DragonUI bag bar)" },
        { "showBankTab", "Bank tab in the bag window" },
        { "guildBank", "Esteria guild bank window (reload to apply)" } }) do
        check(option[2], 300, y, flag(option[1]))
        y = y - 26
    end
    for category = 1, 5 do
        check("Show tab: " .. categories[category + 1], 20, -300 - (category - 1) * 24,
            function() return not db().hiddenCategories[category] end,
            function(value) db().hiddenCategories[category] = (not value) or nil end)
    end
    for s = 1, #sorts do
        check("Allow sort: " .. sorts[s], 300, -380 - (s - 1) * 24,
            function() return not db().hiddenSorts[s] end,
            function(value) db().hiddenSorts[s] = (not value) or nil end)
    end
    local reset = CreateFrame("Button", "EsteriaInventoryResetButton", panel, "UIPanelButtonTemplate")
    reset:SetSize(180, 26); reset:SetPoint("TOPLEFT", 20, -430); reset:SetText("Reset all to defaults")
    reset:SetScript("OnClick", resetAll)
    -- Currency picker: one checkbox per currency the character knows about (headers are expanded briefly).
    local picker = CreateFrame("Frame", "EsteriaInventoryCurrencies", UIParent)
    picker.name, picker.parent = "Currencies", panel.name
    local scroll = CreateFrame("ScrollFrame", "EsteriaInventoryCurrenciesScroll", picker, "UIPanelScrollFrameTemplate")
    scroll:SetPoint("TOPLEFT", 16, -50); scroll:SetPoint("BOTTOMRIGHT", -36, 16)
    local content = CreateFrame("Frame", nil, scroll)
    content:SetSize(560, 400); scroll:SetScrollChild(content)
    local pickerTitle = picker:CreateFontString(nil, "ARTWORK", "GameFontNormalLarge")
    pickerTitle:SetPoint("TOPLEFT", 16, -16); pickerTitle:SetText("Show currencies in the inventory window")
    local boxes = {}
    local function listCurrencies()
        local names, collapsed = {}, {}
        if not GetCurrencyListSize then return names end
        carried.scanning = true
        for index = GetCurrencyListSize(), 1, -1 do
            local _, header, expanded = GetCurrencyListInfo(index)
            if header and not expanded then collapsed[#collapsed + 1] = index; ExpandCurrencyList(index, 1) end
        end
        for index = 1, GetCurrencyListSize() do
            local name, header = GetCurrencyListInfo(index)
            if not header then names[#names + 1] = name end
        end
        for _, index in ipairs(collapsed) do ExpandCurrencyList(index, 0) end
        carried.scanning = false
        return names
    end
    picker:SetScript("OnShow", function()
        local names = listCurrencies()
        for index, name in ipairs(names) do
            local box = boxes[index]
            if not box then
                box = CreateFrame("CheckButton", "EsteriaInventoryCurrencyBox" .. index, content,
                    "InterfaceOptionsCheckButtonTemplate")
                box:SetPoint("TOPLEFT", 0, -(index - 1) * 24)
                box:SetScript("OnClick", function(self)
                    db().currencies[self.currency] = self:GetChecked() and true or nil
                    applyAll()
                end)
                boxes[index] = box
            end
            box.currency = name
            label(box, name)
            box:SetChecked(db().currencies[name] and true or false)
            box:Show()
        end
        for index = #names + 1, #boxes do boxes[index]:Hide() end
        content:SetHeight(math.max(40, #names * 24))
    end)
    if InterfaceOptions_AddCategory then InterfaceOptions_AddCategory(picker) end
    carried.refreshOptions = function()
        panel.loading = true
        for _, refresh in ipairs(controls) do refresh() end
        panel.loading = false
    end
    panel:SetScript("OnShow", carried.refreshOptions)
    panel.refresh = carried.refreshOptions
    panel.okay, panel.cancel, panel.default = function() end, function() end, resetAll
    if InterfaceOptions_AddCategory then InterfaceOptions_AddCategory(panel) end
    carried.options = panel
end

local guildClassOrder
-- Maps an item to the same coarse categories the server uses (1 equipment, 2 consumable, 3 material, 4 other).
local function guildCategory(link)
    local _, _, _, _, _, itemType, _, _, equipLoc = GetItemInfo(link)
    if not itemType then return 4 end
    guildClassOrder = guildClassOrder or { GetAuctionItemClasses() }
    local class
    for index, name in ipairs(guildClassOrder) do if name == itemType then class = index; break end end
    if class == 1 or class == 2 or (equipLoc and equipLoc ~= "") then return 1 end
    if class == 4 then return 2 end
    if class == 6 or class == 10 then return 3 end
    return 4
end

function Inv:GuildCapacity(tab)
    local info = self.meta and self.meta.tabs[tab - 1]
    return info and info.capacity or 128
end

-- Scope-2 request: tab sizes and prices, or (action expand) buy more slots for a tab.
function Inv:GuildMeta(action, count, tab)
    if not EsteriaInventory_Request or not Windows[0].serverReady then return end
    EsteriaInventory_Request(action or ACTION.browse, 1, 0, count or 0, tab or 0, 0, 0, "", nil, 2)
end

function Inv:GuildMetaDeliver(snapshot)
    local meta = { tabs = {}, nextTabPrice = snapshot.price or 0 }
    for _, row in ipairs(snapshot.rows) do
        meta.tabs[row.guid] = { capacity = row.entry, expandPrice = row.count, step = row.durability,
            max = row.maxDurability, name = row.name }
    end
    self.meta = meta
    self:GuildRefresh()
    if snapshot.message ~= "" then self.frame.status:SetText(snapshot.message) end
end

local function guildAgo(years, months, days, hours)
    if (years or 0) > 0 then return years .. "y ago" end
    if (months or 0) > 0 then return months .. "mo ago" end
    if (days or 0) > 0 then return days .. "d ago" end
    if (hours or 0) > 0 then return hours .. "h ago" end
    return "just now"
end

-- Item log, money log and tab info all come from the stock guild bank API.
function Inv:RefreshPanel()
    local frame = self.frame
    local panel, mode = frame.logPanel, self.mode or "items"
    if type(panel) ~= "table" then return end
    if mode == "items" then panel:Hide(); return end
    panel:Show()
    local tab = GetCurrentGuildBankTab and GetCurrentGuildBankTab() or 1
    if mode == "info" then
        panel.scroll:Hide(); panel.edit:Show()
        local canEdit = (CanEditGuildTabInfo and CanEditGuildTabInfo(tab)) or (IsGuildLeader and IsGuildLeader())
        if not panel.edit:HasFocus() then panel.edit:SetText(GetGuildBankText and GetGuildBankText(tab) or "") end
        panel.edit:EnableMouse(canEdit and true or false)
        if canEdit then panel.save:Show() else panel.save:Hide() end
        return
    end
    panel.edit:Hide(); panel.save:Hide(); panel.scroll:Show()
    local lines = {}
    if mode == "log" then
        for index = (GetNumGuildBankTransactions(tab) or 0), 1, -1 do
            local kind, name, link, count, tab1, tab2, years, months, days, hours = GetGuildBankTransaction(tab, index)
            local what = (link or "?") .. ((count or 1) > 1 and (" x" .. count) or "")
            local line = (name or "?") .. (kind == "deposit" and " deposited " or kind == "withdraw" and " withdrew "
                or " moved ") .. what
            if kind == "move" then line = line .. " (tab " .. (tab1 or 0) .. " > " .. (tab2 or 0) .. ")" end
            lines[#lines + 1] = "|cff999999" .. guildAgo(years, months, days, hours) .. "|r  " .. line
        end
    else
        local verbs = { deposit = " deposited ", withdraw = " withdrew ", repair = " spent on repairs: ",
            withdrawForTab = " withdrew for a new tab: ", buyTab = " bought a tab for ", depositSummary = " total deposits: " }
        for index = (GetNumGuildBankMoneyTransactions and GetNumGuildBankMoneyTransactions() or 0), 1, -1 do
            local kind, name, amount, years, months, days, hours = GetGuildBankMoneyTransaction(index)
            lines[#lines + 1] = "|cff999999" .. guildAgo(years, months, days, hours) .. "|r  " .. (name or "?")
                .. (verbs[kind] or (" " .. tostring(kind) .. " ")) .. GetCoinTextureString(amount or 0)
        end
    end
    if #lines == 0 then lines[1] = "|cff999999Nothing logged yet.|r" end
    local width = math.max(200, (panel.scroll:GetWidth() or 520) - 8)
    panel.content:SetWidth(width); panel.logText:SetWidth(width - 8)
    panel.logText:SetText(table.concat(lines, "\n"))
    panel.content:SetHeight(math.max(20, (panel.logText:GetHeight() or 20) + 8))
end

function Inv:SetMode(mode)
    self.mode = mode
    local tab = GetCurrentGuildBankTab and GetCurrentGuildBankTab() or 1
    for key, b in pairs(self.frame.modeButtons) do
        if key == mode then b:LockHighlight() else b:UnlockHighlight() end
    end
    if mode == "log" and QueryGuildBankLog then QueryGuildBankLog(tab)
    elseif mode == "money" and QueryGuildBankLog then QueryGuildBankLog(7)
    elseif mode == "info" and QueryGuildBankText then QueryGuildBankText(tab) end
    self:RefreshPanel()
end

function Inv:GuildExpandPrompt()
    local tab = GetCurrentGuildBankTab() or 1
    local info = self.meta and self.meta.tabs[tab - 1]
    if not info or not self.canExpand then return end
    popupWindow = self
    StaticPopup_Show("ESTERIA_GUILD_EXPAND", info.step, GetCoinTextureString(info.expandPrice))
end

function Inv:GuildBuyPrompt()
    local price = self.meta and self.meta.nextTabPrice or 0
    if price == 0 and GetGuildBankTabCost then price = GetGuildBankTabCost() end
    StaticPopup_Show("ESTERIA_GUILD_BUYTAB", GetCoinTextureString(price))
end

function Inv:BuildGuildExtras()
    local I, frame = self, self.frame
    frame.modeButtons = {}
    local x = 26
    for _, mode in ipairs({ { "items", "Items", 64 }, { "log", "Log", 56 }, { "money", "Money Log", 92 },
        { "info", "Info", 56 } }) do
        frame.modeButtons[mode[1]] = button(frame, nil, mode[2], mode[3], "TOPLEFT", x, -127, function()
            I:SetMode(mode[1])
        end)
        x = x + mode[3] + 5
    end
    frame.rename = button(frame, nil, "Rename Tab", 100, "TOPRIGHT", -26, -127, function()
        popupWindow = I
        StaticPopup_Show("ESTERIA_GUILD_RENAME")
    end)
    frame.buyTab = button(frame, nil, "Buy Tab", 84, "TOPLEFT", 26, -99, function() I:GuildBuyPrompt() end)
    frame.buyTab:Hide()
    local panel = CreateFrame("Frame", nil, frame)
    panel:SetPoint("TOPLEFT", 20, -158); panel:SetPoint("BOTTOMRIGHT", -20, 118)
    skin(panel, 0.05, 0.05, 0.05)
    panel:SetFrameLevel((frame:GetFrameLevel() or 1) + 30); panel:EnableMouse(true); panel:Hide()
    local scroll = CreateFrame("ScrollFrame", self.prefix .. "LogScroll", panel, "UIPanelScrollFrameTemplate")
    scroll:SetPoint("TOPLEFT", 8, -8); scroll:SetPoint("BOTTOMRIGHT", -28, 8)
    local content = CreateFrame("Frame", nil, scroll)
    content:SetSize(500, 20); scroll:SetScrollChild(content)
    local logText = content:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    logText:SetPoint("TOPLEFT", 4, -4); logText:SetWidth(490); logText:SetJustifyH("LEFT")
    local edit = CreateFrame("EditBox", nil, panel)
    edit:SetMultiLine(true); edit:SetMaxLetters(500); edit:SetFontObject("GameFontHighlight"); edit:SetAutoFocus(false)
    edit:SetPoint("TOPLEFT", 12, -12); edit:SetPoint("BOTTOMRIGHT", -12, 44); edit:Hide()
    edit:SetScript("OnEscapePressed", function(box) box:ClearFocus() end)
    local save = button(panel, nil, "Save Info", 100, "BOTTOMRIGHT", -12, 10, function()
        SetGuildBankText(GetCurrentGuildBankTab(), edit:GetText()); edit:ClearFocus()
    end)
    save:Hide()
    panel.scroll, panel.content, panel.logText, panel.edit, panel.save = scroll, content, logText, edit, save
    frame.logPanel = panel
end

function Inv:GuildOpen()
    self.mode = "items"
    local tabs = GetNumGuildBankTabs and GetNumGuildBankTabs() or 0
    if tabs > 0 and (GetCurrentGuildBankTab() or 0) < 1 then SetCurrentGuildBankTab(1) end
    self.active = true
    if tabs > 0 then QueryGuildBankTab(GetCurrentGuildBankTab()) end
    self:GuildMeta()
    self:GuildRefresh(0)
    if type(self.frame.modeButtons) == "table" then self:SetMode("items") end
end

-- Rebuilds the visible page from the stock guild bank API: search, sort and paging run on the client.
function Inv:GuildRefresh(offset)
    if not GetCurrentGuildBankTab then return end
    local tab = GetCurrentGuildBankTab() or 1
    local numTabs = GetNumGuildBankTabs() or 0
    local name, _, viewable, canDeposit, _, remaining
    if numTabs > 0 then name, _, viewable, canDeposit, _, remaining = GetGuildBankTabInfo(tab) end
    local all, filtered, free = 0, {}, nil
    local needle = string.lower(self.search or "")
    if viewable then
        for slot = 1, self:GuildCapacity(tab) do
            local link = GetGuildBankItemLink(tab, slot)
            if link then
                all = all + 1
                local texture, count, locked = GetGuildBankItemInfo(tab, slot)
                local itemName = link:match("%[(.-)%]") or "?"
                local entry = tonumber(link:match("item:(%d+)")) or 0
                local quality = 1
                for q = 0, 7 do
                    local _, _, _, hex = GetItemQualityColor(q)
                    if hex and link:find("|c" .. hex, 1, true) then quality = q; break end
                end
                local equipLoc = select(9, GetItemInfo(link))
                if needle == "" or string.find(string.lower(itemName), needle, 1, true)
                    or tostring(entry):find(needle, 1, true) then
                    filtered[#filtered + 1] = { guid = tab * 1000 + slot, entry = entry, count = count or 1,
                        digital = true, bag = 0, slot = 0, quality = quality, category = guildCategory(link),
                        inventoryType = (equipLoc and equipLoc ~= "") and 1 or 0, durability = 0, maxDurability = 0,
                        favorite = false, bound = false, split = false, tag = "", name = itemName, icon = "",
                        texture = texture, locked = locked, link = link:match("|H(item:[^|]+)|h") or ("item:" .. entry),
                        guild = { tab = tab, slot = slot } }
                end
            elseif not free then free = slot end
        end
    end
    local sort = self.sort
    table.sort(filtered, function(a, b)
        if sort == 1 and a.quality ~= b.quality then return a.quality > b.quality end
        if sort == 2 and a.count ~= b.count then return a.count > b.count end
        if sort == 3 and a.category ~= b.category then return a.category < b.category end
        if a.name ~= b.name then return a.name < b.name end
        return a.guid < b.guid
    end)
    local page, total = self.page or DEFAULT_PAGE, #filtered
    offset = offset or self.targetOffset or self.snapshot.offset or 0
    offset = total > 0 and math.min(math.floor(offset / page), math.floor((total - 1) / page)) * page or 0
    local rows = {}
    for index = offset + 1, math.min(total, offset + page) do rows[#rows + 1] = filtered[index] end
    local message = ""
    if numTabs == 0 then message = "This guild has no bank tabs."
    elseif not viewable then message = "You do not have permission to view this tab."
    elseif not canDeposit then message = "You can withdraw from this tab but not deposit." end
    self.rev = (self.rev or 0) + 1
    self.snapshot = { rows = rows, total = total, offset = offset, used = all, capacity = self:GuildCapacity(tab), upgrade = 0, price = 0,
        revision = self.rev, request = 0, active = true, status = 0, message = message }
    self.snaps[2] = self.snapshot
    self.targetOffset, self.guildFree, self.guildTab, self.guildTabName = nil, free, tab, name
    local unlimited = remaining == nil or remaining < 0 or remaining >= 2147483647
    self.guildInfo = numTabs > 0 and viewable and (unlimited and "Unlimited withdrawals"
        or (remaining .. " withdrawals left today")) or ""
    self.canBuyTab = numTabs < 6 and IsGuildLeader and IsGuildLeader() and true or false
    local meta = self.meta and self.meta.tabs[tab - 1]
    self.canExpand = IsGuildLeader and IsGuildLeader() and meta and meta.capacity < meta.max and true or false
    if type(self.frame.rename) == "table" then
        if IsGuildLeader and IsGuildLeader() and numTabs > 0 then self.frame.rename:Show() else self.frame.rename:Hide() end
    end
    self:LayoutGuildTabs()
    self:Render()
    self:RefreshPanel()
    self.frame.status:SetText(message ~= "" and message
        or "Left-click to pick up or drop. Right-click to withdraw. Shift-click to split.")
end

-- Delivers one server snapshot (scope 0 carried, 1 bank) to the window(s) that show that storage.
function Inv:Deliver(snapshot, scope)
    self.snaps[scope] = snapshot
    if self.pending and snapshot.request == self.pending.id then self.pending = nil end
    if scope == 0 then self.active = snapshot.active end
    if self.view == scope then
        self.snapshot = snapshot
        self.targetOffset = nil
        -- A server without variable pages always answers with 20 rows; stop asking for more.
        local rows = #snapshot.rows
        if rows == DEFAULT_PAGE and self.page > DEFAULT_PAGE and snapshot.offset + rows < snapshot.total then
            self.cap = DEFAULT_PAGE
            self:Layout()
        end
    end
    if scope == 0 and self.scope == 0 and snapshot.active then
        self:InstallHooks()
        if enabled() and EsteriaSatchel and EsteriaSatchel.frames then
            for bag = 1, 4 do
                if EsteriaSatchel.frames[bag] then EsteriaSatchel.frames[bag]:Hide() end
            end
        end
    end
    self:Render()
    if self.scope == 0 and scope == 0 then
        self:RunFollowUp()
        self:SkinBagButtons()
    end
    if self.view == scope then
        self.frame.status:SetText(snapshot.message ~= "" and snapshot.message
            or (scope == 1 and "Left-click to move to your bags. Right-click a bag item to send it here."
                or "Left-click to withdraw. Right-click to use or equip. Alt-click to favorite."))
    end
    self:Flush()
end

local events = CreateFrame("Frame")
events:RegisterEvent("PLAYER_LOGIN"); events:RegisterEvent("PLAYER_LEAVING_WORLD")
events:RegisterEvent("PLAYER_ENTERING_WORLD")
events:RegisterEvent("CHAT_MSG_ADDON")
events:RegisterEvent("BAG_UPDATE"); events:RegisterEvent("ITEM_LOCK_CHANGED")
events:RegisterEvent("BAG_UPDATE_COOLDOWN")
events:RegisterEvent("PLAYER_MONEY"); events:RegisterEvent("CURRENCY_DISPLAY_UPDATE")
events:RegisterEvent("BANKFRAME_OPENED"); events:RegisterEvent("BANKFRAME_CLOSED")
events:RegisterEvent("GUILDBANKFRAME_OPENED"); events:RegisterEvent("GUILDBANKFRAME_CLOSED")
events:RegisterEvent("GUILDBANKBAGSLOTS_CHANGED"); events:RegisterEvent("GUILDBANK_UPDATE_TABS")
events:RegisterEvent("GUILDBANK_UPDATE_MONEY"); events:RegisterEvent("GUILDBANK_UPDATE_WITHDRAWMONEY")
events:RegisterEvent("GUILDBANKLOG_UPDATE"); events:RegisterEvent("GUILDBANK_TEXT_CHANGED")
events:SetScript("OnEvent", function(_, event, arg1, arg2, arg3, arg4)
    local I = Windows[0]
    if event ~= "PLAYER_LOGIN" and not I then return end
    if event == "PLAYER_LOGIN" then
        EsteriaInventoryDB = EsteriaInventoryDB or { replaceBags = true }
        I = newWindow(0); newWindow(1); newWindow(2)
        EsteriaInventory = I
        I:Create(); Windows[1]:Create(); Windows[2]:Create()
        createPopups()
        buildOptions()
        for scope = 0, 2 do
            local w, key = Windows[scope], GEOMETRY[scope][2]
            local p = EsteriaInventoryDB[key]
            if p then w.frame:ClearAllPoints(); w.frame:SetPoint(p[1], UIParent, p[2], p[3], p[4])
            elseif scope >= 1 then w:ResetGeometry() end
        end
        -- Our guild bank window replaces the stock one: load it now and stop it from ever opening.
        silenceStockGuildBank()
        -- The stock bank frame is replaced by our banker window once digital storage is active.
        if BankFrame and EsteriaInventory_Request then BankFrame:UnregisterEvent("BANKFRAME_OPENED") end
        if EsteriaInventory_Request then
            events:RegisterEvent("ESTERIA_INVENTORY_UPDATE")
            I.activateAt = GetTime() + 2
        else
            I.frame.status:SetText("The matched Esteria native client is required. Normal bags remain available.")
            if BankFrame then BankFrame:RegisterEvent("BANKFRAME_OPENED") end
        end
    elseif event == "PLAYER_LEAVING_WORLD" then
        for scope = 0, 2 do
            local w = Windows[scope]
            if w then w.active, w.pending, w.cap = false, nil, nil; w.snaps[0].active, w.snaps[1].active = false, false end
        end
        State.unlocked, State.atBanker, State.atGuildBank = false, false, false
        I.serverReady, I.nextDiscovery, I.discoveryDeadline, I.activateAt = false, nil, nil, nil
        if I.frame then I.frame:Hide(); Windows[1].frame:Hide(); Windows[2].frame:Hide() end
    elseif event == "PLAYER_ENTERING_WORLD" then
        if I.frame and EsteriaInventory_Request then I.activateAt = GetTime() + 2 end
    elseif event == "CHAT_MSG_ADDON" then
        local prefix, message, channel, sender = arg1, arg2, arg3, arg4
        if I.frame and EsteriaInventory_Request and I.discoveryDeadline and GetTime() <= I.discoveryDeadline
            and prefix == "ESTERIADI" and message == "READY:1" and channel == "WHISPER"
            and sender == UnitName("player") then
            I.serverReady, I.discoveryDeadline, I.activateAt = true, nil, nil
            I:Request(ACTION.activate)
        end
    elseif event == "BANKFRAME_OPENED" then
        -- Only reachable once our window owns the bank (see PLAYER_LOGIN); stock frame stays closed.
        if I.frame and I.active then
            State.atBanker = true
            Windows[1]:Show()
            if not I.frame:IsShown() then I:Show() end
        elseif BankFrame then
            ShowUIPanel(BankFrame)
        end
    elseif event == "GUILDBANKFRAME_OPENED" then
        if db().guildBank and I.frame then
            silenceStockGuildBank()
            I.silenceUntil = GetTime() + 2
            State.atGuildBank = true
            Windows[2]:Show()
            if not I.frame:IsShown() and I.active then I:Show() end
        end
    elseif event == "GUILDBANKFRAME_CLOSED" then
        State.atGuildBank = false
        if Windows[2] and Windows[2].frame:IsShown() then Windows[2].frame:Hide() end
    elseif event == "GUILDBANKBAGSLOTS_CHANGED" or event == "GUILDBANK_UPDATE_TABS"
        or event == "GUILDBANK_UPDATE_MONEY" or event == "GUILDBANK_UPDATE_WITHDRAWMONEY" then
        if Windows[2] and Windows[2].frame:IsShown() then Windows[2]:GuildRefresh() end
    elseif event == "GUILDBANKLOG_UPDATE" or event == "GUILDBANK_TEXT_CHANGED" then
        if Windows[2] and Windows[2].frame:IsShown() then Windows[2]:RefreshPanel() end
    elseif event == "BANKFRAME_CLOSED" then
        State.atBanker = false
        if Windows[1] and Windows[1].frame:IsShown() then Windows[1].frame:Hide() end
        applyAll()
    elseif event == "ESTERIA_INVENTORY_UPDATE" then
        local scope = tonumber(arg1) or 0
        if scope > 2 then scope = 0 end
        local snapshot = EsteriaInventory_GetSnapshot(scope)
        if scope == 2 then
            Windows[2]:GuildMetaDeliver(snapshot)
            return
        end
        State.unlocked, State.atBanker, State.cost = snapshot.bankUnlocked, snapshot.atBanker, snapshot.unlockCost or 0
        if scope == 0 then
            I:Deliver(snapshot, 0)
            if Windows[1] then Windows[1].snaps[0] = snapshot end
        else
            I:Deliver(snapshot, 1)
            Windows[1]:Deliver(snapshot, 1)
        end
    elseif event == "PLAYER_MONEY" or event == "CURRENCY_DISPLAY_UPDATE" then
        if I.frame and not I.scanning then
            if I.frame:IsShown() then I:UpdateMoney() end
            if Windows[1].frame:IsShown() then Windows[1]:UpdateMoney() end
            if Windows[2].frame:IsShown() then Windows[2]:UpdateMoney() end
        end
    elseif event == "ITEM_LOCK_CHANGED" and Windows[2] and Windows[2].frame:IsShown() and not (I.active and I.frame:IsShown()) then
        Windows[2]:GuildRefresh()
    elseif I.frame and I.active and I.frame:IsShown() then
        -- Lock/cooldown changes are local; only bag changes need a server refresh.
        if event == "BAG_UPDATE" then
            if I.view == 0 then I.refreshAt = GetTime() + 0.1 end
            I:RunFollowUp()
        else I:Render() end
    end
end)
events:SetScript("OnUpdate", function()
    local now = GetTime()
    local I = Windows[0]
    if I.active and now >= (I.skinAt or 0) then I.skinAt = now + 1; I:SkinBagButtons() end
    if I.silenceUntil and now < I.silenceUntil then silenceStockGuildBank() end
    for scope = 0, 2 do
        local w = Windows[scope]
        if w.pending and now - w.pending.started > 8 then
            w.pending = nil
            w.frame.status:SetText("No reply received. Open inventory to refresh; the action has not been repeated.")
            w:Flush()
        end
    end
    if I.discoveryDeadline and now >= I.discoveryDeadline then
        I.discoveryDeadline = nil
        I.frame.status:SetText("This realm has not advertised digital inventory support. Normal bags remain available.")
    end
    if I.activateAt and now >= I.activateAt then
        I.activateAt = nil; I:Request(ACTION.activate)
        return
    end
    for scope = 0, 2 do
        local w = Windows[scope]
        if w.searchAt and now >= w.searchAt then
            w.searchAt = nil; w:Page(0)
        elseif w.pageAt and now >= w.pageAt then
            -- Window or tile settings changed the page size; refetch from the first visible item.
            w.pageAt = nil
            if (w.active or scope >= 1) and w.frame:IsShown() then w:Page(w.snapshot.offset or 0) end
        elseif w.refreshAt and now >= w.refreshAt and not w.pending then
            w.refreshAt = nil
            if w.frame:IsShown() then w:Refresh() end
        end
    end
end)

SLASH_ESTERIAINVENTORY1, SLASH_ESTERIAINVENTORY2 = "/inventory", "/inv"
SlashCmdList.ESTERIAINVENTORY = function(message)
    local I = Windows[0]
    if not I or not I.frame then return end
    if message == "legacy" then
        EsteriaInventoryDB.replaceBags = not EsteriaInventoryDB.replaceBags
        I.frame:Hide()
        DEFAULT_CHAT_FRAME:AddMessage("Esteria Inventory bag replacement: "
            .. (EsteriaInventoryDB.replaceBags and "enabled" or "disabled"))
    elseif message == "config" or message == "options" then
        if InterfaceOptionsFrame_OpenToCategory and I.options then
            -- Called twice: the first call can open the wrong category on a fresh Interface Options frame.
            InterfaceOptionsFrame_OpenToCategory(I.options); InterfaceOptionsFrame_OpenToCategory(I.options)
        end
    elseif message == "bank" then
        I:Show(); I:SetView(1)
    elseif message == "reset" then
        resetAll()
    elseif tonumber(message) then
        EsteriaInventoryDB.scale = math.max(0.4, math.min(1.5, tonumber(message)))
        I.frame:Hide(); I:Show()
    else I:Toggle() end
end
