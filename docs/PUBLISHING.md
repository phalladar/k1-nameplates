# Publishing checklist

Run `python build.py` first.

1. **DeadlyStream** (primary): Downloads → Knights of the Old Republic → Mods. Upload
   `dist/K1-Nameplates-1.0.zip` and the screenshots, then paste the page text below.
2. **Nexus Mods** (mirror): Knights of the Old Republic → Add mod → category *User Interface*. Same zip,
   screenshots and text. No requirements.
3. **GitHub** (source): make the repo public, attach the zip to a `v1.0` release, and link the repo from both
   mod pages.
4. **Word of mouth**: r/kotor with the combat before/after pair, the KOTOR modding Discord's mod-release
   channel, and a note in Lane's KOTOR 1 reverse engineering thread on Deadly Stream (the addresses came from
   his database).

## Screenshots
`docs/`, 1920x1080 crops of 5120x1440 shots, in upload order: `after-combat.jpg` (main image: targeted hound
with framed 75% plate, damage chip, gold "Dead Settler" loot label), `before-combat.jpg`, `after-pack.jpg`
(plates on a distant pack), `before-pack.jpg`. Originals: `GAME\screenshots\swkotor 2026-10-27 17-58..18-01*.png`.

## Page text

**K1 Nameplates**

World of Warcraft-style nameplates for KOTOR: every enemy gets its own health bar, and loot and usable
objects are always labelled.

KOTOR shows one overhead name and health bar at a time, only for the creature under your cursor or your
target. It flickers away and is hard to click. This mod gives every hostile you could click its own plate:

- Red plates on every visible enemy within 60 m; neutral and friendly creatures on hover or target.
- A framed, larger target plate with health in percent. Other plates fade while you have a target.
- Damage chips, distance scaling and stacking instead of overlap.
- Click a plate to select or attack, just like clicking the model.
- Object labels: remains, containers and items on the ground in gold while they hold loot, "[empty]" once
  looted, terminals and workbenches in cyan. Scenery and invisible script objects get no label.
- Only while the HUD is up. F6 turns it off and on. Everything is configurable in nameplates.ini.

**Install:** copy dinput8.dll and nameplates.ini into the game folder. Delete them to uninstall. It doesn't
touch swkotor.exe, Override or saves.

**Compatibility:** KOTOR 1 patch 1.03 on Windows (Steam, with or without the Editable Executable; GOG should
work but is untested). Any resolution, with or without UniWS / High Resolution Menus / the 4 GB patch.
Conflicts only with other mods that ship a dinput8.dll. Not for KOTOR 2.

Engine addresses from Lane Dibello's KOTOR Patch Manager database. Source:
https://github.com/phalladar/k1-nameplates
