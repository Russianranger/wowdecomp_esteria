-- Manastorm_Speedflying: the outbound half of the server link.
-- The engine builds CMSG_MESSAGECHAT inside SendAddonMessage and has no C++ equivalent, so the client
-- (WoW/Source/Ui/SpeedFlying.cpp SendLink) calls this to put "WXLMS" / "WXLTT" bodies on the wire.
-- The server module answers a whisper to the player's own name, so that is where every body goes.

function Manastorm_SendAddon(prefix, body)
    local to = UnitName and UnitName('player')
    if type(to) ~= 'string' or to == '' or not SendAddonMessage then return end
    SendAddonMessage(prefix, body, 'WHISPER', to)
end
