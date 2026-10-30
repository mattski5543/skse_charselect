# Using your own location for the select screen

The select screen can be almost any place in Skyrim: an inn, a hall, a cave, a player home from another mod, or somewhere outdoors. You pick the place, then arrange the camera and your characters in game with the layout editor. No other tools or plugins are needed.

## 1. Find the location's editor ID

Every interior, and many outdoor spots, has an editor ID: a name with no spaces, such as `WhiterunBanneredMare`, `SkyHavenTemple` or `HighHrothgarEntrance`. It's the name the console's `coc` command uses. To find one:

- **UESP:** each location's page lists its editor ID, under "Cell" or "Editor ID".
- **In game:** if you have More Informative Console, open the console and click anything in the room; it shows the cell's editor ID.
- **xEdit or the Creation Kit:** look under Cell (interiors) or the world spaces (outdoors).

To check it, open the console from the normal main menu and type `coc <editor ID>`. If you end up in the right place, it works.

## 2. Point CharSelect at it

In `Data\SKSE\Plugins\CharSelect.ini`, set:

```ini
sLobbyCell = SkyHavenTemple
```

Start the game. If the location can't be loaded, CharSelect uses `sFallbackCell` (the developer test cell, `qasmoke`) instead, shows a message saying so, and the log says why.

## 3. Arrange it with the layout editor

The first time you visit a location with no layout, the camera starts where the game puts you when you arrive (often a doorway), and your characters stand in a row in front of it. Press **F8** to open the layout editor:

1. **The camera first.** Pick **Camera** in the list and move it where you want to look from: arrows or WASD to move, R/F to raise or lower, Q/E to turn, T/G to tilt.
2. **Then each character.** Pick them in the list (or press 1 to 9, or click them) and move and turn them the same way. Directions are as seen from the camera.
3. **Poses and seats.** For a character, use the arrows on **POSE** (what they do at rest), **SELECTED** (what they do when picked) and **SEAT**. The seat list shows the room's own chairs, benches and beds, nearest first. Floor poses such as sitting cross-legged or kneeling don't need a seat: they place their own spot on the floor.
4. **Save & close** (Enter or F8) keeps everything. **Cancel** (Esc) throws away what you changed since opening the editor. **Reset to default** forgets your changes to this location.

Your layout is saved in `Documents\My Games\Skyrim Special Edition\SKSE\CharSelect\Layout.ini`, in a section named after the location. Switch `sLobbyCell` back and forth as you like: each location keeps its own layout.

## Sharing a layout

A layout is just that section of `Layout.ini`, for example:

```ini
[skyhaventemple]
camerax = 3950.9
cameray = 3325.5
...
slot1pose = sit_drink
slot1furniture = 0x000CBF8B|Skyrim.esm
```

Send it to someone, and they paste it into their own `Layout.ini`. Seats are stored as the furniture's form ID and plugin, so a seat from another mod only works for people who have that mod. If the furniture isn't there, the character stands at their spot instead.

## Editing a layout by hand

Everything the editor does can also be written by hand. Positions are in game units (a person is about 128 tall), and headings and angles in radians.

| Key | Meaning |
|---|---|
| `camerax`, `cameray`, `cameraz` | Where the camera stands (the player's feet). |
| `camerayaw`, `camerapitch` | Which way the camera faces, and how far it tilts down. |
| `slot1x` ... `slot1heading` | Where slot 1 stands, and which way they face. Slot 1 is the most recently played character. |
| `slot1pose`, `slot1select` | Pose at rest and when selected (see the list in `CharSelectLayouts.ini`). |
| `slot1furniture` | A seat in the room (`0x<form ID>\|<plugin>`), furniture to place at the spot (`place:0x<form ID>\|<plugin>`), or `none`. |
| `stagex`, `stagey`, `stagez` | Optional: the middle of the group. Seated characters prefer the seats nearest it. |
| `hour` | Optional: the time of day, from 0 to 24 (`12` is midday). Best for outdoor spots. |
| `weather` | Optional: a weather to keep, as `0x<form ID>\|<plugin>` (for example `0x0010A244\|Skyrim.esm`, a clear sky in the snowy mountains). |

`CharSelectLayouts.ini` (next to `CharSelect.ini`) holds the layouts that ship with CharSelect, with comments explaining every pose. Your `Layout.ini` overrides it key by key, so you only need the keys you want to change.

## Good to know

- **People who live there are hidden** while the select screen is up, and only in the background game, so nothing changes in your saves. Outdoors, that's every person and creature within about 4,000 units (roughly a minute's walk).
- **Outdoors, time and weather matter.** A spot that looks great at noon can be dark or snowed in. Set `hour` and `weather` (above) so it always looks the way you arranged it; both only affect the background game.
- **Uneven ground:** outdoors, characters without a spot yet start on the ground in front of the camera. Use R/F in the editor to raise or lower anyone standing in a slope or a step.
- **Mods that change a location** can move its furniture. A character whose seat has gone stands at their spot instead; fix it in the editor.
- **Dark rooms are fine:** a soft light follows whichever character is selected.
