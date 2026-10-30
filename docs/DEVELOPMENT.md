# CharSelect: development notes

How the code is organised, how to build and test it, and what's been learned about the engine along the way, for anyone working on CharSelect. `README.md` is the player-facing overview; `docs/SAVE_SAFETY.md` is the save-safety write-up; `docs/CUSTOM_LOBBY.md` tells players how to make any location a lobby. Keep this file current when you learn something the hard way.

## What it is

An SKSE plugin for Skyrim AE that replaces the main menu with a character select screen.

- **The lobby:** a throwaway game started with `coc` into an interior cell (the Bannered Mare by default). Each saved character stands at a spot from the cell's layout in their own RaceMenu look and gear, in a configurable pose.
- **The overlay:** a Scaleform menu built entirely from C++ lets the player pick a character and **PLAY** their latest save. The button reads **ENTER** on screen and in the player docs (since 0.5.40); code, tests and these notes still call the action PLAY.
- **Handed to vanilla:** **LOAD** opens the game's own all-characters list in the Journal; **NEW GAME** and **MAIN MENU** go through the real main menu; **QUIT** quits.
- **Layout editor:** F8 in the lobby positions the camera and characters and picks poses and seats.

## Build, test, ship

```bash
export VCPKG_ROOT="C:/Program Files/Microsoft Visual Studio/18/Community/VC/vcpkg"
cmake --preset vs2026 && cmake --build --preset release   # plugin, tests, dist zip
ctest --preset release                                     # all tests
```

- **Toolchain:** Visual Studio 2026 (MSVC 14.51), CMake 4.x, Visual Studio's bundled vcpkg. On VS 2022, change the generator in `CMakePresets.json`.
- **CommonLib:** alandtse CommonLibSSE-NG **v12.1.0**, a submodule in `extern/CommonLibSSE-NG`. SE and AE in one DLL, VR off. Read its headers before using any RE API; never invent members. `directxtk` and `rapidcsv` in `vcpkg.json` are needed by CommonLib itself.
- **Output:** the post-build step builds `dist\CharSelect-<version>.zip`, a FOMOD installer: `Core\` (`dist-files\` minus `CharSelect.ini`, plus the DLL and PDB), `Locations\<folder>\SKSE\Plugins\CharSelect.ini` per location, and `fomod\` (see the lobby cells item under Open items).
- **Tests:**
  - `build\RelWithDebInfo\CharSelectTests.exe` (Catch2) covers `src/core/` and `src/SaveIndex.cpp`. Run it directly for full output, or with a tag: `CharSelectTests.exe "[lobby]"`.
  - `tools/tests/` holds the Python tests for `savediff.py` and `make_swf.py` (`python -m unittest discover -s tools/tests`).
  - Set `CHARSELECT_TEST_SAVES` to a saves folder to also parse real saves (both suites).
  - The tests also check the shipped files: `CharSelect.ini` matches the built-in defaults, every pose and seat in `CharSelectLayouts.ini` is valid, and `CharSelectMenu.swf` matches `tools/make_swf.py`.
- **Tools:**
  - `build\RelWithDebInfo\savescan.exe "<saves dir>"`: the plugin's save indexer as a console program.
  - `python tools/savediff.py A.ess B.ess`: compares two saves and their co-saves (object by object, scripts, every SKSE plugin's data by size and contents). Used for the save-safety test.
  - `python tools/make_swf.py dist-files/Interface/CharSelectMenu.swf`: writes the empty movie the menu loads.

## Test setup

Developed and tested on Skyrim AE 1.7.104 (SKSE 2.3.1, Address Library) with a heavy load order (about 1,000 plugins, over 100 SKSE plugins, RaceMenu 0.5.0). Saves may live in a mod manager profile folder (`sLocalSavePath`, e.g. `Saves\<profile ID>\`).

## Code layout

| File | Role |
|---|---|
| `main.cpp` | Logging (info level, or trace with `bDebugLog`; every line flushed; `SKSE::Init(..., {.log = false})`, or CommonLib truncates our log) and SKSE message routing. |
| `Lobby.*` | The state machine (Idle, Entering, Active, LoadingSave): `coc`, waiting for the player to settle, staging, one runtime `TESNPC` per slot, the reveal, the highlight light, save and control blocking, `KeepCellClear`, the editor backend, `ClearPresets`, and loading the save. |
| `Poses.*` | One performer per slot: top package, linked furniture, rest and select poses, sit and stand with fallbacks, idle events with retries, and a watchdog (re-seat, drift, head tracking). |
| `SelectMenu.*` | The `IMenu` on a 36-byte empty SWF; all UI drawn from C++ through GFx. Modes: splash, select, editor panel. Input is queued from `Events` and handled in `AdvanceMovie` on the UI thread. |
| `Frontend.*` | Replaces the main menu (hides its movie, shows the splash, starts the lobby), `ReturnToMainMenu`, and driving the Journal for LOAD. |
| `Events.*` | Input, menu and object-loaded event sinks (the last hides an actor arriving in the lobby as its 3D loads). |
| `Sidecar.*` | Captures each character on save and load: `SKSE\CharSelect\Characters\<ID>.json`, the RaceMenu preset (`CharGen.SaveCharacter`, throttled), and the tint export with retries. Only while the lobby is Idle. |
| `Tint.*` | Copies the player's face tint off the GPU into a DDS; writes a skin-tone placeholder so a missing tint never renders pink. |
| `Layout.*` | Per-cell layouts: the shipped `CharSelectLayouts.ini` merged key by key with the user's `Documents\...\SKSE\CharSelect\Layout.ini`; resolving form references. |
| `SaveIndex.*` | Reads `.ess` headers only. The character ID comes from the file name (`Save7_1A2B3C4D_...`); the header's FILETIME is local time. Shared with `savescan` and the tests. |
| `ResetGuard.*` | Undoes what Skyrim reverts when the lobby's game ends (see the pitfall under Saves and loading): `Capture` just before leaving (PreLoadGame while the lobby isn't Idle; `ReturnToMainMenu` before `resetGame`), `Restore` at the main menu, NewGame and PostLoadGame. Puts back, only where the record now holds a strict subset of what it held (or an empty field): armor addons (appended, slots added), NPC/race skins, keywords (NPC, race, armor, weapon), NPC spells, perks, head parts, hair color and face details; plugin records only. Emptied NPC records (`FindEmptied`: every spell and perk gone, which a save can't do to a base record, plus the face templates they use; never the player's 0x7) get their lists and look (head data, head parts, tint layers, face data) in fresh blocks, never touching the old ones, and their other fields (face template, class, combat style, voice, outfits, height at 1) only where empty; weight isn't touched. Worked out before restoring, so a kept capture (MAIN MENU, then a load) doesn't restore a record twice. With the debug log, it also logs other changes across the switch, after the restore and without reading head or tint blocks. |
| `Appearance.*` | `SavedHeadParts` and `WhySkipped` serve `Lobby`'s `AddMissingParts` (see the RaceMenu pitfall). The rest is debug-log only (`bDebugLog`), guarded: `Look:` lines with a character's head parts, hair colour, face details and sliders (`LogRecord`), their saved look resolved in the current load order with every difference from a record (`CheckPreset`), and their head 3D (`Log3D`), in game at each capture and export and per slot in the lobby. |
| `Config.*`, `Paths.*`, `Papyrus.h` | Settings, file locations (`paths::ForLog` writes them without the Windows user name), and calling Papyrus statics (RaceMenu's `CharGen`) with a callback. |
| `Util.h` | `Guarded` (every engine entry point runs through it, so an exception is logged instead of terminating the game), `GameLock` (see Threads), `QueueTask` / `QueueTaskUnlocked` / `QueueUITask`, `RunAfter` (one `TimerQueue` thread), `OwningPlugin`. |
| `core/*` | Game-free, unit-tested logic: `LobbyRules`, `InputMap`, `ConfigParse`, `LayoutFile`, `LayoutMath`, `FormKey`, `PoseTable`, `SidecarJson`, `ExportTracker`, `Encoding`, `Files`, `Dds`, `TimerQueue`, `UiRules`, `Text`, `Geometry`. No CommonLib, no logging (they return what to log). Put new decision logic here when it doesn't need the game, and test it in `tests/`. `Encoding.cpp` is the only one with `<windows.h>` and is built without the PCH. |

**Layout keys** (per cell section; `CharSelectLayouts.ini` documents them): `camerax/y/z/yaw/pitch`, `stagex/y/z` (the stage centre: seats nearest it are preferred and listed first; defaults to the middle of the row below), `slotNx/y/z/heading`, `slotNpose`, `slotNselect`, `slotNfurniture` (`0x<local>|<plugin>`, `place:0x<base>|<plugin>` to spawn furniture at the spot, or `none`).

**Built-in Bannered Mare layout:** slot 1 stand (select: draw_weapon); slot 2 hands_behind_back (select: draw_weapon); slot 3 sit_drink on NobleBench01 `0x000C8FC6|Skyrim.esm`; slot 4 warm_hands_crouched at the fire's north-west edge (-223.6, -277.9); slot 5 sit_floor at its north-east edge (-5.3, -294.6). Slots 2, 4 and 5 are the maintainer's own choices.

## Engine facts and pitfalls

Verified in game, mostly the hard way. Read before changing the related code.

**Threads and callbacks**
- **SKSE `AddTask` / `AddUITask` run on worker threads on AE**, not the main thread, and logs show them overlapping each other and the main thread's SKSE messages and menu events. `GameLock` (Util.h) is held by every task, SKSE message and menu event, so the lobby's state is only touched by one at a time. The exceptions are engine calls that wait for other threads, which run as tasks without it (`QueueTaskUnlocked`): starting a load (`BGSSaveLoadManager::Load`; PreLoadGame follows on the main thread and takes the lock) and `coc`, which loads the cell before it returns while that load's menu events arrive on other threads (holding the lock across it froze the game in 0.9.13). Event sinks that fire inside engine work (object loaded) take no lock and only queue a task.
- **The D3D11 immediate context** needs `BSGraphics::Renderer::Lock()` (the tint export crashed the graphics driver without it; RaceMenu takes the same lock), and nothing may throw while it's held.
- **An exception that escapes into the game terminates it.** Route every entry point through `Guarded` (Util.h) and every task through `QueueTask`.
- **Guard every delayed callback** with a generation, epoch or ticket check: the lobby can end while a callback is waiting.
- **Never keep a `GFxValue` past the frame**, even from our own movie. Releasing one after its movie is gone crashes. Look display objects up by path each frame.

**Text and files**
- **Engine strings are in the game's ANSI code page**, not UTF-8. Convert with `text::ToUtf8` before JSON or Scaleform (a name like "Björn" made nlohmann's `dump()` throw), and back with `FromUtf8` before handing names to the engine (`LookupForm` plugin names).
- **`std::filesystem::path::string()` throws** for characters the ANSI code page can't hold. Use `text::PathUtf8` for anything logged or displayed.
- **Owning plugin, not `GetFile(0)`:** `GetFile(0)` is the last plugin to edit a form (USSEP, WACCF), so lookups by it miss. `OwningPlugin` takes the plugin from the form ID's load-order index.

**Saves and loading**
- **Saving is blocked** with `PlayerCharacter::GetGameStatsData().byCharGenFlag` `kDisableSaving` (what `Game.SetInChargen` sets). It's set as soon as the lobby's game exists (NewGame, then the first loading-screen close, and again at staging) and lifted only when a load actually starts (PreLoadGame) or the main menu opens. The console's own `save` command is refused while it's set.
- **`BGSSaveLoadManager::Load(file, true)`:** CommonLib calls the flag `a_checkForMods`, but `true` loads without the missing-content prompt and `false` abandons the load and returns to the main menu (which looped with the lobby re-entering). PLAY uses `true`; the prompt only appears through the game's own LOAD menu. This is a documented limitation.
- **Leaving the lobby's game reverts records** (0.9.2 SkinAudit, 2026-10-27; 0.9.3 ResetGuard, 2026-10-28). NPCs whose record was loaded during the lobby (the Bannered Mare's Mikael and Uthgerd, Housecarls Pre-Thaneship's Lydia, and others a startup script touched) come back emptied: no spells, perks or keywords, their head data, face data and tint list freed but still pointed at (Lydia's pink hair; Papyrus `ActorBase.GetHairColor` on her crashed the game via OBody), and height reset to 1. Their face-template records come back emptied with them. A save's own changes to such an NPC can apply on top, so ResetGuard fills only what's empty (a weight the save set was once overwritten). Also: on a load from the lobby and on the way back to the main menu, plugin records changed at runtime (a skin mod's spare skins, skins SPID gave NPCs at startup) come back as in their plugin. A plain load inside a real game doesn't do this; a console `coc` from the main menu followed by a load does, with no CharSelect code involved. Mods that make such changes once per session (the skin mod) leave them broken: men without hands. `ResetGuard` restores them; the README's Compatibility section is the player-facing version.
- **A cancelled or failed load** reports `PostLoadGame(false)`; the lobby must resume, not treat it as having left.
- **RaceMenu presets are never cleared on load** (its `Revert` skips them), so `lobby::ClearPresets()` runs before every way out of the lobby.
- **Quit to Main Menu crashes on big load orders are Skyrim's**, not ours (checked: no plugin on the stack, nothing of ours running).
- **Don't suppress or touch quests.** An option that suppressed start-game quests broke mods and was removed.

**Lobby actors**
- **Runtime base:** factory `Create()`, then `Copy(playerBase 0x7)`, then `faceNPC = nullptr`, no outfits or container, own `headParts` / `faceData` / `headRelatedData` (never null: a null `faceData` crashed under RaceMenu), and an empty package list (`std::construct_at`). Tint layers, perks, spells and factions are not shared after `Copy` (checked in logs).
- **Appearance:** `CharGen.LoadCharacterEx(actor, race, "CharSelect_<ID>", 0x0F)` while disabled, then enable.
- **RaceMenu can leave out saved head parts** (issue 3, 0.9.10): on the maintainer's setup a hair from a light plugin (KS Hairdos SMP) became a race default, though the part resolves by plugin name and passes RaceMenu's own sex/race rules (`appearance::WhySkipped`). Its presets write a light plugin's form as the low 24 bits of the load-order ID (`KSHairdosSMP.esp|1D58FB`, light slot included), but rewriting that to the plugin's own ID (0.9.9) didn't help, so the cause inside RaceMenu is unknown. `AddMissingParts` puts any such part on with `TESNPC::ChangeHeadPart` (the call RaceMenu uses) after `LoadCharacterEx`, while the actor is still disabled; RaceMenu's hair tint still lands on it as the head builds. Also seen: RaceMenu doesn't set `headRelatedData` (hair colour form, face details texture set) on lobby characters, while the real player has both; the 3D hair tint is right regardless.
- **`SetPosition` on a disabled actor doesn't stick;** place again after enable. **Disable then Enable in the same frame doesn't stick** either; leave a frame between them.
- **Linked refs must never outlive their target.** Seated characters are linked to their furniture (`SetLinkedRef`). When an actor is destroyed the engine reads the link's target to unregister it; if the target was freed first, the game crashes (`BaseExtraList::GetByType` reading type 0x51, called from the linked-ref clean-up; GarbageCollector on the stack). A load freed the Ragged Flagon's chairs before our characters (0.5.37). So `UnlinkSeats` runs when PLAY starts (`LoadFile`), when the Journal's LOAD list opens, and before MAIN MENU / NEW GAME; `ResumeLobby` and a Journal closed without a load link them again (`RelinkSeats`); placed furniture is unlinked before it's deleted. Observed (0.5.39): characters seated instantly at staging often have no link afterwards (`PrepareRef`'s link doesn't survive the instant pop), and only the sit path (editor, resume, re-seat) links them; `UnlinkSeats` copes with either.
- **CrashLogger can hang while writing a report** (seen with EngineFixes' allocator on the stack): the game shows a frozen loading screen and `crash-*.log` stops after the first lines. Check for a fresh `crash-*.log` before calling something a hang.
- **Equip:** `ActorEquipManager::EquipObject(actor, obj, nullptr, 1, nullptr, false, true, false, true)` after the 3D loads.
- **AI must be on**, or the animation graph never updates (arms-out stance). Without their own top package, actors sandbox (`DefaultMasterPackage`).
- **Top packages:** standing, our `CreatePackage(kDoNothing)` with conversation and world-interaction behaviour flags cleared; seated, `defaultSitLinkedRefNoConv` 000ECEEB with the furniture as linked ref; asleep, `ambushSleepPackage` 0009448B. Packages that search for furniture are too slow. Created get-up packages stay running and block re-sitting; standing up uses `StopInteractingQuick`.
- **Bard audience:** `BardAudienceQuest` pulls nearby NPCs over to applaud; our actors join `BardAudienceExcludedFaction` (0010FCB4) to opt out.
- **NPC schedules keep running** in the lobby game. Anyone arriving is disabled as their 3D loads (`TESObjectLoadedEvent`, `OnObjectLoaded`), and `KeepCellClear` sweeps every 0.5 s for anyone that misses: people already there at staging, outdoors an NPC walking into range with their 3D already loaded. The sweep's log line says whether each was caught before or after its 3D loaded.
- **Idles** are graph events via `NotifyAnimationGraph` (no OAR needed). Seated idles need about 1 s after sitting; events are retried.
- **Moving the camera:** easing the player with `SetPosition` every frame looked janky (the engine re-grounds the player). Drive a real camera state if it's ever revisited.

**Menus and UI**
- The engine closes our menu when the `coc` loading screen opens; the splash is shown again when it closes.
- **Input gating:** lobby keys only reach us with our menu open and no console, Journal, message box or RaceMenu over it. Not "any modal menu": a mod's always-open overlay flagged modal would lock the lobby.
- **Journal LOAD** (from SkyUI's `quest_journal.swf`): `_root.QuestJournalFader.Menu_mc`; `RestoreSavedSettings(2, false)` for the System tab; the page's `StartState(CHARACTER_LOAD_STATE)` shows the all-characters list after the save/load panel is up. LOAD is greyed out when the lobby character has no saves, so we re-enable the entry and replace `onCategoryButtonPress` (the original is stashed on the page as `charSelectOriginalCategoryPress`).
- **Notifications can be hidden** by mods such as NotificationFilter (`Debug.Notification`); use message boxes for anything the player must see.
- **GFx:** `$EverywhereFont` with `embedFonts = true`; `_root._xmouse` never updates (use `MenuCursor`); UTF-8 text renders fine.
- **RaceMenu's own tint export never writes a file** on this setup, so we export the tint ourselves (the player's tint slot: 512x512 R8G8B8A8, one mip). The engine caches a texture path once loaded, so a new tint may only show after a restart.
- **Windows macros:** `min`, `max`, `near`, `far`; write `(std::min)(...)`.

**Bannered Mare:** floor z about 65; the fire pit at (-121, -385), sunken to z about 33; NobleBench01 000C8FC6 at (-291, -374) facing east; bard bench 00060343 at (47, -382); columns at (-256, -128) and (0, -128); the bar from x about 224.

## Decisions already made

- **PLAY skips the missing-content prompt:** a documented limitation (README, SAVE_SAFETY); LOAD shows it.
- **A character who used RaceMenu mid-game gets a new ID** and appears twice; leave it (the old one stays reachable through LOAD).
- **A missing or moved seat falls back to standing;** players fix layouts with F8.
- **No cached lobby save:** not viable across changing mod lists.
- **The tint export stays on the GPU** (under the renderer lock), not a CPU recomposition.
- **Alternate-start mods are fine:** `coc` bypasses them.
- **Outdoor lobbies:** any outdoor cell counts as "arrived" (the player can land in a neighbouring cell); hiding people and the seat list cover everything within 4000 units (`ForEachLobbyRef`), not one cell; layouts are keyed by the `sLobbyCell` name (`LayoutKey()`), not the cell the player stands in; the default row sits on the terrain (`TES::GetLandHeight`). Optional `hour` and `weather` layout keys set the lobby game's time and force a weather, released in `Teardown` before any load.
- **No automatic layout:** every cell ships with a layout. A cell without a saved camera uses where the game put the player; characters without a saved spot stand in a row in front of the camera (`geometry::RowSpot`), to be arranged with F8. The old fire-finding arc and its INI settings were removed before release.

## Open items

- **Forms never freed (checked 0.5.38):** the engine frees the placed references (characters, lights, furniture) on a load or at the main menu, but not the base forms we create: every visit leaves 5 actor bases, a light base and a package registered. A few KB per visit, so harmless in practice; freeing or reusing them is a possible later improvement, not a release blocker.
- **Load crash with seated characters (fixed in 0.5.38, to confirm):** see the linked-refs pitfall. The earlier one-off "load hang" after MAIN MENU and back was probably the same crash with CrashLogger hanging. Confirm over the release week that PLAY and MAIN MENU from layouts with several seated characters never crash.
- **Missing hands with a skin mod (fixed in 0.9.3, confirmed 2026-10-28):** The skin mod fills its spare skins (empty in its plugin) from a body skin's addons plus its own, the first time it meets that skin, and never refills them. Leaving the lobby's game reverts the spares (see the pitfall above), so every man on one renders without hands for the session. `ResetGuard` puts the addons back at PostLoadGame, before the skin mod redraws NPCs (it does so about 20 s after a load with a skin distribution mod installed). Emptied NPC records (Mikael, Uthgerd, Lydia and their Exquisite NPCs face templates) are restored whole since 0.9.7, confirmed 2026-10-28: the reset frees their head data, face data and tint list but leaves the record pointing at the freed blocks (Lydia's pink hair; OBody's `GetHairColor` crashed on the garbage), so they get fresh copies and the old blocks are never written. The diagnostics used to find this (SkinAudit, a hardware write-breakpoint watch) were removed in 0.9.6; they're in git history (0.9.1 to 0.9.5) if ever needed. The breakpoint run showed the emptying is the engine's record reload during the load (AE ids 13763 via 15138/15150, from the save loader 35497/35566/35600): ClearData then InitializeData, with no read-back for records whose characters were loaded in the lobby. Full investigation notes are kept outside the repo.
- **Face details on lobby characters (optional):** the saved look's face details texture set (`actor.headTexture`) never reaches the lobby base (see the RaceMenu pitfall). The maintainer saw no difference in the face, but setting it like the missing parts would make the record match.
- **Plugins holding pointers to lobby actors** (OBody, SPID, HDT-SMP touch them) could crash after a load. Never seen; watch crash logs for FF-range actors.
- **Untested setups:** SE 1.5.97, MO2, other main menu mods (we hide the vanilla movie and call its buttons by name), ultrawide, translated games, more than 6 characters (the editor panel runs off the bottom at 9).
- **Mods writing files at new-game start** could overwrite a user's settings with defaults; nothing did on the maintainer's setup. Worth repeating on other setups: list files under `Data` and `My Games` modified during a lobby session.
- **Release: lobby cells (five).** The Bannered Mare; Sky Haven Temple (around the Blades' table below Alduin's Wall); High Hrothgar's entrance (`HighHrothgarEntrance`); Sovngarde by the Hall of Valor (`SovngardeMeadHall`); and the Ragged Flagon (`RiftenRaggedFlagon`). The Bannered Mare is the default (the installer's preselected choice and the shipped `sLobbyCell`). All five are the maintainer's own F8 arrangements (the two outdoor ones also set midday and a clear sky, verified not to carry over into a loaded game). Its Thieves Guild upgrades swap some chairs and tables for nicer ones at the same spots (both sets are in the cell, enabled by quest progress); the lobby is always a fresh game, so seats must be the plain `Common*` ones. The FOMOD offers the locations, so player-facing text (README, SAVE_SAFETY, Nexus page, UI) must never assume a particular cell. **Adding a location:** a layout section in `CharSelectLayouts.ini`, a line in `fomod/locations.txt`, and a `<plugin>` in `fomod/ModuleConfig.xml`; `test_fomod.cpp` checks they agree. The package (`Core/`, `Locations/<folder>/`, `fomod/`) is assembled by `cmake/StageFomod.cmake`, which writes each location's `CharSelect.ini` from the shipped one.
- **Release testing:** once a build is release-ready, the maintainer plays with it for about a week before release.
- **For 1.0.0, after the week:** move the PDB out of the main zip into a symbols download; the Nexus page and README screenshots; tag the release (`v1.0.0`), and tag each release after it. The public history stays as it is (not squashed).
- **Ideas, not planned:** a custom Creation Kit lobby cell; "living" cells; shareable layouts; an optional clean restart between characters (a fresh process is the only way to reset other plugins' memory).

## In-game regression checklist

After changes to the lobby flow, check:
1. **Normal flow:** the lobby comes up, PLAY loads, saving works in the game; MAIN MENU and NEW GAME work.
2. **LOAD and cancel:** with a plugin the save needs disabled, LOAD shows the missing-content prompt; cancel returns to the select screen with poses intact; continue loads.
3. **Console:** typing in the console in the lobby does nothing to the lobby; `save test` makes no save.
4. **Editor:** F8, move someone and change a pose, SAVE, restart: the change persists.
5. **Tint:** save a character with war paint in third person; it shows in the lobby.
6. **Repeated switching:** PLAY, MAIN MENU, PLAY another, a few times.
7. **Logs:** no `[error]` or `unexpected error` lines in `CharSelect.log`, with `bDebugLog` on and off.
