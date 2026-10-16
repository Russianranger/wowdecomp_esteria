# Native cursor selector

Build either client target and open **Escape → Interface → Mouse → Cursor appearance**.
Choose **Human (Default)**, **Night Elf**, **Orc**, **Undead**, **Hearthstone**, or
**Starcraft (Animated)**. The pointer changes
immediately; Okay keeps the choice, Cancel restores the previous choice, and Defaults
restores Human. The native `cursorTheme` CVar saves the choice in `WTF/Config.wtf` and
restores it after restarting or reloading the UI.

This is built into the client, not a loadable addon. It works with addons disabled.
The normal Point cursor changes; attack, loot, cast, item-drag and other contextual
cursors retain their original behavior. Human restores the original Point cursor
from the client's game data.

The custom BLP2 textures are compiled into each executable as Windows resources.
There are no extra runtime files to install and no FrameXML signature changes.

## Regenerating textures

With Python and Pillow installed, run from the repository root:

```powershell
python Addons/Cursors/convert.py
cmake --build build/x64 --config Release --target WowClient Wow --parallel
```

`Raw/` contains the supplied source art; `Textures/` contains 32×32 BLP2 files
with exact RGB palettes and separate 8-bit alpha, or uncompressed BGRA for detailed
art. Hearthstone is resized from 64×64 with alpha-aware Lanczos filtering; its
scaled hotspot is (2, 1), matching the supplied `.cur`. Human.png has two empty rows
above and below the artwork, removed during conversion without cropping any art.
Human.blp is retained as a converted reference; the runtime default uses stock art.

Starcraft uses all five supplied PNG frames. `convert.py` reads the `.ani` sequence
and rate chunks and generates `Textures/StarcraftAnimation.h`: six steps, including
the repeated first frame, with a total duration of 50/60 seconds. The native poll
handler follows elapsed time, including when rendering skips frames. Animation
updates only the normal pointer and unregisters when the in-game UI is destroyed.
The `.ani` is a timing source at conversion time; it is not needed at runtime.

## Offline checks

```powershell
python -m unittest discover -s Addons/Cursors/tests
cmake -S Addons/Cursors/tests -B build/x64/cursor-tests
cmake --build build/x64/cursor-tests --config Release
ctest --test-dir build/x64/cursor-tests -C Release --output-on-failure
```

For a live check, select each choice, test Okay/Cancel/Defaults, reload the UI,
restart the client, and hover lootable objects and enemies while dragging an item.
With Starcraft selected, also check that the animation loops while the mouse is
stationary and resumes after a contextual cursor or item drag.
