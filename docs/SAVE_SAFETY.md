# Save safety

Short version: nothing from the lobby ends up in your saves. I tested it on a load order of about 1,000 mods (100+ of them SKSE plugins) by saving the same character twice, once loaded through Character Select and once with the mod turned off, then comparing the two saves in detail. The only differences were the small ones you get between any two saves.

## Why it's worth checking

The lobby is essentially a new game running before yours. When you pick a character, the mod loads your real save from inside that game.

That's normal for Skyrim, it's what happens every time you die and reload, and the game swaps the world, quests and scripts for what's in your save. The worry is SKSE plugins. They keep their own data in memory, and one that doesn't reset properly on a load could carry something over from the lobby into your game, and from there into your next save.

## What the mod does about it

- **It never saves.** Saving is off the whole time the lobby is up (the same switch the game uses during character creation), and only comes back on once your save starts loading. Even the console's `save` command gets refused.
- **It never changes your save files.** It only reads the small header at the start of each one (name, level, location) to list your characters.
- **It cleans up after RaceMenu.** RaceMenu remembers the looks it applied in the lobby and doesn't forget them on a load, so the mod clears them before you leave.
- **Controls and HUD come back** as soon as your save starts loading.
- **It puts back what leaving resets.** Leaving the lobby makes Skyrim reset some data back to how it is in the plugins (a normal load doesn't), and mods that changed that data lose their changes (see Compatibility in the README). After your save loads, the mod puts back whatever went missing or empty: parts of armor and skins, and the spells, perks and looks of NPCs that got wiped. It never removes anything or overwrites something your save or another mod has set. This is the one thing it changes in your game, and a save made afterwards includes it, same as it would without the lobby.
- **It doesn't touch mod settings.** Over two lobby sessions on my setup, nothing changed in the `Data` folder or in `Documents\My Games\Skyrim Special Edition` apart from a few mods' own logs. So no mod rewrote its settings while starting up in the lobby.

## How I tested it

**Setup:** Skyrim AE 1.7.104, about 1,000 mods and 100+ SKSE plugins, including RaceMenu and the usual body, physics, animation and distribution frameworks. November 2026, on the release build, with the Bannered Mare as the lobby.

**Steps:**

1. Load a save through the lobby (Enter) and save straight away: save **B**.
2. Turn the mod off completely in the mod manager, load the same save from the normal main menu and save straight away: save **A**.
3. Compare A and B, and both against the save they were loaded from: every object the save records, the scripts, and what each SKSE plugin keeps in the `.skse` file next to the save.

The save was my level 68 character's long-running game, standing in Riften. Leaving the lobby reset some skins my skin mods had set up, so the mod had repairs to make there.

**Results:**

| What was compared | Save A (mod off) | Save B (through the lobby) |
| --- | --- | --- |
| Objects in the world the save records | 324,735 | 324,738 |
| Scripts stored in the save | 334,139 | 334,136 |
| SKSE plugins with data in the save | 25 | 25 (the same 25) |

- **Nothing from the lobby.** None of its characters, its light, the markers it places for poses, or anything from the Bannered Mare.
- **B has 3 records A doesn't, all from one of Riften's background conversations** (two townspeople chatting in the Grand Plaza). It was already running in the original save. In B it was still going when I saved, with two of its lines spoken. In A it had already finished. That's also why B has a few more scripts running. Same town, ten seconds of play, slightly different timing.
- **Everything the mod repaired is saved exactly the same as with the mod off.** Every character record and every armor record matches between the two saves.
- **The SKSE data matches** apart from the bits that change between any two sessions: a skin distribution mod had got through a slightly different handful of Riften's townspeople in each (all normal Skyrim NPCs), a JContainers text store, and one other plugin's data, same size but not identical.

**An earlier run** (October 2026, a level 1 character in Whiterun, compared against the normal main menu instead of the mod being off) had the lobby's NPCs come back wiped, so the mod repaired 10 characters there. Those 10 saved exactly the same as without the lobby too, and nothing from the lobby ended up in that save either. A first test with 0.9.0 (before any of the repairs existed), on a level 66 character, came out the same: 310,231 objects in each save and nothing from the lobby.

## What the test can't tell you

- **It only sees what gets saved, not what's in memory.** A plugin could in theory hang on to something from the lobby that changes how the game behaves without ever writing it to a save. I haven't seen any sign of that.
- **It's one load order.** Yours is different, and you can check it yourself (below).
- **It's the same kind of risk as switching characters without restarting the game,** which plenty of people do anyway. The lobby is just one more "character" before yours.

## Checking your own setup

You'll need Python 3 and `tools/savediff.py` from the source.

1. Start the game with the mod on, press **Enter** on a character, save straight away and quit.
2. Turn the mod off in your mod manager, load the same save from the normal main menu (one below the save you just made), save straight away and quit. Turn the mod back on.
3. Run `python savediff.py <first save>.ess <second save>.ess`. Small differences are normal. Look out for anything that only shows up in the save from the lobby: an SKSE plugin, extra scripts, or objects you don't recognise.

## Enter doesn't warn about missing mods

If you load a save that used mods you've since removed, the game's own menus warn you first ("missing content"). **Enter in the lobby doesn't.** It just loads the latest save. Loading a save without the mods it was made with and then saving can damage it, however you load it.

If you've removed or changed mods since you last played a character, use the standard **Load** menu, or the normal main menu, which both show the warning. Or set `bReplaceMainMenu = 0` to keep the normal main menu and only open the lobby with F7 when you want it.
