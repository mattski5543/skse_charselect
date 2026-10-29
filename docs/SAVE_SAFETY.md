# Is CharSelect safe for my saves?

**Short answer: yes, in our testing.** Nothing from the select screen ends up in your saves. We checked on a heavy load order (about 1,000 plugins and over 100 SKSE plugins) by saving the same character twice, once after loading through CharSelect and once without it, and comparing the two saves in detail. The only differences were the small ones any two saves have.

This page explains why people ask, what CharSelect does to keep your saves clean, how we tested it, and what the test can't tell you.

## Why people ask

To show your characters, CharSelect starts a separate, throwaway game in the background: the scene you see is that game. When you pick a character, it loads your real save from inside it.

Loading a save while another game is running is something Skyrim does all the time (every reload after a death works this way), and the game replaces the world, quests and scripts with what's in your save. The worry is mods that include their own program code, known as SKSE plugins. They keep their own information in memory, and one that doesn't reset properly when a save loads could carry something from the select screen into your game, and from there into your next save.

## What CharSelect does to keep your saves clean

- **It never makes a save.** Saving is switched off for the whole time the select screen's game is running, using the same switch the game uses during character creation. It's switched back on only once your save starts loading. Even the console's `save` command is refused.
- **It never changes your save files.** It only reads the short summary at the start of each one (name, level, location) to list your characters.
- **It cleans up after RaceMenu.** RaceMenu remembers the looks it applied to the select screen's characters and doesn't forget them when a save loads, so CharSelect clears them before you leave the select screen.
- **It gives you your controls and HUD back** as your save loads, so nothing stays switched off.
- **It repairs what leaving the select screen resets.** When the select screen's game ends, Skyrim resets some of the game's data to how it is in the plugin files, which a normal load doesn't do, and mods that changed that data while the game was running lose their changes (see Compatibility in the README). Once your save has loaded, CharSelect puts back what went missing or empty: parts of armor and skins, and the spells, perks and looks of characters the reset blanked. It never removes anything or replaces a value your save or a mod has set. This is the one thing CharSelect changes in your game, and like any game data, a save made afterwards includes it: the same data you'd have without the select screen.
- **It doesn't change mod settings.** During two select-screen sessions on the test setup, no file changed in the game's `Data` folder or in `Documents\My Games\Skyrim Special Edition`, apart from a few mods' own log files. So no mod rewrote its settings while starting up in the throwaway game.

## How it was tested

**Setup:** Skyrim Anniversary Edition 1.7.104 with a heavy load order: about 1,000 plugins and over 100 SKSE plugins, including RaceMenu and popular body, physics, animation and distribution frameworks. October 2026, with CharSelect 0.9.0, before the repair described above existed; the comparison is being repeated on the release build.

**Method:**

1. Load a long-running save (a level 66 character) from the normal main menu, and save straight away: save **A**.
2. Restart the game, load the same save through CharSelect, and save straight away: save **B**.
3. Compare A and B: every object in the world the save records, the game's scripts, and the information each SKSE plugin stores in its own file next to the save (the `.skse` file).

**Results:**

| What was compared | Save A (normal main menu) | Save B (through CharSelect) |
| --- | --- | --- |
| Objects in the world the save records | 310,231 | 310,231 |
| Scripts stored in the save | 320,304 | 320,303 |
| SKSE plugins with information in the save | 25 | 25 (the same 25) |
| Characters with RaceMenu body shapes saved | 984 | 984 (the same 984) |

- **Nothing in B that isn't in A.** No extra SKSE plugin information, no extra scripts, and none of the select screen's characters.
- **The same objects in the world.** Object by object, everything is in both saves except 8 things the game creates as you play (dropped items, spell effects and the like), and there are 8 of those in each. That's normal turnover between two play sessions. The select screen's characters, its light and the markers it places for poses aren't among them.
- **Every SKSE plugin's information was the same size,** except RaceMenu's body shape for one character from the real game, which was stored slightly differently (normal between two loads). Four pieces were the same size but not identical: SKSE's list of which scripts are waiting for which events, a text store used by JContainers, and two pieces from one other plugin. These hold things like internal ID numbers that change from session to session, so we'd expect that, but we haven't looked inside them.

## What the test can't show

- **It sees what gets saved, not what's in memory.** A plugin could, in principle, hold onto something from the select screen that changes how the game behaves without ever writing it to a save. We've seen no sign of that.
- **It's one load order.** Every setup is different. You can check yours (below).
- **It's the same kind of risk as switching characters without restarting the game,** which many people do already. CharSelect adds one more "character" (the select screen) before yours.

## Checking your own setup

You'll need Python 3 and the `tools/savediff.py` script from the CharSelect source.

1. Set `bReplaceMainMenu = 0` in `CharSelect.ini`, load a save from the normal main menu, save straight away, and quit.
2. Set it back to `1`, start the game, use **Load** on the select screen to load the same save as in step 1, save straight away, and quit.
3. Run `python savediff.py <first save>.ess <second save>.ess`. Small differences are normal. Watch for anything that only shows up in the second save: an SKSE plugin, extra scripts, or objects you don't recognise.

## Known limitation: the Enter button doesn't warn about missing mods

When you load a save from the game's own menus and it used mods you've since removed, Skyrim warns you ("missing content") first. **The Enter button on the select screen doesn't show that warning:** it loads your character's latest save straight away. Loading a save without the mods it was made with, and then saving, can damage it, however you load it.

If you've removed or changed mods since you last played a character, load them with **Load** (the game's own load menu) or from the normal main menu, which show the warning.

If you'd rather not take the chance at all, set `bReplaceMainMenu = 0` in `CharSelect.ini`. You get the normal main menu, and F7 opens the select screen when you want it.
