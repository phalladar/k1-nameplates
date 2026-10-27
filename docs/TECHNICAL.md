# Technical notes

Engine details the mod relies on, for KOTOR 1.03 Windows (Steam/GOG share these addresses). Function names and many
offsets come from the [KOTOR Patch Manager](https://github.com/LaneDibello/Kotor-Patch-Manager) address database
(`AddressDatabases/kotor1_0_3.db`); the rest were found while building this mod.

## Contents
1. [Loading and hooking](#1-loading-and-hooking)
2. [Drawing](#2-drawing)
3. [Objects](#3-objects)
4. [Camera and visibility](#4-camera-and-visibility)
5. [Selection and clicking](#5-selection-and-clicking)
6. [Pitfalls](#6-pitfalls)

## 1. Loading and hooking
- swkotor.exe imports only `DirectInput8Create` from `dinput8.dll`. The proxy loads the System32 copy and forwards it.
- The stock Steam exe is SteamStub-packed: `.text` is encrypted on disk and still encrypted when `DllMain` runs. The
  mod installs in `DllMain` only if the first hook site already reads correctly (GOG, de-DRM'd exes), otherwise on the
  first `DirectInput8Create` call.
- Engine methods are `__thiscall`. Hooks are written as `__fastcall (this, edx_unused, args...)`, which has the same
  stack cleanup. Two hook kinds: retarget an `E8` call (`hook_call`) and detour a position-independent prologue
  through a trampoline (`hook_entry`, used for `HandleMouseOver`, prologue `56 8b f1 8b 4c 24 0c`).

## 2. Drawing
- `CSWGuiMainInterface::Draw` calls `CSWGuiPanel::Draw` at 0x68b6a7. It runs only while the HUD is shown, so drawing
  before forwarding puts the plates under the HUD and hides them in dialogs, menus and cutscenes.
- GL state: `glPushAttrib(GL_ALL_ATTRIB_BITS)`; disable texture units 0-3 via `glActiveTextureARB` (the engine leaves
  lightmap units enabled), ARB/NV vertex/fragment programs, depth test and fog; set pixel-store state before texture
  uploads; ortho `0..gui_w, gui_h..0`.
- GUI size: `CClientExoApp::GetSWGuiManager` (0x5eda40) +0x6c / +0x6e (shorts) = the window size in pixels.
- Text: glyphs rendered with GDI (anti-aliased) into an atlas texture.
- The engine's overhead name/health bar belongs to `CSWGuiTargetActionMenu` (call at 0x68b6b3 → 0x685ed0). The mod
  temporarily swaps the vtables of its labels at +0x184c, +0x170c, +0x15cc and +0x198c to a no-op draw when the
  object already has a plate; the action buttons still draw.

## 3. Objects
- `*(0x7a39fc)` = CAppManager → +4 CClientExoApp → +4 CClientExoAppInternal.
- Internal +0x18 = module → +0x48 = CSWCArea. Area +0x20c / +0x210 = object id array / count.
- `CClientExoApp::GetGameObject(id)` 0x5ed580 → vtable +0xc returns the CSWCObject.
- **CSWCObject:** +4 id, +8 type (5 creature, 6 item, 7 trigger, 9 placeable, 10 door), +0x24 position (Vector),
  +0x68 anim base → +0xb8 Gob, +0xe8 name (CExoString), `GetServerObject` 0x63d4b0.
- **CSWCCreature** (CSWCObject vtable +0x2c): +0x2dc reaction (0 neutral, 1 friendly, 2 hostile), +0x21c appearance →
  `GetHeight` 0x6973b0 (model height, float).
- **Server object:** vtable +0x98 with arg 1 = max HP, vtable +0x9c with arg 0 = current HP (short).
- **CSWSPlaceable:** +0x328 usable, +0x324 non-null for containers, `GetItemCount(0)` 0x587710, +0x228 localized name
  (CExoLocString).
- Names: `CExoLocString::GetString` 0x5ea130 `(this, language, CExoString* out, gender 0)` with language =
  CClientExoApp → +4 → +0x144. Free with `CExoString` destructor 0x5e5c20.

## 4. Camera and visibility
- `CClientExoApp::GetModuleCamera` 0x5ed560: vtable +0x20 position, +0x24 orientation quaternion (forward = −Z
  column), +0x28 `MapToScreen(out, x, y, z, 1)` → normalized 0..1, y top-down. It doesn't reject points behind the
  camera; test that yourself.
- Gob +0x11 (`IsInViewVolume`) is set by the renderer each frame for models it draws (frustum + room visibility). It is
  a good "could the player click this" test and cheaper than line of sight.

## 5. Selection and clicking
- Internal +0x2b4 = target id, +0x4a4 = mouse-over id (0x7f000000 = none).
- `CClientExoAppInternal::HandleMouseOver(id, Vector* pos, int)` 0x6222f0 stores the hovered object. Substituting the
  plate's object id (and position) while the cursor is over a plate makes the game's own click handling
  select/attack it.

## 6. Pitfalls
- CSWCCreature +0x194 is **not** a stealth flag (it's set on ordinary enemies).
- CSWCObject +0x90 ("selectable", what `HiliteAreaObjects` checks) is also true for scenery and invisible script
  objects such as "Transition Abort Object" or "Zone Controller". Use the server placeable's +0x328 `usable`.
- The client-side name (+0xe8) stays empty until the object is hovered; the server sends it on demand. Read the server
  placeable's localized name instead.
- A fresh kill is a dead **creature** (what the cursor hits) plus a separate invisible "Remains" placeable at the same
  spot holding the loot. After a save/reload only the placeable exists. The mod treats a hovered/targeted corpse as
  hovering the Remains within 1.5 m.
