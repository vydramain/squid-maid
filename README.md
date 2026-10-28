# squid-maid

A single-player game for the [3dmppc console](https://github.com/vydramain/3dmppc-polymer), built as a
`.mppcdisc`. The player tilts the pad to roll balls of four sizes on a dome over a flat Earth, which stands on
three elephants and a turtle. Alkoldun hits the refrigerators for beer bottles and drinks bottles worth five beer
points to win: a marked one is worth two, a plain one one.

The in-game texts (Russian, in `src/squid-texts.hpp`) are the user guide.

## Controls

Pad buttons only. The names are the pad's labels, as in the in-game hints.

| Button | Phase | Action |
| --- | --- | --- |
| A (SOUTH) | prepare | get ready; a pad lying flat is level |
| A (SOUTH) | ready | start the round |
| Y (NORTH) | ready, pause, descent pause | make the current pad pose level (gyroscope only) |
| B (EAST) | ready, pause, descent pause, landed | restart |
| X (WEST) | any | level the D-pad keys to zero; the left stick still tilts |
| View | play, descent | pause |
| View | pause, descent pause | resume |
| L4 (upper left rear) | any (debug) | show or hide the debug panel |
| L5 (lower left rear) | play, pause; debug panel open | split every ball of size 2 and up |

## Sound

Effects are the `sfx_*.wav` files in `assets/snd`, resident in sound memory and played by name through the
drive (the console's disc drive, which also streams sound).
The music is the `mus_*.wav` files in `assets/snd`, declared in the `[assets.stream]` section of `disc.toml`: the
burner bakes each 24-bit WAV to PCM, and the drive streams it from the disc, so the music does not take sound memory.

The music follows the mood (`src/squid-music.hpp`):

- menu (phases prepare and ready): `mus_intro`, looping.
- play and pause: `mus_intro_harmonic`, `mus_intro_nasral` and `mus_intro`, in random order, each once per cycle.
- last bottle (at 3 points or more, one bottle can end the round): `mus_riff_full` and `mus_riff_sparce`, the same way.
- descent and landing: `mus_interlude`, looping.

A mood change waits for the playing section to end, or for one more section when the next one is already taken
from the queue (sections are 9.6 s long). Restart returns to the menu and switches to `mus_intro` at once, at the
same position (from its top if that position is past its end). Pause does not change the music.

## Structure

The game is an ECS (entity-component-system: objects carry data components, systems update them each frame) scene:
`scenes/main.scene.toml` (scene format 2.0) holds the objects with their components, and the editor shows them. The
camera is the `camera` object with a `lens`. The `round` object carries the round's state in the `squid_round`,
`squid_view`, `squid_effects` and `squid_music` components.

The frame logic runs in systems: input, round, tilt and bodies in `src/squid-systems-play.hpp`; effects and log in
`src/squid-systems-report.hpp`; music in `src/squid-music.hpp`. The HUD (the on-screen text) and the starry background
are drawn by the disc's 2D layer, not by the scene.

## Running

The disc needs a 3dmppc console with PDK (the console's development kit) 2.3 or a later 2.x: the game uses the
drive's stream queue (`rv_cd_stream_queue`, `rv_cd_stream_switch`) and the pdkkit (the PDK's ECS kit)
of that version.

```sh
3dmppc squid-maid.mppcdisc
```

## Burning the disc

The burner `mppcburner` comes from the console repository of the same version (PDK 2.3 or later 2.x). Build the
disc directory into a disc image:

```sh
mppcburner build <disc-directory> -o squid-maid.mppcdisc
```

## Layout

```
disc.toml   the disc manifest: sources, assets (scenes, effects, models), stream music
src/        the game's C++ sources, burned per disc.toml
assets/     sounds and music (snd), models, and the Earth's picture (tex, used by models/earth.gltf)
scenes/     the game's scene: objects, camera and the round's state
docs/       the v0.1 design document
```
