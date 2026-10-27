K1 Nameplates 1.0
=================

Star Wars: Knights of the Old Republic (2003), PC.

WHAT IT DOES
------------
KOTOR shows a single overhead name and health bar, only for the creature under your cursor or your
current target. It flickers away constantly and is hard to click. This mod gives every enemy in range
its own nameplate, World of Warcraft style, and labels the things you can loot or use.

Nameplates
  - Every hostile you could click (on screen, not hidden behind walls, within 60 m) gets a red plate
    with its name and health bar. Neutral (yellow) and friendly (green) creatures get one while you
    hover or target them.
  - Your target's plate gets a white frame, is a little larger and shows health in percent. Other
    plates fade slightly while you have a target.
  - Damage leaves a short "chip" on the bar so you can see each hit.
  - Plates shrink with distance and stack instead of overlapping.
  - Click a plate to select or attack that creature, exactly like clicking the model.

Object labels
  - Containers, remains and items on the ground are labelled in gold with a small sparkle while they
    hold loot, and show "[empty]" in grey once looted.
  - Terminals, workbenches and other usable objects are labelled in cyan.
  - Scenery and invisible script objects get no label. Doors are off by default (ShowDoors=1).

Plates appear only while the normal HUD is up: never in dialogs, menus or cutscenes. F6 turns
everything off and on.

REQUIREMENTS
------------
KOTOR 1 for Windows, patch 1.03 (every current Steam and GOG copy). It works with or without the
KOTOR Editable Executable, UniWS, High Resolution Menus and the 4 GB patch, at any resolution.

INSTALL
-------
Copy dinput8.dll and nameplates.ini into the game folder (the one with swkotor.exe), for example
    C:\Program Files (x86)\Steam\steamapps\common\swkotor
Start the game normally.

It doesn't change swkotor.exe, Override or your saves. Everything happens in memory while the game
runs, and it checks the game code at every place it hooks before touching it. On an exe it doesn't
recognise, it does nothing.

UNINSTALL
---------
Delete dinput8.dll, nameplates.ini and nameplates.log from the game folder.

SETTINGS
--------
Edit nameplates.ini (Notepad is fine) while the game is closed. Every setting is explained in the
file. The most useful ones:
    ShowNeutral=1      plates on neutral creatures all the time, not only when hovered
    ShowFriendly=1     the same for party members and allies
    Range=60           how far away (metres) hostiles get a plate
    ShowObjects=0      no object labels
    ShowDoors=1        label doors too
    HideEmpty=1        hide looted containers instead of marking them "[empty]"
    Scale=1.2          make everything 20% larger
    ToggleKey=117      the on/off key as a Windows key code (117 = F6)
    Font=...           any installed Windows font

COMPATIBILITY
-------------
- Tested on Steam with the KOTOR Editable Executable, UniWS, High Resolution Menus, K1 Area Map
  Fixes, the 4 GB patch, ReShade (OpenGL) and about 200 other mods from the kotor.neocities.org K1
  Full build, at 5120x1440. On the stock Steam exe it hooks correctly; full play on it is untested.
- GOG uses the same 1.03 code and should work, but is untested. Please report either way.
- Conflicts with any other mod that also ships a dinput8.dll. (ReShade uses opengl32.dll, which is
  fine.)
- Not tested together with KOTOR Patch Manager patches.
- KOTOR 2 / TSL, the Mac/Linux ports and the mobile versions are not supported.

TROUBLESHOOTING
---------------
nameplates.log in the game folder says whether the hooks installed. "hooks installed: ok" is good.
If something looks wrong, set Debug=1 in nameplates.ini, reproduce it, and include the log and a
screenshot in your report.

CREDITS
-------
Engine function addresses come from Lane Dibello's KOTOR Patch Manager address database (MIT,
github.com/LaneDibello/Kotor-Patch-Manager) and his KOTOR 1 reverse engineering work on Deadly Stream.

Source: https://github.com/phalladar/k1-nameplates    License: MIT
