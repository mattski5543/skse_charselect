# CharSelect

![Five characters gathered around the fire in the Bannered Mare](docs/images/cover.png)

A character select screen for Skyrim. Instead of the main menu you get all your characters standing together, in their own looks and gear. Pick one, press **Enter**, and you're in their latest save.

## Features

- Your characters look like they do in your saves: face, body, hair, war paint and gear.
- They stand, sit or pose, and you pick how.
- Five locations to choose from when you install, or [your own](docs/CUSTOM_LOBBY.md).
- **Enter** loads a character's latest save.
- **Load**, **New Game** and **Main Menu** still use the game's own menus.
- A layout editor (F8) for moving the camera and characters around and picking poses and seats.
- Keyboard, mouse and controller.
- No `.esp`.

## Requirements

- Skyrim SE or AE. Tested on 1.7.104. 1.6.1170 and 1.5.97 should work but haven't been tested. No VR.
- [SKSE](https://skse.silverlock.org/)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
- [RaceMenu](https://www.nexusmods.com/skyrimspecialedition/mods/19080), a version that works on your game version. CharSelect uses it to rebuild your characters' looks.

## Installing

Install with Vortex or Mod Organizer 2 and start the game through SKSE like normal. The installer asks where your characters should gather: the Bannered Mare, the Ragged Flagon, Sky Haven Temple, High Hrothgar or Sovngarde. You can change it later with `sLobbyCell`.

**First time:** load each character once so CharSelect can record their look. Until then they show up as a hooded figure, and you can still play them.

## Controls

| | Keyboard and mouse | Controller |
|---|---|---|
| Choose a character | Click them, Left/Right, A/D, or 1-9 | D-pad left/right |
| Load their latest save | Enter, E, or click **Enter** | A |
| Pick any save (Load) | S | X |
| New Game, Load, Main Menu, Quit | Down arrow, then Up/Down and Enter | D-pad down, then up/down and A (B goes back) |
| Layout editor | F8 | |

**Main Menu** takes you to the normal main menu. Press F7 there to get the select screen back.

**Layout editor:** press F8 on the select screen. Pick the camera or a character from the list (or click one), then move with the arrows or WASD, turn with Q/E, go up and down with R/F, and tilt the camera with T/G. Hold a key to keep going. For a character you can also set a pose for when they're idle, one for when they're selected, and a seat. **Save & close** (Enter or F8) keeps it, **Cancel** (Esc) throws it away, and **Reset to default** brings back the built-in layout. Your layout is saved in `Documents\My Games\Skyrim Special Edition\SKSE\CharSelect\Layout.ini`.

## Settings

In `Data\SKSE\Plugins\CharSelect.ini`. Delete a line to go back to its default.

| Setting | Default | What it does |
|---|---|---|
| `bReplaceMainMenu` | `1` | `0` keeps the normal main menu. F7 there opens the select screen. |
| `iLobbyHotkey` | `0x41` (F7) | The key that opens the select screen from the main menu. |
| `bDebugLog` | `0` | `1` writes a detailed `CharSelect.log`, for reporting a problem. |
| `sLobbyCell` | your installer choice | Where the select screen is (a cell editor ID): `WhiterunBanneredMare`, `RiftenRaggedFlagon`, `SkyHavenTemple`, `HighHrothgarEntrance`, `SovngardeMeadHall`, or [your own](docs/CUSTOM_LOBBY.md). |
| `sFallbackCell` | `qasmoke` | Used if `sLobbyCell` can't be loaded (a typo, or its mod isn't installed). You get a message when that happens. |
| `iMaxCharacters` | `5` | How many characters to show, 1 to 9, most recently played first. |
| `sStandInOutfit` | black hood, robes, boots | What the hooded figure wears for characters that haven't been recorded yet. |

Each location has a layout that says where the camera and characters go (`CharSelectLayouts.ini`, plus whatever you change with F8). In a location without one, the camera starts wherever the game drops you and the characters line up in front of it, ready for F8.

## Is it safe for my saves?

As far as I can tell, yes. CharSelect never writes or changes a save file, and saving is turned off while the select screen is up. The only thing it changes in your game is putting back data that leaving the select screen resets (see [Compatibility](#compatibility)). I loaded the same save with and without CharSelect on a load order of about 1,000 plugins, saved, and compared the two. Nothing from the select screen made it into the save. The details, and what that test can't tell you, are in [docs/SAVE_SAFETY.md](docs/SAVE_SAFETY.md).

## Compatibility

The select screen is really a separate game running in the background, so your other mods start up in it as usual. Most don't care. The catch is what happens when you leave: Skyrim resets some records back to how they are in their plugin files. That doesn't happen on a normal load in your own game, but it does any time you leave a game started with `coc` from the main menu, which is how the select screen gets going. Mods that change records once per session can lose those changes.

CharSelect deals with this. Right before the select screen's game ends it takes note of what's there, and once your save has loaded it puts back what got reset:

- missing parts of armors and skins, like the hands on a body skin
- skins missing from characters and races
- spells, perks, keywords, head parts and hair colour missing from characters, and keywords missing from races, armor and weapons
- characters that got wiped completely (every spell and perk gone): their spells, perks, keywords, face, head parts and skin tints, plus their factions, AI packages, inventory, outfits, voice and height wherever those came back empty

It only fills in what's missing or empty. It never removes anything or overwrites something your save or another mod has set since, and it doesn't run when you load from inside your own game. With `bDebugLog = 1` it also logs anything else that changed, so new cases can be found.

| Mod | What happened | Status |
|---|---|---|
| A skin mod | Men lost their hands after the select screen. The skin mod fills in its own skins the first time it sees each body skin, and the reset emptied them. | Fixed in 0.9.3 |
| Skins handed out at startup (SPID, other skin distributors, Exquisite NPCs) | Some characters lost their skin for the session. | Fixed in 0.9.3 |
| Characters who live where the select screen is (Mikael and Uthgerd in the Bannered Mare, Lydia from Housecarls Pre-Thaneship) | They came back wiped: no spells, perks or keywords. Lydia lost her hair colour too, so her hair went pink and OBody crashed the game reading it. | Fixed in 0.9.7 |
| RaceMenu, with hair from a light (ESL-flagged) plugin, such as KS Hairdos SMP | The hair showed up as a default one on the select screen, because RaceMenu left that part out. CharSelect now adds back any part of a look that RaceMenu skips. | Fixed in 0.9.10 |

If something looks wrong only after using the select screen, and not when you load the same save from the normal main menu (set `bReplaceMainMenu = 0` to check), please report it with your `CharSelect.log`, ideally with `bDebugLog = 1`. The log has your characters' names and the mods involved, but leaves your Windows user name out of any paths.

## Known limitations

- **Enter doesn't warn about missing mods.** The game's "missing content" warning only shows up through **Load**. If you've removed mods since you last played a character, use **Load** for them.
- **A character who used RaceMenu partway through a playthrough can show up twice**, because Skyrim gives them a new ID. The old one is still under **Load**.
- **Script errors from other mods at startup are normal.** They're setting up in the background game and may complain in the Papyrus log.
- **Not tested yet:** 1.6.1170, 1.5.97, Mod Organizer 2, other main menu mods, ultrawide screens, and more than six characters (the editor's list gets cramped).
- **Mods that change a location** (interior overhauls and the like) can move its furniture, so someone might end up standing instead of sitting. Fix it with F8.

## Uninstalling

Remove it with your mod manager. CharSelect also writes a few files per character that your mod manager doesn't know about. You can delete them any time; they come back the next time you load or save that character with CharSelect installed.

| What | Where |
|---|---|
| Face texture, about 1 MB per character | `<Skyrim>\Data\Textures\CharGen\Exported\CharSelect_<ID>.dds` |
| RaceMenu preset | `<Skyrim>\Data\SKSE\Plugins\CharGen\Exported\CharSelect_<ID>.jslot` |
| Character details, your layout, the logs | `Documents\My Games\Skyrim Special Edition\SKSE\CharSelect\`, plus `...\SKSE\CharSelect.log` and `CharSelect.1.log` (the session before) |

`<ID>` is the 8 characters after the save number in your save file names, like `5E6F7A8B`. With Mod Organizer 2, the files under `Data` land in the overwrite folder. In the two `Exported` folders, only delete files starting with `CharSelect_`: the rest are RaceMenu's. On the GOG version, `Skyrim Special Edition` in the `Documents` paths is `Skyrim Special Edition GOG`.

## Building from source

You need Visual Studio 2026 or 2022 with C++ desktop development, CMake 4.2 or newer (3.26 is enough with Visual Studio 2022), and vcpkg (Visual Studio comes with one).

```powershell
git clone --recurse-submodules https://github.com/mattski5543/skse_charselect.git
cd skse_charselect
$env:VCPKG_ROOT = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\vcpkg"
cmake --preset vs2026
cmake --build --preset release      # builds dist\CharSelect-<version>.zip
ctest --preset release              # runs the tests
```

On Visual Studio 2022, change the generator in `CMakePresets.json` to `Visual Studio 17 2022`. The first build compiles CommonLibSSE-NG, so it takes a while.

How the code fits together, and everything learned about the engine along the way, is in [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).

## Credits

Made by **portalfl0w**. Built on [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG). Thanks to the SKSE team, and to expired6978 for RaceMenu.

Released under the [MIT License](LICENSE).
