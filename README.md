# Hearthmind

A colony sim in C (SDL3) where **nobody has a map**. Each settler keeps their own memory
of the world and of how to do things, and those memories fade.

## Build & run

```sh
make run          # needs SDL3 (pkg-config sdl3)
./hearthmind --seed 42
```

## The idea

- **Spatial memory, per settler.** Every tile a settler sees is copied into *their* memory
  with a vividness (`str`) and a stability (`stab`). All AI decisions (where the food is, where
  the stockpile is, which trees are marked, how to walk somewhere) read only from that memory.
- **Forgetting.** Memories fade each second. Seeing a place again *after a gap* raises its stability
  (spacing effect). Sleep consolidates memories, exhaustion erodes them, and traits like
  *Forgetful* or *Sharp memory* change the rate. Fading memories can **drift** to the wrong tile
  before they vanish, so settlers can walk to berries that were never there.
- **Pathfinding on beliefs.** A* avoids obstacles the settler *remembers*. It assumes unknown ground
  is walkable, and fixes its memory when it bumps into reality.
- **Know-how fades too.** Woodcutting, cooking, building and the other skills decay unless practised.
  A forgotten skill is relearned by being **taught** (chatting at the evening fire), **watching**
  someone work, or **trial and error** (slow and botch-prone).
- **Gossip.** Settlers who chat pass on what they remember: where the stockpile is, a fruiting
  bush, a new blueprint. The information arrives weaker and is sometimes out of date.
- **Orders are physical.** Chop/mine/harvest marks and blueprints are objects in the world.
  Settlers only act on them after seeing them or being told.
- **The fog is collective memory.** Once every settler has forgotten a place, it fades from your
  view as well.
- **Mind view (M).** See the world as the selected settler remembers it: sepia, fading, wobbling
  where they are unsure, with objects that may already be gone.

## Controls

| Input | Action |
|---|---|
| Left click / drag | Use tool (select, mark, paint zone, place blueprint) |
| Right click / Esc | Cancel tool / deselect |
| Middle drag, WASD/arrows | Pan |
| Wheel, `-` `=` | Zoom |
| Space, 1–4 | Pause, speed |
| Q C X H Z F B V R | Select, Chop, Mine, Harvest, Stockpile, Farm, Build, Tear down, Cancel |
| 1–6 (in Build) | Wall, Door, Bed, Campfire, Torch, Floor |
| Tab | Next settler |
| M | Mind view of the selected settler |
| F1 | Help |

Hover a tile to see who remembers it, and whether their memory is out of date.

## Dev tools

```sh
./hearthmind --seed 42 --shot 30000 out.bmp [--sel N] [--mind]   # headless: simulate N ticks, screenshot
AUTO=1 TRACE=1 ./hearthmind --seed 42 --shot 30000 out.bmp       # auto-place orders, print colonist trace
BENCH=2 ./hearthmind --shot 100 out.bmp                          # render/sim timings
```

Source: `memory.c` (perception, fading, gossip, skills), `colonist.c` (AI from memory),
`path.c` (A* on beliefs), `world.c`, `render.c` (software renderer), `sprites.c` (procedural art),
`ui.c`, `main.c`.
