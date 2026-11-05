# Is CharSelect safe for my saves?

As far as I can tell, yes. Nothing from the select screen ends up in your saves. I checked on a big load order (about 1,000 plugins and 100+ SKSE plugins) by saving the same character twice, once loaded through CharSelect and once with CharSelect switched off, and comparing the two saves in detail. The only differences were the usual small ones you get between any two saves.

## Why it's a fair question

The select screen is a throwaway game running in the background. When you pick someone, CharSelect loads your real save from inside that game.

Loading a save while another game is running is normal for Skyrim (it's what happens every time you die and reload), and the game swaps the world, quests and scripts for what's in your save. The worry is SKSE plugins. They keep their own data in memory, and one that doesn't reset properly on a load could carry something over from the select screen into your game, and from there into your next save.

## What CharSelect does about it

- **It never saves.** Saving is off the whole time the select screen's game runs (the same switch the game uses during character creation), and only comes back on once your save starts loading. Even the console's `save` command gets refused.
- **It never changes your save files.** It only reads the small header at the start of each one (name, level, location) to list your characters.
- **It cleans up after RaceMenu.** RaceMenu remembers the looks it applied on the select screen and doesn't forget them on a load, so CharSelect clears them before you leave.
- **You get your controls and HUD back** as soon as your save starts loading.
- **It puts back what leaving resets.** When the select screen's game ends, Skyrim resets some data back to how it is in the plugin files (a normal load doesn't), and mods that changed that data lose their changes (see Compatibility in the README). After your save loads, CharSelect puts back whatever went missing or empty: parts of armor and skins, and the spells, perks and looks of characters that got wiped. It never removes anything or overwrites something your save or a mod has set. This is the one thing CharSelect changes in your game, and a save made afterwards includes it, same as it would without the select screen.
- **It doesn't touch mod settings.** Over two select screen sessions on my setup, nothing changed in the `Data` folder or in `Documents\My Games\Skyrim Special Edition` apart from a few mods' own logs. So no mod rewrote its settings while starting up in the background game.

## How I tested it

**Setup:** Skyrim AE 1.7.104 with about 1,000 plugins and 100+ SKSE plugins, including RaceMenu and the usual body, physics, animation and distribution frameworks. November 2026, on the release build. The select screen was the Bannered Mare.

**Steps:**

1. Load a save through the select screen (Enter) and save straight away: save **B**.
2. Turn CharSelect off completely in the mod manager, load the same save from the normal main menu and save straight away: save **A**.
3. Compare A and B, and both against the save they were loaded from: every object the save records, the scripts, and what each SKSE plugin keeps in the `.skse` file next to the save.

The save was a level 68 character's long-running game, standing in Riften. Leaving the select screen reset a skin mod's skins, so CharSelect had repairs to make there.

**Results:**

| What was compared | Save A (without CharSelect) | Save B (through CharSelect) |
| --- | --- | --- |
| Objects in the world the save records | 324,735 | 324,738 |
| Scripts stored in the save | 334,139 | 334,136 |
| SKSE plugins with data in the save | 25 | 25 (the same 25) |

- **Nothing from the select screen.** None of its characters, its light, the markers it places for poses, or anything from the Bannered Mare.
- **B has 3 records A doesn't, all from one of Riften's background conversations** (two townspeople chatting in the Grand Plaza). The original save was made while it was running. In B it was still going when I saved, with two of its lines spoken; in A it had already finished. That's also why B has a few more scripts running. Same town, ten seconds of play, slightly different timing.
- **Everything CharSelect repaired is saved exactly the same as without it.** Every character record and every armor record matches between the two saves.
- **The SKSE data matches** apart from the bits that change between any two sessions: a skin distribution mod had set up a slightly different handful of Riften's townspeople in each (all regular Skyrim characters), a JContainers text store, and one other plugin's data, the same size but not identical.

**An earlier run** (October 2026, a level 1 character in Whiterun, with the normal main menu instead of CharSelect switched off) had the select screen's residents come back wiped, so CharSelect repaired 10 characters there. Those 10 were saved exactly the same as without CharSelect too, and nothing from the select screen ended up in that save either. And a first test with 0.9.0 (before any repairs existed), on a level 66 character, came out the same: 310,231 objects in each save and nothing from the select screen.

## What the test can't tell you

- **It only sees what gets saved, not what's in memory.** A plugin could in theory hang on to something from the select screen that changes how the game behaves without ever writing it to a save. I haven't seen any sign of that.
- **It's one load order.** Yours is different, and you can check it yourself (below).
- **It's the same kind of risk as switching characters without restarting the game,** which plenty of people do anyway. CharSelect just adds one more "character" (the select screen) before yours.

## Checking your own setup

You'll need Python 3 and `tools/savediff.py` from the CharSelect source.

1. Start the game with CharSelect on, press **Enter** on a character, save straight away and quit.
2. Turn CharSelect off in your mod manager, load the same save from the normal main menu (one below the save you just made), save straight away and quit. Turn CharSelect back on.
3. Run `python savediff.py <first save>.ess <second save>.ess`. Small differences are normal. Look out for anything that only shows up in the second save: an SKSE plugin, extra scripts, or objects you don't recognise.

## The Enter button doesn't warn about missing mods

If you load a save that used mods you've since removed, the game's own menus warn you first ("missing content"). **The Enter button on the select screen doesn't.** It just loads the latest save. Loading a save without the mods it was made with and then saving can damage it, however you load it.

If you've removed or changed mods since you last played a character, use **Load** (the game's own load menu) or the normal main menu, which both show the warning.

If you'd rather not risk it at all, set `bReplaceMainMenu = 0`. You get the normal main menu, and F7 opens the select screen when you want it.
