# Using your own lobby location

Any named cell can be a lobby: an inn, a hall, a cave, a player home from another mod, or somewhere outdoors. Point the mod at it, then set up the camera and characters in game with the layout editor. No other tools or plugins needed.

## 1. Find the location's editor ID

Interiors, and plenty of outdoor spots, have an editor ID: a name with no spaces, like `WhiterunBanneredMare`, `SkyHavenTemple` or `HighHrothgarEntrance`. It's the name the console's `coc` command takes. To find one:

- **UESP:** each location's page lists its editor ID, under "Cell" or "Editor ID".
- **In game:** with More Informative Console, open the console and click anything in the room. It shows the cell's editor ID.
- **xEdit or the Creation Kit:** look under Cell (interiors) or the world spaces (outdoors).

To check it works, open the console from the normal main menu and type `coc <editor ID>`. If you end up in the right place, you're good.

## 2. Point the mod at it

In `Data\SKSE\Plugins\CharSelect.ini`:

```ini
sLobbyCell = SkyHavenTemple
```

Start the game. If the location can't be loaded, the mod uses `sFallbackCell` instead (the developer test cell, `qasmoke`), tells you so, and the log says why.

Want a change of scene? List several locations, separated by commas, and the lobby picks a different one at random each time it opens:

```ini
sLobbyCell = SkyHavenTemple, WhiterunBanneredMare, MyOwnLocation
```

The installer's **Random** choice does this with the five built-in locations.

## 3. Set it up with the layout editor

The first time you go to a location without a layout, the camera starts wherever the game puts you (often a doorway) and your characters line up in front of it. Press **F8** to open the layout editor:

1. **Camera first.** Pick **Camera** in the list and move it to where you want to look from: arrows or WASD to move, R/F to raise or lower, Q/E to turn, T/G to tilt.
2. **Then each character.** Pick them in the list (or press 1 to 9, or click them), and move and turn them the same way. Directions are as seen from the camera.
3. **Poses and seats.** Use the arrows on **POSE** (what they do when idle), **SELECTED** (what they do when picked) and **SEAT**. The seat list shows the room's own chairs, benches and beds, nearest first. Floor poses like sitting cross-legged or kneeling don't need a seat, they make their own spot on the floor.
4. **Save & close** (Enter or F8) keeps it all. **Cancel** (Esc) throws away what you changed since opening the editor. **Reset to default** forgets your changes to this location.

Your layout is saved in `Documents\My Games\Skyrim Special Edition\SKSE\CharSelect\Layout.ini`, in a section named after the location. Switch `sLobbyCell` back and forth as much as you like, each location keeps its own layout.

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

Send it to someone and they paste it into their own `Layout.ini`. Seats are stored as the furniture's form ID and plugin, so a seat from another mod only works for people who have that mod. If the furniture isn't there, the character stands at their spot instead.

## Editing a layout by hand

Anything the editor does, you can also write by hand. Positions are in game units (a person is about 128 tall), headings and angles in radians.

| Key | Meaning |
|---|---|
| `camerax`, `cameray`, `cameraz` | Where the camera stands (the player's feet). |
| `camerayaw`, `camerapitch` | Which way the camera faces, and how far it tilts down. |
| `slot1x` ... `slot1heading` | Where slot 1 stands and which way they face. Slot 1 is the most recently played character. |
| `slot1pose`, `slot1select` | Pose when idle and when selected (see the list in `CharSelectLayouts.ini`). |
| `slot1furniture` | A seat in the room (`0x<form ID>\|<plugin>`), furniture to place at the spot (`place:0x<form ID>\|<plugin>`), or `none`. |
| `stagex`, `stagey`, `stagez` | Optional: the middle of the group. Seated characters go for the seats nearest it. |
| `hour` | Optional: the time of day, 0 to 24 (`12` is midday). Best for outdoor spots. |
| `weather` | Optional: a weather to keep, as `0x<form ID>\|<plugin>` (for example `0x0010A244\|Skyrim.esm`, a clear sky in the snowy mountains). |

`CharSelectLayouts.ini` (next to `CharSelect.ini`) has the layouts that come with the mod, with comments on every pose. Your `Layout.ini` overrides it key by key, so you only need the keys you want to change.

## Good to know

- **NPCs who live there get hidden** while the lobby is up, but only in the lobby's game, so nothing changes in your saves. Outdoors that's every NPC and creature within about 4,000 units (roughly a minute's walk).
- **Outdoors, time and weather matter.** A spot that looks great at noon can be pitch black or snowed in. Set `hour` and `weather` (above) so it always looks the way you set it up. Both only affect the lobby.
- **Uneven ground:** outdoors, characters without a spot yet start on the ground in front of the camera. Use R/F in the editor to lift or lower anyone stuck in a slope or a step.
- **Mods that change a location** can move its furniture. A character whose seat has gone stands at their spot instead. Fix it in the editor.
- **Dark rooms are fine:** a soft light follows whoever's selected.
