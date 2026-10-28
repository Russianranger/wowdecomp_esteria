-- Manastorm_Speedflying: the addon-message listener and the player-name cache.
-- Copyright (C) 2026 WarcraftXL
--
-- The engine delivers addon messages only to script, so this frame is the module's ear: it stashes
-- the last body into a global and src/SpeedFlying.cpp/TalentUI.cpp read those back. It is a frame
-- plus an event handler and no module state, so it lives here rather than in an injected chunk that
-- had to be rebuilt on every script context.
--
-- Writes (read back by the module):
--   Manastorm_MountAnimPlayer      the player name, off the live global -- the module names its recipients
--                            from this, so an absent name means no link at all
--   Manastorm_MountAnimServerMsg / ...Time    the last WXLMS "S:" speed announce
--   Manastorm_TalentBody           the last WXLTT tally ('T') or refusal ('F')
--   Manastorm_TalentBar            the last WXLTT bar op ('P') -- its own slot, see the module's comment
--   Manastorm_AbilityQueue         Esteria: WXLMS "A:<letter>" ability grants, appended until the module reads them

local f = CreateFrame('Frame', 'Manastorm_MountAnimLink', UIParent)
f:RegisterEvent('CHAT_MSG_ADDON')
if RegisterAddonMessagePrefix then
    RegisterAddonMessagePrefix('WXLMS')
    RegisterAddonMessagePrefix('WXLTT')
end

f:SetScript('OnEvent', function(_, event, prefix, msg)
    -- The name is re-read on every delivery and while it is still absent: UnitName answers nil for the
    -- first frames of a context, and latching that nil dropped every send for the whole session (the
    -- panel never saw a tally, so Confirm and Reset looked ignored) until a relog.
    local n = UnitName and UnitName('player')
    if type(n) == 'string' and n ~= '' then _G.Manastorm_MountAnimPlayer = n end

    if event ~= 'CHAT_MSG_ADDON' or type(msg) ~= 'string' then return end
    if prefix == 'WXLMS' and string.sub(msg, 1, 2) == 'S:' then
        _G.Manastorm_MountAnimServerMsg = msg
        _G.Manastorm_MountAnimServerMsgTime = GetTime()
    elseif prefix == 'WXLMS' and string.sub(msg, 1, 2) == 'A:' then
        -- Esteria: an ability the server granted (one letter each). Queued, not overwritten: two can land in
        -- one frame. SpeedFlying.cpp takes the queue and clears it.
        _G.Manastorm_AbilityQueue = (_G.Manastorm_AbilityQueue or '') .. string.sub(msg, 3, 3)
    elseif prefix == 'WXLTT' then
        local c = string.sub(msg, 1, 1)
        -- The bar op is kept in its own global: it is written ~every 500 ms, so sharing one slot with
        -- the tally let a bar tick that landed in the same client frame overwrite the tally answering
        -- a Reset or a Confirm -- the ranks then stayed as they were and the panel looked ignored.
        -- Only the server's ops are stored: the request whispers to the player's own name, so the
        -- client receives its own `Q`/`W`/`R` echo back, and that echo used to clobber the tally.
        if c == 'T' or c == 'F' then _G.Manastorm_TalentBody = msg
        elseif c == 'P' then _G.Manastorm_TalentBar = msg end
    end
end)

-- The name may never be needed before the first message (a Q is sent on world enter), so publish it
-- once at login too; the OnEvent above keeps it fresh afterwards.
local login = CreateFrame('Frame')
login:RegisterEvent('PLAYER_LOGIN')
login:SetScript('OnEvent', function()
    local n = UnitName and UnitName('player')
    if type(n) == 'string' and n ~= '' then _G.Manastorm_MountAnimPlayer = n end
end)