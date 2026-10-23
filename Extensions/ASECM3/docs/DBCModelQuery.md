# Querying the client's loaded display/model records

`client/DBCModelScript.cpp` is a module-owned native object. Its registration is
called from the existing module script bootstrap, adding no new original-host
hook. The Lua API is read-only and runs on the normal client script thread:

```lua
local info, reason = ASECM3_GetDBCModelInfo(displayId)
```

The input must be an actual Lua number containing a positive finite int32.
Strings, fractions, zero, negatives, infinity/NaN and larger values reject.
Unloaded or schema-invalid DBCs, absent display/model records, empty/overlong
paths and nonfinite scale fields return nil plus a reason. In particular, the
host's schema-mismatch zero record is not mistaken for a real result.

A result contains displayId, modelId, path, format, isM3, displayScale,
modelScale, alpha, displayRecords, modelRecords and exactPathExists. Values come
from the already loaded CreatureDisplayInfo/CreatureModelData tables. The exact
path is checked through native SFile. No model extension fallback is implied:
an MDX spelling may legitimately require a native M2 alias. Existence does not
establish parse validity or renderability. No DBC pointer is returned or retained.

Alt+F10 -> Player -> Inspect DBC reports the currently selected display's loaded
DBC model record, separately from a Player visual override. It changes no path,
setting, server field or DBC bytes. The API is also available before login.

The native probe checks seven invalid input forms, queries the existing gallery
display300001 (falling back to display1 if absent), reports the effective loaded
record and checks Player panel initialization. Its success is a loaded-DBC query
check, not an actual DBC-selected nonplayer M3 draw. The latter still requires a
DBC containing a selected M3 name and a live unit using that display. The staged
gallery table is a separate client-data operation. In the current installation
its model70001/display300001 path has been updated to the supplied Alexstrasza
M3 with an original-table backup. The native probe reports the effective loaded
M3 row and tests the exact virtual path through AssetFrame, retaining the
distinction between native preview and actual live nonplayer unit dispatch.

Evidence: verification/m3/dbc-effective-query-{native-result,normal-runtime}.json
and dbc-effective-query-native-probe.log. Source-table hashes remain separate
from effective loaded table observations because archive priority can differ.
