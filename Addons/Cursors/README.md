# Native cursor selector

Build either client target and open **Escape → Interface → Mouse → Cursor appearance**.
Choose **Human (Default)**, **Night Elf**, **Orc**, or **Undead**. The pointer changes
immediately; Okay keeps the choice, Cancel restores the previous choice, and Defaults
restores Human. The native `cursorTheme` CVar saves the choice in `WTF/Config.wtf` and
restores it after restarting or reloading the UI.

This is built into the client, not a loadable addon. It works with addons disabled.
The normal Point cursor changes; attack, loot, cast, item-drag and other contextual
cursors retain their original behavior. Human restores the original Point cursor
from the client's game data.

The other three BLP2 textures are compiled into each executable as Windows resources.
There are no extra runtime files to install and no FrameXML signature changes.

## Regenerating textures

With Python and Pillow installed, run from the repository root:

```powershell
python Addons/Cursors/convert.py
cmake --build build/x64 --config Release --target WowClient Wow --parallel
```

`Raw/` contains the supplied PNGs; `Textures/` contains lossless 32×32 BLP2 files
with an exact RGB palette and separate 8-bit alpha. Human.png has two empty rows
above and below the artwork, removed during conversion without cropping any art.
Human.blp is retained as a converted reference; the runtime default uses stock art.

For a live check, select each choice, test Okay/Cancel/Defaults, reload the UI,
restart the client, and hover lootable objects and enemies while dragging an item.
