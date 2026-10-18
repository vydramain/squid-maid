# squid-maid

A single-player game for the [3dmppc console](https://github.com/vydramain/3dmppc-polymer), built as a
`.mppcdisc`. The player tilts the pad to roll balls of four sizes on a dome over a flat Earth, which stands on
three elephants and a turtle. Alkoldun hits the refrigerators for beer bottles and drinks five marked ones to win.

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

Effects are the WAV files in `assets/snd`. The ambient track, `assets/music/ambient.mp3`, is declared in the
`[music]` section of `disc.toml`: the burner bakes it to PCM and the game streams it.

## Running

The disc needs a 3dmppc console with PDK 1.3 or a later 1.x, because of its `[music]` section. A 1.2 console
refuses it.

```sh
3dmppc squid-maid.mppcdisc
```

## Burning the disc

The burner `mppcburner` comes from the console repository (PDK 1.3 or later 1.x). Build the disc
directory into a disc image:

```sh
mppcburner build <disc-directory> -o squid-maid.mppcdisc
```

## Layout

```
disc.toml   the disc manifest: sources, sounds, music, textures and models for the burner
src/        the game's C++ sources, burned per disc.toml
assets/     sounds (snd), ambient music (music), models, and the Earth's picture (tex, used by models/earth.gltf)
scenes/     the game's scene, carried by the disc as an asset per disc.toml
docs/       the v0.1 design document
```
