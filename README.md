# squid-maid

A test prototype (v0.1) of a single-player game for the
[3dmppc console](https://github.com/vydramain/3dmppc-polymer), built as a
`.mppcdisc`. The player tilts a Steam Controller to change the orbital plane of
a ball that keeps flying around the world, and steers it through the
highlighted target among the grey debris balls.

The design is in [`docs/disdoc.md`](docs/disdoc.md).

## Controls

Tilt the pad to steer. The pad was tested with Steam running.
Without a gyroscope the left stick and the D-pad tilt the orbit instead.

| Button | Keyboard | Action |
| --- | --- | --- |
| SOUTH | Space / Z | Calibrate while preparing, start when ready |
| NORTH | V | Recalibrate when ready or paused |
| EAST | X | Free mode |
| WEST | C | Level the reserve tilt to zero |
| VIEW | Tab | Restart; repeat after the result |
| Right bumper | E | Pause and resume |
| Left bumper | Q | Show or hide the debug panel |
| MENU | Esc | Releases the disc |

## Round

1. Hold the pad in a comfortable pose and press SOUTH to calibrate.
2. Press SOUTH to start.
3. Touch the highlighted target among the 8 targets.
4. The result shows the targets hit and the time. Press VIEW, then SOUTH, to play again.

Free mode (EAST): no active target and no hits; touching the debris is counted; the round does not end.

## Layout

```
disc.toml   the disc manifest: sources, sounds, textures and models for the burner
src/        the game's C++ sources, burned per disc.toml
assets/     sounds and models, burned per disc.toml; models/earth.gltf uses the Earth's picture in assets/tex/
scenes/     the game's scene, carried by the disc as an asset per disc.toml
docs/       the design document
```

## Running

Run the disc in a 3dmppc console with PDK 1.2 or a later 1.x; a 1.1 console refuses this disc.

```sh
3dmppc squid-maid.mppcdisc
```

## Burning the disc

The burner `mppcburner` comes from the console repository. Build the disc
directory into a disc image:

```sh
mppcburner build <disc-directory> -o squid-maid.mppcdisc
```
