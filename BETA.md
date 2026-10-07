# PS5CEMU-HAR 3.0.1 Beta

**Not a release.** A test build of what comes after 3.0.0, kept on this branch (`3.0.1-beta`) so it
can be downloaded and rebuilt. The `releases` branch and the GitHub releases are untouched, and the
app's update check never offers it.

## Download and install

- **[beta/PS5CEMU-HAR-v3.0.1-beta.zip](https://github.com/premohq/PS5CEMU-HAR/raw/3.0.1-beta/beta/PS5CEMU-HAR-v3.0.1-beta.zip)**
  (its SHA-256 is in [beta/SHA256SUMS](beta/SHA256SUMS)).
- Install it as any release: copy its `PPSA99360` folder to `/data/homebrew/PPSA99360`, with your
  HEN set up as [docs/HEN-SETUP.md](docs/HEN-SETUP.md) says. Everything in `/data/ps5cemu` (games,
  saves, settings) is kept. The app shows version **3.0.1**: its version format has room for one
  letter only, which reads as a revision ("3.0.1 B"), so "Beta" is in the file's name.

## What's in it, against 3.0.0

- **The new launcher** ([docs/UI-REDESIGN.md](docs/UI-REDESIGN.md)), opening on a side's Home with
  the game pages, the Library, the new Settings and the Setup check. Hold **L1** as the app starts
  for the classic one.
- **The new driver** (PS5_Mesa `7b59ef2`): **frame pacing** (Settings > Wii U > Graphics; smoother,
  not faster) and **120 Hz output** that reaches the game.
- **Every driver patch** in `patches/mesa` ([docs/DRIVER-PERFORMANCE.md](docs/DRIVER-PERFORMANCE.md)),
  where `make release` on `PS5-Cemu` builds 0006 alone:
  - **0001-0003, on:** less work on Cemu's GPU thread for each submission (the TSC instead of the
    clock's system calls, streamed copies into the ring, cheaper fence checks and waits). This is
    where any speed-up would come from; it has not been measured.
  - **0004 and 0005, built in and off** until `radvEnvironment` in `/data/ps5cemu/ps5cemu.json` turns
    them on: `"RADV_PS5_GPU_TIME": "1"` (the GPU's busy time in the boot log's `[driver]` lines) and
    `"RADV_PS5_GPU_FLIP": "1"` (flips put on the GPU behind each frame).
  - **0006, on:** the refresh rate asked for again once the new launcher is gone, so 120 Hz output
    works after it. With 120 Hz output off (the default, and the only choice on a 60 Hz TV) nothing
    changes.

**None of these has run on a console before this beta**: the new launcher, the driver patches, or
the two together. The performance contract's A/B run ([docs/UI-REDESIGN.md](docs/UI-REDESIGN.md),
4.2) is still to do.

## Known

- Each game compiles its shaders again on its first start: the driver's build id is new against
  3.0.0's. A slower first load, or some stutter, once per game.
- The update check offers only a newer version, so a later official 3.0.1 (the same number) is not
  offered: install it by hand.

## Testing it

- **Speed:** the same game at the same spot, its FPS from the in-game menu's performance overlay or
  the boot log's `[perf]` lines (one every 10 s), against 3.0.0 or against `PS5-Cemu`'s default
  build. Holding L1 as the app starts (the classic launcher) is the same check for the new launcher.
- **Logs:** Settings > Diagnostics > **Copy logs to USB**; they go with any report.
- **Crashes:** [beta/ps5cemu-v3.0.1.elf.xz](beta/ps5cemu-v3.0.1.elf.xz) is the ELF this ZIP's eboot
  was made from, with its symbols. It turns a boot log's `[crash]` lines into function names:

  ```sh
  xz -dk beta/ps5cemu-v3.0.1.elf.xz
  python3 tools/symbolize-crash.py beta/ps5cemu-v3.0.1.elf boot.log
  ```

## Building it

On this branch, `make release` builds this beta: the Makefile has `VERSION := 3.0.1` and
`RADV_PATCHES ?= all`. `make release RADV_PATCHES=0006` builds `PS5-Cemu`'s driver instead.
[docs/BUILDING.md](docs/BUILDING.md) has the requirements and the rest. The ZIP was built from
`PS5-Cemu` at `3ae393c` with those two settings, on Ubuntu 24.04.
