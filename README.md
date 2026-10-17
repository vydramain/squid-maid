# squid-maid

A test prototype (v0.1) of a single-player game for the
[3dmppc console](https://github.com/vydramain/3dmppc-polymer), built as a
`.mppcdisc`. The player tilts a Steam Controller to change the orbital plane of
a ball that keeps flying around the world, and steers it through the
highlighted target among the grey debris balls.

The design is in [`docs/disdoc.md`](docs/disdoc.md).

## Layout

```
disc.toml   the disc manifest: sources, assets and budgets for the burner
src/        the game's C++ sources, burned per disc.toml
scenes/     the game's scene, carried by the disc as an asset per disc.toml
docs/       the design document
```

## Burning the disc

The burner `mppcburner` comes from the console repository. Build the disc
directory into a disc image:

```sh
mppcburner build <disc-directory> -o squid-maid.mppcdisc
```
