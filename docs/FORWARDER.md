# Starting a game from a home screen forwarder

A forwarder is a separate small app with its own home screen tile (its own title ID, icon and name).
When it is opened, it launches PS5CEMU-HAR (`PPSA99360`) with launch arguments, and PS5CEMU-HAR
starts that game directly instead of opening its launcher.

## Arguments

| Argument | Meaning |
| --- | --- |
| `--rom <file>` or `--rom=<file>` | The game to start. An absolute path (`/mnt/usb0/wiiu/Zelda.wua`), or a path inside that side's game files folder (Settings: `/data/ps5cemu/games` for the Wii U and `/data/ps5cemu/azahar/games` for the 3DS by default), such as `Zelda.wua` or `RPG/Pokemon.3ds`. A Wii U game in its folder format (`code/`, `content/`, `meta/`) is given as its folder. A relative path may not contain `..`. An Artic Base address (`articbase://192.168.1.20`) is a 3DS game. |
| `--type <wiiu\|3ds>` or `--type=<wiiu\|3ds>` | The emulator: `wiiu` for Cemu, `3ds` for Azahar. Without it, the file's extension decides: `.wua` `.wud` `.wux` `.rpx` `.wuhb` are the Wii U's, `.3ds` `.cci` `.cxi` `.cia` `.3dsx` `.app` `.axf` and the `.z` compressed ones the 3DS's, and a folder is a Wii U game. `.elf` is both emulators', so it needs `--type`. |
| `--exit-after-game` | When that game goes back to the library (the in-game menu), close PS5CEMU-HAR so the console returns to the home screen. Without it, the launcher opens on that game's side, as usual. |

Unknown arguments are ignored. Parsing is in `port/app/forward.h`, checked on a PC by
`tools/forward-check.cpp`; `port/main_ps5.cpp` uses it once the settings are read, so a relative
path resolves against the game files folders chosen in Settings.

A relative path without `--type` and without an extension that says is looked for in the Wii U's
folder first, then the 3DS's; the folder it is found in is its side.

## Behaviour

- The game starts as one chosen in the launcher would: its side is prepared (Cemu's core for the
  Wii U, Azahar's game list for the 3DS), the library's scan finishes first, and its settings,
  graphic packs and controller profiles apply. It becomes the side's last played game. The launcher
  and the update check are skipped.
- The file's folder is added to the folders the app makes sure it can read before it starts
  (`ps5privilege::ReachFolders`), so a game on a USB drive works on a HEN that opens `/data` only.
- Going back to the library starts PS5CEMU-HAR over on that game's side, or, with
  `--exit-after-game`, closes it.
- If the game doesn't start, the launcher shows why, as for a game chosen in it, even with
  `--exit-after-game`.
- If the file isn't found (or `--type` is neither `wiiu` nor `3ds`), a notification says so and the
  launcher opens on that side with the reason. Without `/data` access, the usual notice says why
  nothing can be read.
- Every restart PS5CEMU-HAR makes of itself (back to the library, a game that failed, the classic
  launcher after the new one stopped drawing) runs without arguments, so the forwarded game never
  starts twice.
- Arguments only reach a new PS5CEMU-HAR process. If it is already running, the system brings it to
  the front and `main` does not run again; a forwarder should close it first.

## Example

```
--rom "Mario Kart 8.wua" --type wiiu
--rom /mnt/usb0/3ds/Pokemon.3ds --type 3ds --exit-after-game
```
