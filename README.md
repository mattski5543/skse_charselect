# CharSelect

A character select screen for Skyrim. Instead of the main menu, you start in a scene with your characters gathered together, each in their own look and gear. Pick one and press **Enter** to load their latest save.

<!-- Screenshot of the lobby goes here. -->

## Features

- Your characters, as they look in your saves: face, body, hair, war paint and worn gear, each standing, sitting or striking a pose.
- A choice of locations for the select screen, picked when you install.
- **Enter** loads a character's most recent save in one click.
- **Load**, **New Game** and **Main Menu** use the game's own menus, so nothing you're used to goes away.
- A layout editor (F8) to move the camera and characters, and to pick each character's pose and seat.
- Keyboard, mouse and controller.
- No plugin file (`.esp`) needed.

## Requirements

- Skyrim Special Edition or Anniversary Edition. Tested on 1.7.104; 1.5.97 should work but hasn't been tested. VR isn't supported.
- [SKSE](https://skse.silverlock.org/)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
- [RaceMenu](https://www.nexusmods.com/skyrimspecialedition/mods/19080), which CharSelect uses to rebuild your characters' looks.

## Installing

Install the archive with your mod manager (Vortex or Mod Organizer 2) and launch the game through SKSE as usual. The installer asks where your characters gather: the Bannered Mare, the Ragged Flagon, Sky Haven Temple, High Hrothgar or Sovngarde. You can change it later (see `sLobbyCell` below).

**The first time,** load each character you want on the select screen once. That's when CharSelect records their look. Until then they appear as a hooded figure, and you can still play them.

## Using it

| | Keyboard and mouse | Controller |
|---|---|---|
| Choose a character | Click them, Left/Right, A/D, or 1-9 | D-pad left/right |
| Load their latest save | Enter, E, or click **Enter** | A |
| Pick any save (Load) | S | X |
| Layout editor | F8 | |

**New Game**, **Main Menu** and **Quit** are in the bottom right. **Main Menu** takes you to the normal main menu; from there, F7 brings the select screen back.

**Layout editor.** Press F8 in the select screen. Pick the camera or a character from the list (or click a character), then move them with the arrows or WASD, turn with Q/E, raise or lower with R/F, and tilt the camera with T/G. Holding a key repeats it. For a character you can also choose a pose at rest, a pose while selected, and a seat. **Save & close** (Enter or F8) keeps your changes, **Cancel** (Esc) throws them away, and **Reset to default** goes back to the built-in layout. Your layout is saved in `Documents\My Games\Skyrim Special Edition\SKSE\CharSelect\Layout.ini`.

## Settings

`Data\SKSE\Plugins\CharSelect.ini`. Delete a line to use its default.

| Setting | Default | What it does |
|---|---|---|
| `bReplaceMainMenu` | `1` | `0` keeps the normal main menu; F7 there opens the select screen. |
| `iLobbyHotkey` | `0x41` (F7) | Key that opens the select screen from the main menu. |
| `sLobbyCell` | your installer choice | The select screen's location (cell editor ID): `WhiterunBanneredMare`, `RiftenRaggedFlagon`, `SkyHavenTemple`, `HighHrothgarEntrance`, `SovngardeMeadHall`, or [your own](docs/CUSTOM_LOBBY.md). |
| `iMaxCharacters` | `5` | How many characters to show, 1 to 9, most recently played first. |
| `sStandInOutfit` | black hood, robes, boots | What the hooded figure wears for characters not recorded yet. |

Where characters stand, and the camera, come from the layout for each location (`CharSelectLayouts.ini`, plus your own changes from the layout editor). In a location with no layout, the camera starts where the game puts you and the characters stand in a row in front of it, ready to arrange with F8.

**Want a different location?** Almost any place works, inside or out. See [docs/CUSTOM_LOBBY.md](docs/CUSTOM_LOBBY.md) for how to set one up and share it.

## Is it safe for my saves?

Yes, in our testing. CharSelect never writes or changes a save, and saving is switched off while the select screen is up. We compared the same save loaded with and without CharSelect on a load order of about 1,000 plugins, and nothing from the select screen ended up in it. The details, and the limits of that test, are in [docs/SAVE_SAFETY.md](docs/SAVE_SAFETY.md).

## Compatibility

The select screen is a short game of its own, started in the background before your save loads, and your other mods run in it as usual. Most never notice. The part that matters is leaving it: when the select screen's game ends, Skyrim resets some records to how they are in their plugin files. A normal load in your own game doesn't do that; it happens whenever you leave a game started with the console's `coc` command at the main menu, which is how the select screen starts. A mod that changes records while the game runs, once per session, can lose those changes.

CharSelect handles this itself. Just before the select screen's game ends, it notes what's there, and once your save has loaded it puts back what the reset took away:

- parts missing from armors and skins, such as the hands of a body skin;
- skins missing from characters and races;
- spells, perks, keywords, head parts and hair colour missing from characters, and keywords missing from races, armor and weapons;
- the whole record of any character it finds reset to blank: face, height, skin tints, factions, AI packages and inventory included.

It only adds back what went missing. It never replaces or removes anything a mod has changed since, and it doesn't run when you load from within your own game. It also writes to `CharSelect.log` anything else that changed across the switch (factions, AI packages, inventories, outfits, looks, texture records), so new cases can be found.

| Mod | What happened | Status |
|---|---|---|
| A skin mod | Men lost their hands after the select screen: it fills its own skins the first time it meets each body skin, and the reset emptied them. | Handled since 0.9.3 |
| Skins handed out at startup (SPID, other skin distributors, Exquisite NPCs) | Some characters lost their skin for the session. | Handled since 0.9.3 |
| Characters in the select screen's location (seen with the Bannered Mare's Mikael, Uthgerd and Housecarls Pre-Thaneship's Lydia) | Their records came back emptied: no spells, perks or keywords, and Lydia had no hair colour, so her hair rendered pink and a script reading it (OBody) crashed the game. | Handled since 0.9.7 |

If something looks wrong only after using the select screen, and not when you load the same save from the normal main menu (set `bReplaceMainMenu = 0` to check), please report it with your `CharSelect.log`.

## Known limitations

- **The Enter button doesn't warn about missing mods.** The game's usual "missing content" warning only appears through **Load**. If you've removed mods since you last played a character, load them with **Load** instead.
- **A character who used RaceMenu partway through a playthrough can appear twice**, because Skyrim gives them a new ID. The old entry is still reachable through **Load**.
- **Script errors from other mods at startup are normal.** The select screen starts a fresh game in the background, so other mods run their setup scripts behind it and may log errors to the Papyrus log.
- **Not tested yet:** Skyrim 1.5.97, Mod Organizer 2, other main menu mods, ultrawide screens, and more than six characters (the layout editor's list gets crowded).
- **Mods that change the select screen's location** (interior overhauls, for example) can move its furniture and leave a character standing instead of sitting. Fix it in the layout editor.

## Uninstalling

Uninstall the mod as usual. CharSelect also writes a few files per character, which your mod manager doesn't know about. They're safe to delete at any time; they come back the next time you load or save that character with CharSelect installed.

| What | Where |
|---|---|
| Face texture, about 1 MB per character | `<Skyrim>\Data\Textures\CharGen\Exported\CharSelect_<ID>.dds` |
| RaceMenu preset | `<Skyrim>\Data\SKSE\Plugins\CharGen\Exported\CharSelect_<ID>.jslot` |
| Character details, your layout, the log | `Documents\My Games\Skyrim Special Edition\SKSE\CharSelect\` and `...\SKSE\CharSelect.log` |

`<ID>` is the 8 characters after the save number in your save file names, such as `5E6F7A8B`. With Mod Organizer 2, the files under `Data` end up in the overwrite folder. When deleting from the two `Exported` folders, only delete files starting with `CharSelect_`; the rest belong to RaceMenu. On the GOG version, `Skyrim Special Edition` in the `Documents` paths is `Skyrim Special Edition GOG`.

## Building from source

You need Visual Studio 2026 or 2022 with C++ desktop development, CMake 3.26 or newer, and vcpkg (Visual Studio includes one).

```powershell
git clone --recurse-submodules https://github.com/mattski5543/skse_charselect.git
cd skse_charselect
$env:VCPKG_ROOT = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\vcpkg"
cmake --preset vs2026
cmake --build --preset release      # builds dist\CharSelect-<version>.zip
ctest --preset release              # runs the tests
```

On Visual Studio 2022, change the generator in `CMakePresets.json` to `Visual Studio 17 2022`. The first build compiles CommonLibSSE-NG and takes a while.

`AGENTS.md` has the detailed notes on how the code is organised and what we've learned about the engine along the way.

## Credits

Made by **portalfl0w**. Built on [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG). Thanks to the SKSE team, and to expired6978 for RaceMenu.

Released under the [MIT License](LICENSE).
