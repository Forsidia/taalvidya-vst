# Taal Vidya

A tabla taal composer and sequencer, built with [JUCE](https://juce.com) as a **VST3 instrument** and a **standalone app** for Windows.

Pick a taal (Teental, Keherwa, Ektal, ...), edit it bol by bol, and play it back in your DAW or on its own. Tabla samples are built in, so there is nothing to load.

## Features

- **Taal composer** – the main workspace. Bols sit in rows of 8 beats with a moving playhead. Click to select, double-click to hear a bol, drag to reorder.
- **Built-in taals** – Ardh Trital, Chautal, Dadra, Dhamar, Dipchandi, Ektal, Jhaptal, Keherwa, Rupak, Teental, Tilwada.
- **Your own taals** – create, edit, save and open `.taalv` files.
- **Bol palette** – one click adds a bol after the selected one. Compound bols (Dhage, Tirakita, Terekite, Dhere, Tete, Dhatit) squeeze several strokes into one beat.
- **Text entry** – type a whole taal in the box under the composer.
- **Per-bol control** – name, duration in beats and emphasis in dB.
- **Tempo, pitch and volume** – with reset buttons; pitch is in semitones.
- **Loop, or sync to the host** – follow your DAW's transport and tempo, or use the Play button.
- **MIDI input** – notes from C2 (36) upward trigger the bols in palette order, so you can play it live.

## Text syntax

```
Dha, Dhin, Dhin, Dha, Dha, Tin[0.5], Tin[0.5], -, Ge{+3}
```

| Part | Meaning |
|---|---|
| `,` | separates bols (spaces and `|` also work) |
| `Name[d]` | duration in beats, written as a fraction: `Dha[1/4]`, `Ki[1/3]`, `Dha[3/2]` (default 1). A decimal such as `[0.25]` is accepted and converted to a fraction |
| `Name{+3}` | emphasis in dB, from -24 to +12 |
| `-` or `Rest` | a silent beat |

Durations go down to 1/16 of a beat (0.0625) and up to 64 beats. The box is multi-line: Enter starts a new line, so you can lay a long composition out by hand. Click **Apply** (or click away) to update the taal; your line breaks are kept. An optional leading `Tabla:` is still accepted.

## Keyboard

| Key | Action |
|---|---|
| Space | play / stop (ignored in host-sync mode) |
| Left / Right | select previous / next bol |
| Delete / Backspace | delete the selected bol |

## Bols

Recorded: Dhe, Ge, Ghe, Ka, Ke, Ki, Na, Ra, Re, Ta, Te, Ti, Tin, Tit, Tu, Tun.

Layered from the recordings: Dha (Na + Ge), Dhin (Tin + Ghe), Dhi (Ti + Ge), Kat (Ke + Te).

Compound: Dhage, Dhere, Tete, Tirakita, Terekite, Dhatit.

## Build

Requirements: Windows, Visual Studio 2022 (or Build Tools) with the C++ workload, CMake 3.22+, and git. Ninja comes with Visual Studio.

```
build.bat            Release build
build.bat debug      Debug build
build.bat clean      wipe the build folder first
build.bat install    also copy the VST3 to C:\Program Files\Common Files\VST3 (needs admin)
```

The script clones JUCE 8.0.8 into `libs/JUCE` on first run. Output goes to `dist\`:

- `Taal Vidya.exe` – standalone app
- `VST3\Taal Vidya.vst3` – plugin

To also copy the plugin into a VST folder after each build, set `TABLA_VST_DIR`, or create a `build.local.bat` (git-ignored) containing e.g. `set "TABLA_VST_DIR=D:\VST"`. Close your DAW first, or the copy fails.

## Files

- Your taals are saved in `%APPDATA%\TablaVST\Taals`. Older `.tal` files there still load; new saves use `.taalv`.
- Plugin state in a DAW project stores the current taal and the tempo, pitch and volume settings.

## Project layout

```
Source/      plugin code (see CLAUDE.md for a file-by-file guide)
Samples/     tabla recordings, embedded into the plugin at build time
libs/JUCE/   fetched by build.bat, not committed
```

## Licence and credits

Copyright (c) 2026 [Forsidia](https://forsidia.in) and Shreyak Chakraborty.

Taal Vidya is free software released under the [GNU Affero General Public License v3.0](LICENSE) (AGPLv3). See [NOTICE](NOTICE) for third-party components.

- Tabla samples by [mmiron](https://freesound.org/people/mmiron/packs/8162/) from Freesound, released under CC0.
- Built with [JUCE](https://juce.com), which is available under AGPLv3 as well.
