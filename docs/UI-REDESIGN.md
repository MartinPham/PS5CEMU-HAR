# PS5CEMU-HAR: the UI redesign

**Status:** proposal, for discussion · **Covers:** the launcher, the in-game menus, and the code behind
them · **Mockups:** [`docs/ui-redesign/`](ui-redesign/), from HTML sources in
[`docs/ui-redesign/mockups/`](ui-redesign/mockups/)

This document proposes replacing PS5CEMU-HAR's interface with one that feels like it belongs on a
PS5: one app for both emulators, each on its own side, drawn on the GPU at 4K, with motion, sound and
controller conventions taken from the console itself. Every feature the app has today stays. The document covers
what was studied, what the new design is, and what the code change involves, file by file and phase
by phase.

![The proposed Home screen](ui-redesign/01-home.jpg)

<sub>All mockups are 1920 × 1080, the layout canvas the app scales to 4K. The covers are stand-ins drawn
by the mockup kit: the app shows GameTDB's box art, as it does today. Mockup text such as play times and
frame rates is illustrative.</sub>

## Contents

1. [Summary](#1-summary)
2. [The UI today](#2-the-ui-today)
3. [Research: what makes an emulator UI good, on a PS5 and elsewhere](#3-research-what-makes-an-emulator-ui-good-on-a-ps5-and-elsewhere)
4. [Principles](#4-principles)
5. [Cemu and Azahar: two sides of one app](#5-cemu-and-azahar-two-sides-of-one-app)
6. [Screens](#6-screens)
7. [Visual system](#7-visual-system)
8. [Motion, sound and touch](#8-motion-sound-and-touch)
9. [Architecture and code plan](#9-architecture-and-code-plan)
10. [Delivery plan](#10-delivery-plan)
11. [Risks](#11-risks)
12. [Open questions](#12-open-questions)
- [Appendix A: feature parity](#appendix-a-feature-parity)
- [Appendix B: voice and copy](#appendix-b-voice-and-copy)
- [Appendix C: sources](#appendix-c-sources)

---

## 1. Summary

Five changes do most of the work:

1. **Two sides, one app.** With the Wii U side picked, Home and the Library show only Wii U games; with
   the 3DS side picked, only 3DS games. The sides become two states of one shell: one design, one
   codebase, one Settings. A switch at the top left (or one click of the touchpad) moves between
   them, the app remembers the side, and the start screen stays as an option
   ([section 5](#5-cemu-and-azahar-two-sides-of-one-app)).
2. **The launcher moves to the GPU.** Today it is drawn by SDL's software renderer at 1080p, which
   rules out the motion, depth and 4K text that make a console UI feel finished. The proposal is a
   small Vulkan renderer on the RADV driver the app already links, drawing signed-distance shapes and
   text at 3840 × 2160, 60 fps. The same kit draws the in-game menus, so the launcher and the menus
   finally share one look and one codebase ([section 9](#9-architecture-and-code-plan)).
3. **The PS5's grammar.** Cross plays, Circle goes back, Options opens a game's menu, L1 and R1 change
   tabs, the Home row is covers with the focused game's hub under it, and the in-game menu works like
   the PS5's Control Center: the game stays in view. Rumble is punctuation, not noise; the light bar
   takes the system's colour.
4. **The friction people actually hit.** A setup check on first start (most issues filed so far are
   HEN, JIT and folder problems), game names in any script (today anything outside Latin-1 is
   dropped), sorting, filtering and search for big 3DS libraries, settings per game, save-state
   thumbnails, and hold-to-confirm for anything that loses progress.
5. **Nothing lost.** [Appendix A](#appendix-a-feature-parity) maps every feature of today's launcher
   and in-game menus to its place in the new design. The current launcher stays in the build as
   "classic" until the new one has shipped and settled.

Rough size: 12 to 16 weeks of focused work in five phases, each one shippable on its own
([section 10](#10-delivery-plan)).

---

## 2. The UI today

### 2.1 How it is built

```mermaid
flowchart LR
    subgraph Launcher["Launcher (until a game is chosen)"]
        RL["tools/render-layout.py<br/>writes start.rml, main.rml, azahar.rml<br/>and two stylesheets from har.rcss"] --> RML["RmlUi documents<br/>(element ids)"]
        LC["frontend/launcher.cpp<br/>3,694 lines: state machine,<br/>DOM writes by id"] --> RML
        RML --> HOST["frontend/ui_host.cpp<br/>SDL software renderer<br/>1920 × 1080"]
        BG["bubbles.cpp / wave.cpp<br/>backgrounds"] --> HOST
        HOST --> VO["VideoOut<br/>(SDL's PS5 driver)"]
    end
    subgraph Ingame["In a game"]
        SM["app/side_menu.h + menu_canvas.h<br/>(ImGui draw lists)"] --> IG1["app/ingame.cpp<br/>Cemu's ImGui on its Vulkan frame"]
        SM --> IG3["app/ingame3ds.cpp<br/>own ImGui Vulkan backend<br/>in Azahar's frame"]
    end
```

- **Layout** is generated: `tools/render-layout.py` writes the RmlUi documents and two copies of
  `port/frontend/ui/har.rcss`, one per side's colours. `launcher.cpp` fills them by element id
  (`SetText(m_document, "hero-title", …)`), so every screen exists twice: once as Python-generated
  markup, once as the C++ that knows its ids.
- **Drawing** is SDL's software renderer into a 1920 × 1080 surface (`port/frontend/ui_host.cpp:626`),
  ProsperoEden's approach. The history explains why: RmlUi's Vulkan renderer presented frames, but
  nothing it drew reached them (commit `3352086`). `ui_host.cpp` has its own triangle filler because
  SDL's own left seams in translucent shapes, and the 3.0.0 notes mention the Home screen's large rounded
  shapes slowing the background down.
- **The in-game menus** are a different stack: ImGui draw lists in `app/side_menu.h`, styled by hand
  to look like the launcher, with the colours copied from `render-layout.py`'s `THEMES`
  (`tools/render-layout.py:25`) into `menu_canvas.h`'s `kBlue` and `kGold`
  (`port/app/menu_canvas.h:30`). Two definitions of one palette, kept in step by hand.
- **Input** is polled in each place: `launcher.cpp`'s `Input` (repeat after 400 ms, then every 90 ms:
  `launcher.cpp:125`) and `SideMenu::Update` (380 ms, then 90 ms).
- **Sound** decides whether a key press did anything by serialising the whole document before and
  after it and comparing the strings (`Showing`, `launcher.cpp:677`).

### 2.2 What is good, and stays

The current UI is careful, and much of that care carries over unchanged:

- **The words.** Every setting says what it does in one line, Triangle has the rest, and the copy is
  plain and specific ("Off can raise the frame rate, but some games then flicker"). Appendix B keeps
  this voice.
- **Hints everywhere**, at most four, changing with context.
- **Box art first** in the Library, GameTDB facts and the compatibility list on each game's page.
- **L1 and R1 for tabs**, Circle always one step back, a second confirmation for destructive actions.
- **Original music and menu sounds**, synthesised by `tools/render-sounds.py`.
- **A PC preview harness** (`tools/preview-launcher.sh`) that renders every screen to PNG from an input
  script. This proposal leans on it heavily.

### 2.3 Where it falls short

The screenshots below are today's launcher, rendered with the repository's own preview tool (no box
art in the preview's sample library, so the icon fallback shows).

| Start screen | Home |
|---|---|
| ![](ui-redesign/current/start.png) | ![](ui-redesign/current/home.png) |
| **Library** | **Settings** |
| ![](ui-redesign/current/library.png) | ![](ui-redesign/current/video.png) |

1. **A door at every start.** The first thing every fresh start asks is which console, before a
   single game is visible, and changing sides means going back out through that door (Circle on Home
   is "change emulator"). Behind it, Home, Library and Settings are built twice, as two RmlUi
   documents in two stylesheets.
2. **Home shows the least interesting picture.** The hero is the game's icon on a large card
   (`ShowIcon`, `launcher.cpp:987`; box art "is the library's"), so the biggest image on the
   screen is a 128-pixel Wii U icon or a 48-pixel 3DS one, scaled. A third of the screen is a
   decorative circle.
3. **Nothing moves.** Focus jumps, screens swap in one frame, dropdowns appear. On a PS5, where every
   system screen glides, that reads as unfinished, and jumps make it harder to see where the focus
   went. The software renderer is the limit: full-screen alpha at 60 fps on the CPU leaves no room
   for motion, blur or 4K text.
4. **Text is a 1080p bitmap, Latin-1 only.** The fonts are pre-rendered atlases at fixed sizes, drawn
   at 1080p and scaled up to the 4K output. `Printable` (`launcher.cpp:159`) drops every character the atlases lack, so a Japanese
   3DS game's title, or any CJK, Cyrillic or Greek name, shows as nothing.
5. **Names are cut mid-word** in the grid and on Home's shelf ("The Legend of Zelda: The"), and the
   focused game's name is printed in a strip at the bottom of the screen, far from the focus.
6. **Focus is a thin ring.** A 4-pixel accent outline on a dark tile is easy to lose from a sofa,
   especially on the gold side; nothing lifts or brightens.
7. **Settings rows change height on focus** (84 to 126 pixels, `har.rcss` `.srow.focused`), which
   shifts every row under them as you scroll. Values are plain text: nothing shows that Left and
   Right change "Off", or what the other choices are.
8. **No sorting, filtering or search.** Fine for eleven Wii U games; slow for a 200-game 3DS
   collection on a 7 × 3 grid.
9. **Setup problems hide in Diagnostics.** HEN, JIT and folder questions are most of the issue
   tracker (#11, #12, #19, #22), yet the app's own diagnosis is a block of text under Settings >
   Diagnostics.
10. **The in-game menus are a third design.** They borrow the launcher's colours but not its code,
    so every change is made twice, and the Wii U's and 3DS's menus already differ (only the 3DS's
    pauses the game; only it has save states, and they show no picture of what was saved).

None of these is a bug in isolation. Together they are the difference between a careful tool and
something that feels like console software.

---

## 3. Research: what makes an emulator UI good, on a PS5 and elsewhere

The research covered the PS5 homebrew field as it stands in October 2026, the big-screen modes of
desktop emulators, multi-system frontends, console system UIs, published TV design guidance, and
what people say about all of them, including this app's own issue tracker. Sources are in
[Appendix C](#appendix-c-sources).

### 3.1 The PS5 homebrew field

| Project | What its UI does | What to take |
|---|---|---|
| **PS5SX2** (PCSX2 on PS5) | A 3D cover-flow shelf; covers download on first start; a **QR code on the shelf** opens a settings page on your phone; settings for all games or one; achievement unlocks pop up **like PS5 trophies**, with the trophy sound; touchpad left/right halves usable as buttons | Art-first shelf; per-game settings; the phone for long forms; system-like notifications; the touchpad as two zones |
| **RetroArch PS5** (Mihawk) | A pre-screen choosing RetroArch's XMB or EmulationStation, remembered with Square, back with L1 held at start; a browser **WebUI** for uploads and settings | A remembered choice instead of a gate; a companion page for files |
| **ProsperoEden** (Eden on PS5; this launcher's ancestor) | An **animated launcher drawn with OpenGL**, sound effects, a loading screen, the connected controllers on Home; **profiles**; **settings per game** where each value says "Default" until changed; the PS5's **system language** (29 languages, CJK with the console's own fonts); **larger text, high contrast and reduce motion** | GPU drawing; per-game settings with inheritance; accessibility switches; Unicode with system fonts |
| **ps5-homebrew-ui** (BlackBearReloaded; Home already credits its designs) | 21 reference screens, one OpenGL 4.6 shader drawing every shape and glyph as signed-distance fields, springs for focus, frosted glass, a 42-cue sound set; **measured at 4K: every design held 60 fps**, no frame over 21 ms | The craft rules (below), the rendering approach, and proof that a PS5 can do this comfortably |
| **Nativehbl** | An SDL2 software launcher with JSON skins and per-app music preview | Software drawing caps how far it can go, as it does here |

ps5-homebrew-ui's [`docs/CRAFT.md`](https://github.com/blackbearreloaded/ps5-homebrew-ui/blob/main/docs/CRAFT.md)
is the closest thing to a style guide for PS5 homebrew, and it is specific: lay out on a 1920 × 1080
canvas; keep readable content 96 pixels from the sides and about 60 from the top and bottom; body text
24 to 28, nothing under 20; exactly one focus, found in under a second, never shown by colour alone;
focus moves settle in 120 to 200 ms on springs, sheets in 250 to 350 ms, screens in 400 to 500;
entrances stagger 30 to 90 ms; a list's end answers with a soft refusal but stays quiet on a held
direction; rumble only for refusals and launches; destructive actions focus the safe choice; and
"sixty frames per second, always". Its performance notes add the costs: a first frame about 5.8 s
after opening an OpenGL display (shader builds), so **don't close the display for something short**.

### 3.2 Big-screen emulator UIs

- **PCSX2 2.0's Big Picture mode** and **DuckStation's fullscreen UI** are both Dear ImGui screens built
  for a controller on a TV: a horizontal main menu, a game grid, per-game settings, achievements.
  PCSX2's is about 4,200 lines (`pcsx2/ImGui/FullscreenUI.cpp`) on a 1280 × 720 layout scaled to the
  screen. They do the job without a mouse, and are liked for it; what they lack is a home: they open
  on a menu and read as one. **Take:** a layout canvas scaled to the
  output, controller-first everything, per-game settings. **Avoid:** making the home screen a menu.
- **RetroArch** is the counter-example everyone cites. Its forums have years of "interface
  confusing" and "hard to learn for newbies" threads: menus inside menus, options with no context,
  backtracking to find things. XMB, its PS3-style menu, was replaced as the default by Ozone in 1.8.5
  because XMB "looked pretty, but wasn't very accessible": you had to know what you were looking for.
  **Take:** looking like a console is not enough; every screen must explain itself.
- **xemu** wraps the whole emulator in a polished ImGui shell. **Take:** ImGui-class tooling can look
  finished when the design is deliberate.

### 3.3 Multi-system frontends

- **ES-DE**, **Playnite's fullscreen mode** and **LaunchBox's Big Box** exist because people want one
  library across many systems, art-first, driven by a controller. ES-DE's classic flow is a carousel
  of systems, then a game list: a step that adds nothing when you have two systems. Big Box leans on
  high-impact visuals; Playnite on breadth and openness. **Take:** art first and controller first; and
  since a carousel of two systems is a step with two stops, moving between our two sides must be one
  press, not a screen.
- **Steam's Big Picture**, rebuilt from the Steam Deck UI, was praised for its controller-first home
  with "continue playing" and universal search, and criticised for "sub-menu hell": two to five more
  presses for common tasks than the old one. **Take:** the home row; **avoid:** burying frequent
  actions.
- **Delta** (iOS) is the emulator people call "a masterclass in design": artwork first, clean and
  intuitive. A design critique of it found one concrete flaw: the save-state and load-state icons
  are too alike at a glance. **Take:** artwork first; **check:** opposite actions get opposite shapes.
- **RetroPass** on Xbox Dev Mode copies the Game Pass layout on purpose, for familiarity. **Take:** on
  a console, the console's own conventions are a feature.

### 3.4 Console system UIs

- **PS5.** A layered, card-based home; each game has a hub; Sony's UX lead described the goals as
  "measured in milliseconds across the entire UI". Its Activities cards are the part people
  criticise: distracting, a to-do list that takes the fun out of exploring. The console also offers
  haptic feedback during menu navigation. **Take:** hubs, cards that are shortcuts, speed;
  **avoid:** cards that nag.
- **Nintendo Switch.** Nintendo's CEDEC 2018 talk on its home menu: the design resources are under
  200 KB so it stays snappy; animations as short as possible while still reading; fewer steps (the
  quit dialog defaults to Yes); the NES's immediacy as the model. **Take:** short animations, fewer
  steps, instant input.
- **Wii U and 3DS** themselves gave this app its heritage: the Homebrew Launcher's bubbles and the 3DS
  launcher's waves, and music in the spirit of their shop and setup screens. **Keep it**, as accents.

### 3.5 TV design guidance

- Microsoft's *Designing for Xbox and TV*: keep content in the TV-safe area; a standard focus
  rectangle is too faint from ten feet; show about as much as a phone would, not a desktop.
- Android TV: 5 % margins (48 × 27 dp on a 960 × 540 design grid, so about 96 × 54 pixels at 1080p),
  design at one grid and scale, let backgrounds bleed past the safe area.
- Smashing Magazine's 2025 TV series: the D-pad, select and back are the evergreen core; design for
  them first.

### 3.6 What this app's users run into

From the issue tracker (16 issues as of 2026-10-05):

- **Setup:** #11 and #12 (JIT and the HEN's app jailbreak list), #19 (fixed by adding the title ID to
  etaHEN's list), #22 (the app does not start on some firmware and HEN combinations). People cannot
  tell what the app got from their HEN. → **A setup check that says it plainly, with the fix.**
- **Controls:** #19 also: Create opens the PS5's own menu, so it cannot be the Wii U's Select, and the
  touchpad could not be mapped. → **The mapping screen marks system-reserved buttons and offers the
  touchpad's zones.**
- **Portability:** #21 asks for the data folder on a USB drive. → Out of the UI's scope, but the
  Settings structure leaves room for it under *Games and folders*.
- **Packs and cheats:** #24 (a graphic pack with a `content/` folder crashes) and #20 (cheats crash 3DS
  games). → **The UI says what a pack replaces and when it applies**; a crash on launch offers to
  start the game once without its packs or cheats.
- **Requests that became features:** #17 (3DS region), #18 (emulated USB devices). The new Settings
  structure has a place for the next ones.

### 3.7 What it adds up to

Good emulator UIs on a TV share five traits: **the games are the interface** (art first, the system
as context, never a maze); **they speak the host console's language** (buttons, layouts, sounds people already know);
**every state is visible and explained** (one focus, real progress, plain errors); **they are fast
and alive** (input on the frame it arrives, motion that shows where things went, 60 fps); and
**depth is available but never in the way** (per-game settings and graphic packs behind Options, not
on the home screen). The principles below are those traits made into rules.

---

## 4. Principles

Each principle has a test a screen must pass before it ships.

| # | Principle | The test |
|---|---|---|
| P1 | **Games first; the other side one press away.** Each side's Home and Library are its games, art first. | Switching sides is one press from Home or the Library; no screen asks "Wii U or 3DS?" except the first start, or with *Ask each time* on. |
| P2 | **One focus, always found.** Exactly one thing is focused, lifted, ringed and lit. | A new player finds the focus within a second on any screen, in high contrast mode too. |
| P3 | **Nothing teleports.** Focus glides, screens assemble, content cross-fades. | Every change of focus, screen or value has a motion; Reduce motion turns each into a short fade. |
| P4 | **The PS5's grammar.** Cross, Circle, Options, L1/R1, the touchpad, the Create button left to the system. | A PS5 owner can use every screen without reading a hint. |
| P5 | **Every action answers.** Sound for moves, choices, backs and refusals; rumble only for refusals and launches. | Pressing anything produces a response within one frame. |
| P6 | **Show, don't describe.** Layouts, borders, resolutions and screens are previewed, not named. | Any setting with a visual result shows that result next to it. |
| P7 | **Say what is true.** Measured progress, real state, specific errors with the fix. | No spinner without a number or a reason; no error without a next step. |
| P8 | **Fast is a feature.** Input acts on the frame it arrives; 60 fps; nothing blocks a frame. | The console tour logs no frame over 21 ms; the first frame shows within 1.5 s of the launcher opening. |
| P9 | **Respect the game.** The in-game menu covers only what it must and pauses where the emulator can. | The game stays visible behind every in-game screen. |
| P10 | **Fewer, better words.** The current voice: short, plain, specific. | Every row's one-line description fits on one line at 26 px. |
| P11 | **Accessible by default.** Larger text, high contrast, reduce motion; hold-to-confirm instead of double presses. | Every screen passes the checklist in all three accessibility modes. |

---

## 5. Cemu and Azahar: two sides of one app

### 5.1 The decision

The app keeps its two sides. With the Wii U side picked, Home and the Library show only Wii U games;
with the 3DS side picked, only 3DS games. What changes is everything around them: the two sides
become two states of one shell (one design, one codebase, one Settings, one set of overlays), and
moving between them takes one press instead of a trip back through the start screen.

| Option | What it is | For | Against |
|---|---|---|---|
| **A. Today** | Two launchers behind a start screen | Familiar | A door at every fresh start; switching means going back out through it; every screen built twice (two RmlUi documents, two stylesheets) |
| **B. One mixed library** | Both systems' games in one shelf, the system as a badge and a filter | One place for everything | A 200-game 3DS collection buries a handful of Wii U games; each side loses its identity; Wii U games would have to be listed without Cemu |
| **C. Two sides of one shell** (adopted) | A side switch in the bar, like the PS5's Games and Media; each side's Home and Library show only its games | Each side keeps its focus and colours; one press to switch; one codebase | The switch must be fast and obvious (5.3) |

**C** is the design. The PS5 works the same way: Games and Media are two spaces of one home screen,
switched at its top left.

### 5.2 How each side looks like itself

1. **The accent:** sky blue on the Wii U side, sand gold on the 3DS side, today's colours, for the
   focus glow, the switch, kickers and the side's settings.
2. **The backdrop:** the focused game's own picture (a Wii U game's boot screen, a 3DS game's
   screenshot: 7.4) over the side's motif, bubbles on the Wii U side and waves on the 3DS side.
3. **The covers:** the Wii U's tall 5:7 cases, the 3DS's shorter and wider ones.
4. **The light bar:** blue on the Wii U side, gold on the 3DS side.

Switching sides cross-fades all four over 600 ms (Reduce motion: a 150 ms fade). The start screen's
"seam", bubbles meeting waves, stays as the brand's mark, on the Setup check and the side chooser.

### 5.3 Picking and switching sides

- **The switch** sits at the left of the bar: *Wii U | 3DS*, the side you are on lit in its colour.
  Up to the bar, Left onto the switch, then Left, Right or Cross.
- **One press:** a click of the touchpad on Home or the Library switches sides. In a game the
  touchpad is already the app's own key (click + Options opens the menu), so it is the app's key in
  the launcher too. The hint row says so: *[touchpad] Nintendo 3DS*.
- **Remembered:** the app opens on the side last used; after a game, on that game's hub.
- **First start:** after the Setup check, the app opens on the only side with games, or asks once,
  with today's two cards restyled, when both have some.
- **Ask each time:** Settings > General > Start on offers *The side last used* (the default) or *Ask
  each time*, which is today's start screen.
- **Settings is shared:** General first, then the side you are on, then the other side, so either
  side's settings are reachable without switching.

### 5.4 Navigation map

```mermaid
flowchart TD
    Boot([App starts]) --> First{"First start,<br/>or a check failed?"}
    First -- yes --> Setup[Setup check]
    First -- no --> Side{"Start on"}
    Setup --> Side
    Side -- "the side last used" --> Home
    Side -- "ask each time" --> Chooser[Side chooser] --> Home
    Home -- "touchpad or the switch" --> Other[The other side's Home]
    subgraph Tabs["L1 / R1"]
        Home --> Library --> Settings
    end
    Home -- "Cross on a cover" --> Launch([Launch])
    Home -- "Down / Game hub" --> Hub[Game hub]
    Library -- Cross --> Launch
    Library -- "Options" --> Menu[Game menu]
    Hub -- Cross --> Launch
    Hub --> Packs[Graphic packs]
    Hub --> GameSettings[Game settings]
    Menu --> Hub & Packs & GameSettings
    Settings --> Pages["Controllers · Button mapping<br/>Folders · Installs · Artic Base<br/>Diagnostics · About"]
    Launch --> Game([Game])
    Game -- "touchpad click + Options" --> Quick[Quick Menu]
    Quick -- "Quit to the library (hold)" --> Restart(["App starts over<br/>on that game's hub"])
```

Overlays sit above any screen: the dropdown picker, a setting's help (Triangle), the update sheet,
toasts, and the on-screen keyboard.

### 5.5 Under the hood

- **Each side prepares as today** (`prepare`, `port/main_ps5.cpp:149`): the Wii U side starts Cemu's
  core once per session (guest memory, system threads, graphic packs, its scan); the 3DS side starts
  Azahar's library scan. Moving between sides never restarts the app, as today. One emulator per
  session stays true: a core only runs a game, and the app starts over after every game.
- **A launcher-side catalogue** (`port/app/catalog.{h,cpp}`) keeps each side's last game list in
  `/data/ps5cemu/library.json`: title ID, name (all scripts), path, format, version, update and DLC,
  GameTDB ID, box-art path, backdrop picture (7.4), two ambient colours, last played, play time,
  last-session frame rate, and a per-game pack summary. A side draws from it at once while its scan
  runs; the scan's list replaces it when it finishes.
- **Starting Cemu** takes a few seconds the first time the Wii U side opens in a session. Today the
  start screen shows "Starting Cemu" on its card meanwhile; the new side shows it on its Home, already
  drawn from the catalogue. Whether `InitializeCore` can run off the launcher's thread, so the screen
  stays live meanwhile, is an open question (section 12); if it cannot, the screen holds, as today.
- **Wii U boot screens** can be read while Cemu's core is up on the Wii U side, the way
  `port/app/covers.cpp` reads icons (7.4).

### 5.6 Buttons

| Where | Cross | Circle | Square | Triangle | Options | L1 / R1 | L2 / R2 | Touchpad |
|---|---|---|---|---|---|---|---|---|
| Home row | Play | — | — | — | Game menu | Tabs | — | Switch side |
| Home, below the row | The focused button | Up to the row | — | — | Game menu | Tabs | — | Switch side |
| Library | Play | Home tab | Sort | Search | Game menu | Tabs | Previous / next letter | Switch side |
| Game hub | The focused button | Back | — | — | Game menu | Previous / next game | — | — |
| Settings | Choose / open | Back | Back to *Default* (game settings) | More about it | — | Tabs | Previous / next section | — |
| Dropdown, help, sheets | Choose | Cancel | — | — | — | — | Page up / down | — |
| Quick Menu (in a game) | Choose | Back, then close | — | — | Close | Previous / next slot | — | — |

Left and Right change a focused value everywhere. Create is never used: the system owns it. In a game
the touchpad stays the shortcut key, as now (click + Options, L1, R1).

---

## 6. Screens

### 6.1 Home

![Home](ui-redesign/01-home.jpg)

**Purpose:** back into a game in one press.

- **The row:** the side's recent games, newest first, the focused cover larger (PS5 home style), then
  *All games* to the Library. The current app keeps four recent games per side; the catalogue keeps
  twelve per side.
- **The hub preview** under the row: the system badge and "Continue · last played …", the name at
  display size (two lines at most, never cut mid-word: it wraps or shrinks one step), publisher, year,
  play time, the compatibility status, then **Play**, **Game hub** and **…** (the Game menu).
- **Glance cards** along the bottom: shortcuts into the game's state, never tasks. Graphic packs
  (count and names), Controllers (who is which controller), How it runs (status, average frame rate
  of the last session from the `[perf]` lines the app already logs), Updates and DLC. Each opens its
  page; a card with nothing to say is left out. The cards read the catalogue's saved summary (5.5), so
  they show at once, before the side's scan finishes.
- **The backdrop** takes the focused cover's two ambient colours (7.4), eased over 600 ms.
- **States:** first start opens the Setup check instead; no games shows a single card, "Your games go
  here", with *Choose a folder* focused; a launch error from the last session shows as a card above
  the row with the reason and *Try without graphic packs* or *Try without cheats* where those were on.

### 6.2 Library

![Library](ui-redesign/02-library.jpg)

**Purpose:** every game on this side, found fast.

- **Filters** across the top: *All*, *Recently added*, *Favourites*, each with its count; on the
  Wii U side also *Graphic packs on*. Left and Right on the filter row, or Up from the first shelf. On
  the 3DS side the shelf's first tile is *Play from your 3DS*: Artic Base, which today has a button on
  the 3DS side's Home.
- **Sort** (Square): *Recently played*, *A to Z*, *Release year*, *How it runs*. **Search** (Triangle)
  opens the on-screen keyboard; results narrow as you type.
- **The shelf**: covers at their natural shapes on a shared baseline, rows of equal height. The
  focused cover lifts 7 %, gains the white ring and glows in its own colour; its name, system,
  publisher, year and status sit right under it, not in a strip across the screen.
- **Navigation:** Left and Right move along a row; Up and Down go to the cover in the next row whose
  centre is nearest the one you started from, remembering that horizontal position so repeated Up and
  Down don't drift (the "goal column" text editors use). L2 and R2 jump to the previous or next
  letter (A to Z) or year; the index on the right shows where you are.
- **Loading:** covers decode on a worker thread and fade in over a placeholder in the cover's ambient
  colours; the catalogue's saved list draws before the scan finishes, with "Looking for new games…"
  in the filter row until it does.

### 6.3 Game hub

![Game hub](ui-redesign/03-game-hub.jpg)

**Purpose:** everything about one game, and its settings, in one place. Replaces *Details*
([today's](ui-redesign/current/details.png)).

- The cover large, with the backdrop in its colours; the neighbouring games peek at the screen's
  edges, with L1 and R1 named at the top.
- Kicker: system badge, GameTDB ID, region, title ID. Title. Chips: status, version or update, DLC,
  format, play time.
- **Actions:** *Play*, *Graphic packs* (Wii U, with the count on), *Game settings*, *…*.
- **About** (GameTDB's description, scrolls with Up and Down when the actions don't have the focus)
  and **facts** (developer, publisher, released, genre, players, rating).
- **How it runs:** the compatibility list's status and note, and the last session's average frame
  rate.

### 6.4 The Game menu (Options)

The PS5 puts a game's secondary actions behind Options; so does this. It opens as a small sheet next
to the focused cover, from Home, the Library or the hub:

*Play* · *Game hub* · *Graphic packs* (Wii U) · *Game settings* · *Start without graphic packs* (Wii U,
when some are on) · *Start without cheats* (3DS, when some are on) · *Look for its box art again* ·
*Show where it is* (the path, and the drive).

### 6.5 Settings

![Settings](ui-redesign/04-settings.jpg)

**Purpose:** everything the app can change, in one list, with each setting showing its effect.

**Structure** (every row of today's two Settings tabs has a place; Appendix A has the mapping). The
section of the side you are on comes right after General, the other side's after it:

| Section | Pages |
|---|---|
| General | Display · Sound · Controllers · Games and folders · Online and updates · Accessibility |
| Wii U | Graphics · Controllers · USB devices · Install updates and DLC |
| Nintendo 3DS | Graphics · Screens and borders · Controls · System and Home Menu · Install CIA files · Artic Base |
| Help | Setup check · Diagnostics · About |

**Rows are controls, not text:** a toggle for on/off; a slider for volumes and deadzones (its pitch
rises with its value); a stepper with pips for the 3DS's internal resolution, with the resulting size
("2400 × 1440"); segmented choices for two or three options (*Top | Bottom*); swatches for borders;
pictograms for screen layouts. The focused row grows a help line **inside the same height budget**
(rows reserve it), so nothing below moves. Triangle still opens the longer help.

**Previews:** pages with a visual result show it on the right: the 3DS screens in the chosen layout on
the chosen border, the Wii U's TV and GamePad arrangement, the effect of an upscaling filter on a
sample.

**Game settings** use the same pages, scoped to one game, as ProsperoEden does: every value shows
*Default (6×)* until changed, going past the last choice comes back to *Default*, and Square resets
the row. What a game can override: Wii U upscaling, async shaders, accurate barriers, controller
types; 3DS internal resolution, texture filter, layout, main screen, border, CPU clock, speed limit,
A and B. They live in `ps5cemu.json` under `games.<title ID>`.

### 6.6 Pages kept from today, rebuilt

All of these keep their behaviour; they move onto the new kit and the two-column page pattern (list
left, detail right) the current app already uses well.

- **Graphic packs** ([today's](ui-redesign/current/packs-presets.png)): the folder tree, on/off, presets
  as dropdowns, "choosing one turns it on". New: a chip on packs that **replace game files** ("Applies
  at the next start · replaces game files"), so a content pack is never a surprise (#24).
- **A player's controls** and **button mapping:** the emulated controller (with "Cemu has two GamePads
  at most"), motion, vibration, deadzones, buttons. New: the mapping screen draws a DualSense with the
  pressed input lit; Create is marked "used by the PS5"; the **touchpad's left and right halves** are
  inputs (#19); a mapping that takes a button already in use offers to swap the two.
- **Folder browser and installs:** drives first, folder counts ("12 games here", "keys.txt found"),
  the install's measured progress with Circle to cancel and what cancelling does.
- **Artic Base:** the 3DS's address edited a number at a time, as now, plus a **numeric keypad** sheet;
  connect; Artic Setup for an Old or a New 3DS, held to confirm.
- **Diagnostics:** the same facts as cards with status icons (HEN, JIT, /data, firmware, version), *Copy
  logs to USB*, *Clear shader caches* (held to confirm) and the Setup check.
- **Update sheet:** the release's notes before you decide (ProsperoEden shows them), the download as a
  measured bar, *Restart now*.

### 6.7 Setup check

![Setup check](ui-redesign/06-setup-check.jpg)

**Purpose:** answer "why doesn't it work?" before anyone opens an issue.

Shown on the first start, whenever a check fails at start, and from Settings > Help. Each check has a
status (ready, needs a look, optional) and, when focused, what it means, what to do, and a QR code to
the right part of the README or HEN guide:

| Check | Ready | Needs a look |
|---|---|---|
| Storage | `/data` reachable, free space | Not reachable: "Add PPSA99360 to your HEN's app jailbreak list" with the HEN guide's QR |
| Recompilers | JIT or executable memory found | Interpreter only: slower; which HEN setting gives it |
| Wii U games | *n* games in *folder* | None found: choose a folder (Cross) |
| 3DS games | *n* games in *folder* | None found: choose a folder |
| Wii U disc keys | `keys.txt` found | Missing: only encrypted `.wud`/`.wux` need it |
| 3DS keys | `aes_keys.txt` found | Missing: only encrypted dumps need it |
| 3DS system files (optional) | Artic Setup has run | Not yet: what needs them (Home Menu, some games) |
| Box art | GameTDB reachable | Offline: icons are shown instead |

All of these facts exist today (`ps5privilege::Result`, the folder counts in `launcher.cpp`, the
diagnostics lines); the check puts them in one place, in words.

### 6.8 Launching, and coming back

The current loading screen is the game's icon and "Starting" ([today's](ui-redesign/current/launch.png)).
The new one:

- The focused cover flies to the centre and grows while the rest of the screen dims (420 ms), the
  launch chime plays, the music ducks, and a short rumble marks it.
- A caption says what is happening, with numbers where they exist: "Starting Cemu", "Connecting to
  192.168.1.20" (Artic Base).
- The launcher hands VideoOut to the emulator's renderer once the game is under way. After that,
  Cemu's own shader-cache screen ("Loading 4,812 cached shaders", drawn with ImGui in Cemu's
  renderer) takes over; Phase 2 draws it with the kit's in-game path, in the same style, so the
  handover is invisible.
- Coming back from a game is a fresh process today (`RestartToLibrary`, `port/app/emulator.h:69`).
  The launcher reopens on that game's hub with the cover already in place, and a toast, "Saved your
  place in the library", so the restart reads as part of the transition. The launcher already keeps
  the PS5's splash screen until its first frame (`launcher.cpp`, `sceSystemServiceHideSplashScreen`
  on frame 1); the GPU launcher must keep doing so, so the console never shows black in between.

### 6.9 The Quick Menu (in a game)

![Quick Menu](ui-redesign/05-quick-menu.jpg)

**Purpose:** change what you need without leaving the game, then get back to it.

- **Opening:** touchpad click + Options, as now. The game's picture eases back (scale 94 %, rounded
  corners) and dims more on the left than on the right, so it stays readable behind the sheet.
- **The sheet's head:** the cover, name, system badge, and *Paused* when the emulator pauses (the
  3DS does today). For the Wii U, which keeps running (README, Known issues), the badge reads
  *Running* until Cemu can be paused; whether `CafeSystem` offers a safe pause is an open question
  (section 12).
- **Quick actions:** four large tiles. 3DS: *Resume*, *Save state*, *Load state*, *Screens*. Wii U:
  *Resume*, *Screens* (swap TV and GamePad), *Amiibo*, *Graphic packs*. Save and Load have different
  shapes (a disk, an arrow into a tray), which Delta's critique asked for.
- **Save states get pictures:** a strip of the five slots with a thumbnail of the moment each was
  saved, when it was saved, and the empty ones. Saving to an empty slot is instant; replacing one or
  loading is **held** (a ring fills over 600 ms) instead of pressed twice.
- **The list:** the categories of today's menus (Screens and border, Graphics, Speed, Graphic packs,
  USB devices, Cheats, Amiibo, Controls), each showing its current value so most visits need no
  opening; Volume is a slider in place; *Quit to the library* at the bottom, held to confirm.
- **One keyboard:** the 3DS's keyboard (the port's) and Cemu's (ImGui, patched for the DualSense)
  become one on-screen keyboard on the kit, used by both games and by the launcher's Search.
- **Toasts** for what happens in a game: "Saved to slot 2", "Amiibo on the reader", "Graphic pack on:
  applies at the next start".

---

## 7. Visual system

![Design system](ui-redesign/00-style-tile.jpg)

The tokens live in one file, `port/ui/tokens.json`, from which a build step writes `tokens.h` for the
app and `tokens.css` for the mockups. Nothing else defines a colour, a size or a duration.

### 7.1 Colour

| Token | Value | Use |
|---|---|---|
| `ink-0` | `#05070d` | The page under everything |
| `ink-1` | `#0a0f1b` | Sheets, the Quick Menu |
| `ink-2` | `#111827` | Opaque surfaces in high contrast mode |
| `glass` / `glass-2` | white at 5.5 % / 8.5 % | Cards, rows, resting buttons |
| `glass-edge` | white at 10 % | One-pixel light edge on glass |
| `text` | `#f5f7fb` | All text; secondary at 70 %, tertiary at 46 % opacity |
| `wiiu` / `wiiu-strong` | `#5aa9ff` / `#2f7fe8` | The Wii U side's accent, its badges and settings |
| `n3ds` / `n3ds-strong` | `#f4b63f` / `#d9961b` | The 3DS side's accent, its badges and settings |
| `good` / `warn` / `bad` | `#3dd6a3` / `#ffb547` / `#ff7272` | Compatibility status, checks |
| `focus` | `#ffffff` | The focus ring |

Hierarchy is by size and opacity, not by colour; the accent appears once per region at most. The side
you are on sets the accent, sky on the Wii U side and sand on the 3DS side: today's colours
(`render-layout.py`'s `accent`), so the change keeps the app recognisable.

### 7.2 Type

Lexend stays: it is designed for reading ease and already the app's voice. It becomes a
signed-distance-field font (sharp at any size, at 4K), with the console's own system fonts as the
fallback for scripts Lexend lacks (CJK, Thai, Arabic), as ProsperoEden loads them.

| Style | Size / weight | Use |
|---|---|---|
| Display | 64–72, Bold | A game's name on Home and its hub |
| Title | 44, SemiBold | Page titles |
| Heading | 32, SemiBold | Sheet titles |
| Body | 26, Regular | Reading text, row labels |
| Label | 24, Medium | Buttons, values |
| Caption | 20, Regular | Secondary facts (the smallest anything readable gets) |
| Overline | 18, SemiBold, capitals, 3.5 px tracking | Section kickers |

Numbers that change (timers, percentages, progress) use tabular figures so they don't jitter. Text
never overflows: it wraps to its line limit, then ends in an ellipsis at a word boundary.

### 7.3 Space, shape and depth

- **Grid:** 8 px; related things 8–16 apart, groups 24–48.
- **Safe area:** 96 px left and right, 60 px top and bottom, for anything readable or focusable.
  Backdrops bleed to the edge.
- **Radii:** chips 12, cards and tiles 20 (covers 14), sheets 28, buttons fully round.
- **Three layers:** a backdrop that moves slowly; content; overlays on frosted glass (the scene
  blurred into a 480 × 270 target, as ps5-homebrew-ui does, then tinted and edged).
- **Shadows** float only what is focused or modal: the focused cover's shadow drops 26 px with a 60 px
  softness and its own colour as a glow.

### 7.4 Covers and the backdrop

- **Covers** come from GameTDB as today. The catalogue stores their natural aspect, so the shelf lays
  them out without decoding them.
- **Ambient colours:** when a cover arrives, a worker downsamples it to 32 × 32 and picks two colours
  (the most frequent saturated one, and a dark companion) with a small median cut. Both are kept in
  `library.json`. Their luminance is clamped so white text over them keeps at least 4.5:1 contrast.
- **The backdrop** is two radial gradients in those colours over `ink-0`, a film grain at 11 %, a
  vignette, and the system motif (bubbles or waves, from `bubbles.cpp` and `wave.cpp`'s parameters,
  as a shader). It cross-fades over 600 ms as the focus moves; Reduce motion stops the motif.
- **Icons and glyphs** are drawn as shapes (the controller glyphs already are, in `menu_canvas.h`), so
  they tint and scale with no atlases.

### 7.5 Focus

The focused element: scales up 4 % (buttons) or 7 % (covers), gains a **double ring** (4 px of `ink-0`,
then 4 px of white, so it reads on light and dark art alike), its shadow deepens, and it glows in its
own colour. The ring is one object that glides between elements on a spring; the eye follows it. A
breathing glow (±6 % opacity over 2.4 s) keeps a resting focus alive.

### 7.6 Accessibility

Three switches, as ProsperoEden has them, plus one:

- **Larger text:** every style up one step (Caption 20 → 24, Body 26 → 30).
- **High contrast:** opaque `ink-2` panels instead of glass, text at full opacity, a thicker ring, no
  grain.
- **Reduce motion:** springs become 150 ms fades; nothing drifts, breathes or parallaxes.
- **Hold to confirm** is always on for destructive actions; the hold time is a setting (0.4 to 1.5 s).

---

## 8. Motion, sound and touch

### 8.1 Motion

| What moves | How | Duration |
|---|---|---|
| The focus ring and lifted covers | Spring, ω 20, critically damped | Settles in 120–200 ms; interruptible, so held directions stay fluid |
| A row or grid scrolling | Spring, ω 16 | Keeps the focus 1.5 tiles from the edge |
| Sheets, dropdowns, the Game menu, the Quick Menu | Spring, ω 13 | About 300 ms in; out on `cubic-in` in 180 ms |
| Changing tab or screen | `quint-out`; parts arrive 40 ms apart, top to bottom | 420 ms |
| Changing text (title, facts) | Old text out 120 ms, new text in 200 ms, 40 ms later | 240 ms |
| Ambient colour | `ease-in-out` | 600 ms |
| Toggles | The thumb on `back-out` (a small overshoot) | 220 ms |
| Launch | The cover to the centre, the rest dims | 420 ms, then the loading screen |

All motion advances by measured frame time, clamped to 50 ms, so a hitch (a system notification) costs
one late frame and no jump. Input is acted on in the frame it arrives; nothing waits for an animation.

### 8.2 Sound

The current five effects (`Move`, `Select`, `Back`, `Denied`, `Launch`) and both music pieces stay and
are extended:

| Cue | When | Notes |
|---|---|---|
| Focus | The focus moved | Panned by the focused element's horizontal position; lower rows a little lower in pitch |
| Select / Back | Opened, chose / closed, cancelled | As now |
| Edge | Pushed past a list's end | Soft, quiet; silent while the direction is held and repeating |
| Toggle on / off | A switch flipped | Rising / falling |
| Slider | A value stepped | Pitch follows the value |
| Sheet in / out | A sheet opened / closed | Very quiet |
| Notify | A toast | |
| Launch | A game starts | Ducks the music, as the fade does now |

Levels follow ps5-homebrew-ui's: ticks about −33 dBFS, interface sounds −27, chimes −23, so navigation
is comfortable at game volume. Each cue has two or three takes, rotated and detuned by up to 3 %, so
held navigation never sounds mechanical. `tools/render-sounds.py` synthesises the new ones as it does
the current five.

### 8.3 Touch and light

- **Rumble** (through `ps5pad::SetVibration`): a light 50 ms pulse for a refusal (Cross on something
  unavailable, the edge of a list once), a stronger 120 ms one for a launch and a completed hold.
  Never on ordinary navigation. Off when Settings > Controllers > Vibration is off.
- **The light bar** (`ps5pad::SetLightBar`): the focused game's system colour in the launcher; in a
  game, as the game sets it.

---

## 9. Architecture and code plan

### 9.1 How to draw it

The design needs 4K text, springs at 60 fps, blur behind sheets, and one toolkit for the launcher and
the in-game menus. Four ways to get there:

| | RmlUi + SDL software (today) | ps5-opengl (ProsperoEden, ps5-homebrew-ui) | **Vulkan on RADV (proposed)** | Dear ImGui everywhere (PCSX2, DuckStation) |
|---|---|---|---|---|
| 4K, 60 fps with motion | No: 1080p, CPU-bound fills | Yes, measured | Yes: the driver Cemu and Azahar already render 4K games with | Yes |
| Blur, shadows, SDF text | No | Yes | Yes (own shaders) | Partly (no blur; text from a bitmap atlas) |
| New dependencies | None | A second GPU stack (Mesa's OpenGL on the PS5's own graphics library) beside RADV | None: RADV, glslang and the VideoOut surface are in the build | None (Cemu bundles it) |
| First frame | Fast | About 5.8 s measured (shader builds; the cache didn't help) | One pipeline through RADV's compiler, with a pipeline cache on `/data`; expected well under that, measured in Phase 0 | Fast |
| Shared with the in-game menus | No | No: the games render with Vulkan | **Yes**: the same batch recorded into Cemu's and Azahar's frames | Yes |
| Known risk | — | Two drivers owning the GPU in one process | The 2026-10-02 attempt drew nothing (9.4) | Looks like a tool unless heavily customised |

**Proposed:** a small Vulkan renderer of our own, in the style ps5-homebrew-ui proved on the
console: one instanced pipeline whose fragment shader evaluates a signed distance for each quad
(rounded rectangle, ring, shadow, glow, image, glyph), a blur pass for glass, and nothing else. It
runs on the RADV build the app links, it shares one kit with the in-game menus (which already draw
with Vulkan into the games' frames) without ImGui's limits on text and depth, and it adds no
dependency. Today's
launcher stays compiled in as **classic** until the new one has shipped (Phase 1 to Phase 4).

### 9.2 Where the code goes

```text
port/ui/                         the kit: launcher and in-game menus both use it
├── tokens.json                  colours, type, spacing, radii, durations (section 7)
├── tokens.h                     generated from tokens.json (tools/render-tokens.py)
├── gfx/
│   ├── device.{h,cpp}           Vulkan on VideoOut for the launcher: instance, device, swapchain,
│   │                            frames in flight, pipeline cache; torn down before a game
│   ├── batch.{h,cpp}            the instanced SDF batch: shapes, images, glyphs, clip runs
│   ├── external.{h,cpp}         the same batch recorded into another renderer's pass (Cemu, Azahar)
│   ├── textures.{h,cpp}         covers decoded on a worker, uploaded between frames, LRU of 96
│   └── shaders/                 ui.vert, ui.frag, blur.frag → SPIR-V at build time (glslang)
├── text/
│   ├── font.{h,cpp}             SDF atlases: Lexend baked at build; FreeType SDF for other scripts
│   └── layout.{h,cpp}           UTF-8, line breaking, ellipsis at word boundaries, tabular figures
├── motion/                      spring.h, ease.h, stagger.h (time-based, clamped dt)
├── input/pad.{h,cpp}            the DualSense as actions: repeat, edges accumulated across samples,
│                                focus lost when the system takes the pad, holds with progress
├── widgets/                     focus ring, button, toggle, slider, stepper, segmented, swatches,
│                                chip, badge, cover, shelf, list, sheet, picker, toast, hints,
│                                keyboard, numeric keypad, qr
├── feedback.{h,cpp}             cues → ps5sound, rumble, light bar
└── backdrop.{h,cpp}             ambient gradients, grain, the bubbles and waves motifs
port/frontend/
├── shell.{h,cpp}                Run(): tabs, screen stack, overlays, toasts, update sheet
├── screens/                     home, library, hub, game_menu, settings (+ settings_schema),
│                                packs, controls, mapping, files, installs, artic, diagnostics,
│                                about, setup_check, launch
├── actions.{h,cpp}              today's non-UI helpers from launcher.cpp, moved unchanged:
│                                folder listing, game counts, logs to USB, shader caches, Artic address
├── sound.{h,cpp}, settings.{h,cpp}   kept and extended (8.2, 9.8)
└── classic/                     today's launcher.cpp, ui_host.cpp, bubbles, wave, until removed
port/app/
├── catalog.{h,cpp}              each side's cached game list (5.5), library.json
├── ambient.{h,cpp}              two colours per cover (7.4)
└── ingame/quick_menu.{h,cpp}    the Quick Menu's model and drawing; ingame.cpp and ingame3ds.cpp
                                 keep their emulator hooks and supply its rows
```

### 9.3 The main interfaces

```cpp
namespace ui
{
	// One frame's input and time. Update() reads it; Draw() never changes state.
	struct Frame
	{
		double now = 0;
		float dt = 0;		 // seconds since the last frame, clamped to 0.05
		Actions actions;	 // confirm, back, up, down, left, right, l1, r1, options... with repeats
	};

	class Screen
	{
	public:
		virtual ~Screen() = default;
		virtual void Enter(Context& context) {}			 // restarts its staggered entrance
		virtual void Update(Context& context, const Frame& frame) = 0;
		virtual void Draw(Canvas& canvas) const = 0;
		virtual std::vector<Hint> Hints() const = 0;		 // the hint row, at most four
	};

	// Records instances into the batch, in the 1920 x 1080 layout space; the batch scales to 4K.
	class Canvas
	{
	public:
		void Rect(const Box& box, float radius, const Paint& paint);
		void Ring(const Box& box, float radius, float width, Colour colour);
		void Shadow(const Box& box, float radius, float offset, float softness, Colour colour);
		void Image(Texture texture, const Box& box, float radius, Colour tint = kWhite);
		void Text(const TextStyle& style, Point at, std::string_view utf8, const TextLimit& limit);
		void PadGlyph(Glyph glyph, const Box& box, Colour colour); // drawn as shapes, not textures
		void Glass(const Box& box, float radius, Colour tint);	   // the scene blurred into it
		void PushClip(const Box& box);
		void PopClip();
	};

	// Critically damped by default; retargeting keeps the velocity, so held directions stay smooth.
	struct Spring
	{
		float value = 0, velocity = 0, target = 0, omega = 20;
		void Update(float dt);
	};
}

namespace ps5catalog
{
	enum class System { WiiU, N3ds };

	struct Entry
	{
		System system;
		uint64_t titleId;
		std::string name;			   // UTF-8, any script
		std::string path, format, gameId, publisher;
		uint16_t version = 0;
		bool hasUpdate = false;
		uint32_t dlcCount = 0;
		float coverAspect = 0;		   // width / height; 0 until the cover is known
		uint32_t ambient[2] = {};	   // two colours from the cover (7.4)
		int64_t lastPlayed = 0;		   // seconds since 1970; 0: never
		uint32_t minutesPlayed = 0;
		float lastSessionFps = 0;	   // from the [perf] / [perf3ds] lines of the last session
		uint8_t packsOn = 0;		   // Wii U: graphic packs on, as Cemu last saw them
		std::string backdrop;		   // the cached boot screen (Wii U) or screenshot (3DS), 7.4
	};

	void Load();					   // /data/ps5cemu/library.json
	void Save();
	std::vector<const Entry*> Games(System side);	   // a side's games, as last saved or scanned
	// After a side's scan: its list replaced, play times, colours and pictures kept by title ID
	void Replace(System side, const std::vector<ps5emu::Game>& scanned);
}
```

### 9.4 Getting Vulkan onto VideoOut this time

Commit `3352086` records the earlier failure: RmlUi's Vulkan backend presented frames whose clear
colour showed, but nothing it drew did, and no call reported an error. Cemu's renderer presents to the
same `VK_KHR_display` surface (`ps5vk::CreateDisplaySurface`) on the same driver, and the in-game menus
draw ImGui geometry through RADV every frame, so the hardware path works; the fault was in that
backend's integration. Phase 0 rebuilds the path in small, checkable steps, logging each to the boot
log:

1. Clear to a colour and present (known to work).
2. One full-screen triangle with a constant colour, no buffers (vertex positions from `gl_VertexIndex`).
3. The same from a vertex buffer in host-visible memory, then from an instance buffer.
4. A textured quad (a cover), then a glyph.

The usual suspects for "clear works, draws don't", each ruled out at the step where it would show: a
zero-sized viewport or scissor, a pipeline built for another render pass or sample count, draws
recorded outside the render pass, memory the GPU cannot see without a flush, a missing layout
transition to `PRESENT_SRC_KHR`, or a swapchain format VideoOut shows differently. Where the kit's
setup differs from Cemu's (its swapchain and render pass creation in `VulkanRenderer`), it follows
Cemu's. RADV's own logging (`radvDebug` in `ps5cemu.json` already feeds `RADV_DEBUG`) is the next
tool.

The launcher owns VideoOut until a game is chosen, then destroys its swapchain, surface, device and
instance before Cemu's or Azahar's renderer starts: exactly what `SDL_Quit` does today. Phase 0's
exit test repeats that handover twenty times on a console.

**Budget** (from ps5-homebrew-ui's measurements on PS5): under 60 draw calls and 6,000 instances per
frame; glass only while a sheet is up; no texture uploads during motion; nothing read back from the
display.

### 9.5 The in-game menus on the kit

- **Wii U:** the menu draws inside Cemu's present pass today, through the port's hooks
  (`PS5Cemu_RenderOverlay`, `PS5Cemu_ImguiUploads`; patches `0008` and `0018`). The kit's
  `gfx/external` records its batch into the same command buffer with a pipeline made for Cemu's render
  pass; textures (the cover, save-state thumbnails) are uploaded in the existing "pictures before the
  pass" hook.
- **3DS:** Azahar's frontend overlay hook (patch `azahar/0004`) hands over the command buffer, render
  pass and size; `ingame3ds.cpp` builds an ImGui Vulkan backend from them today, and the kit's
  external path replaces it.
- **What stays:** each menu's content code (`MenuRows` in `ingame.cpp` and `ingame3ds.cpp`), the
  shortcuts, the touchpad cursor, the GamePad and 3DS screen arrangement, and the hand-off of changes
  to the game's thread. Only the drawing and the input handling of `side_menu.h` and `menu_canvas.h`
  are replaced.

### 9.6 Text

- **Lexend** (regular, medium, semibold and bold) is baked into SDF atlases at build time by a small
  host tool using FreeType, covering Latin, Latin Extended, Greek, Cyrillic and the typographic marks.
  One atlas per weight serves every size from 18 to 96.
- **Everything else** (Japanese, Chinese and Korean titles first) is rasterised on demand with
  FreeType's SDF renderer from the PS5's system fonts, into a second atlas, cached on `/data` between
  sessions. pacbrew's FreeType is already in the dependency set.
- **Layout** is UTF-8 throughout: `Printable` goes. Lines break at spaces (and between CJK characters),
  and an ellipsis only ever replaces whole words.

### 9.7 What happens to each file today

| File (lines) | Becomes |
|---|---|
| `port/frontend/launcher.cpp` (3,694) | Split: the state machine into `shell.cpp` and `screens/`; the RmlUi id writing deleted; the non-UI helpers (folder listing, game counts, `CopyLogsToUsb`, `ClearShaderCaches`, Artic addresses, mapping capture) moved unchanged into `actions.cpp`. A copy stays in `classic/` until removal. |
| `port/frontend/launcher.h` (48) | Kept as the shell's API: `Run(settings, status, prepare)` and `Choice`; `prepare` is called when a side opens, as today |
| `port/frontend/ui_host.{h,cpp}` (802) | `classic/`; replaced by `ui/gfx/device` |
| `port/frontend/bubbles.*`, `wave.*` (288) | Their parameters drive the backdrop's motifs; the CPU versions stay with classic |
| `port/frontend/sound.{h,cpp}` (383) | Kept; new cues, pan, pitch and variations (8.2) |
| `port/frontend/settings.{h,cpp}` (308) | Kept; version 2 of `ps5cemu.json` (9.8) |
| `port/frontend/ui/har.rcss`, `tools/render-layout.py` (590) | Classic only; deleted with it |
| `tools/render-fonts.py`, `tools/render-glyphs.py` | Classic only; replaced by the SDF font tool and drawn glyphs |
| `tools/render-sounds.py` | Extended with the new cues |
| `tools/render-icons.py`, `render-background.py`, `render-banner.py`, `render-borders.py` | Unchanged (home screen icon, banner, 3DS borders) |
| `tools/launcher-preview/` (1,054 + script) | Rebuilt on the kit with desktop Vulkan (Mesa's lavapipe in CI); `screens.txt`'s script format kept |
| `port/app/side_menu.h`, `menu_canvas.h` (455) | Replaced by the kit's widgets and `app/ingame/quick_menu.cpp` |
| `port/app/ingame.cpp` (690), `ingame3ds.cpp` (1,012) | Keep their hooks and content; drawing and keyboard move to the kit (9.5) |
| `port/main_ps5.cpp` (241) | The side choice moves into the shell's switch; `prepare` is unchanged (Cemu's core or Azahar's scan, per side) |
| `port/app/boxart.cpp`, `covers.cpp`, `gameinfo.cpp`, `compatibility.cpp`, `port/azahar/library.cpp` | Unchanged; box art arrivals also trigger the ambient colours |
| `port/CMakeLists.txt` | New sources; a step compiling the UI's shaders to SPIR-V with the pinned glslang |
| `patches/cemu` | One more patch: Cemu's shader-cache loading screen drawn by the kit (6.8) |

Size, roughly: the kit 5,000 to 6,000 lines; the shell and screens about 5,000 (replacing about 5,000
of launcher, host, layout script and stylesheet); the Quick Menu about 1,500 (replacing about 1,200).

### 9.8 Settings and data

- `ps5cemu.json` gains `"version": 2`. Every current key is read as before; unknown keys are kept on
  save, so going back to an older release loses nothing.
- **Kept, with a new meaning:** `side` is now the side last used, restored at every start (today it is
  kept only across a game's restart). **Removed:** the two `gameCount`s (the catalogue has them).
- **Moved:** each side's `recent` list becomes the catalogue's last-played times for that side; the
  old lists, which have no times, keep their order on migration.
- **Added:** `games.<title ID>` (game settings, 6.5), `ui.textScale`, `ui.highContrast`,
  `ui.reduceMotion`, `ui.holdMs`, `ui.startOn` (`last` or `ask`), `ui.libraryFilter` and
  `ui.librarySort` (per side), and `ui.classic` (the fallback switch).
- `library.json` holds the catalogue (5.5); deleting it only costs one full scan of each side.

### 9.9 Testing

- **Unit tests** on the host: springs settle and retarget, spatial navigation's goal column, catalogue
  merges (a game in both scans, a game gone), settings migration both ways, line breaking with CJK
  and ellipses.
- **Snapshot tests:** the preview harness plays `screens.txt` and renders every state on lavapipe;
  CI compares each PNG with a committed golden. The mockups in `docs/ui-redesign/` are the starting
  goldens' targets.
- **On the console:** a tour mode plays the same script and writes `[ui] frames=… avg=… p99=… max=…
  draws=… instances=…` to the boot log every 600 frames, as ps5-homebrew-ui does; Phase 0 to 3's exit
  criteria are read from it.
- **The craft checklist** (ps5-homebrew-ui's, adapted in section 4) for every screen before it ships.

---

## 10. Delivery plan

Each phase ends in a release; the classic launcher is the fallback throughout (`ui.classic`, or L1
held while the app starts, as RetroArch PS5 does for its pre-screen).

| Phase | What | Exit criteria | Size |
|---|---|---|---|
| **0. Foundations** | `ui/gfx` on VideoOut (9.4), the SDF batch, text, springs, input, feedback; the preview harness on lavapipe; a gallery screen showing every widget | Gallery at 4K, 60 fps on a PS5 and a PS5 Pro (tour log: p99 under 17 ms, max under 21 ms); first frame within 1.5 s of the launcher opening; twenty clean hand-overs of VideoOut to each emulator | 2–3 weeks |
| **1. The shell, at parity** | The catalogue (5.5) and the side switch (5.3); Home, Library, Game hub, Game menu, Settings with every current setting and page, the launch screen, the update sheet | Every launcher row of Appendix A ticked; a snapshot for every screen; launch times no slower than 3.0.0's | 4–5 weeks |
| **2. In a game** | The Quick Menu on the kit for both systems; one keyboard; hold to confirm; save-state thumbnails; toasts; Cemu's shader-cache screen | Every in-game row of Appendix A ticked; the menu at 60 fps over a 4K game without lowering the game's own frame rate measurably | 2–3 weeks |
| **3. The rest of the design** | Setup check; game settings; search, sort and filters; accessibility switches; frame-rate stats; light bar and rumble; touchpad zones and reserved buttons in mapping | The principles' tests (section 4) pass on every screen | 2–3 weeks |
| **4. Reach** | The PS5's system language and a string table; an opt-in companion page by QR for long text and game settings (LAN only, off by default, as PS5SX2 and RetroArch PS5 do it); classic removed after a release with no fallback reports | — | Open |

Altogether 12 to 16 weeks for Phases 0 to 3.

---

## 11. Risks

| Risk | Likelihood | Effect | What to do |
|---|---|---|---|
| Vulkan on VideoOut fails again | Medium | Phase 0 slips | The step-by-step bring-up (9.4); Cemu's working setup as the reference; the classic launcher ships meanwhile |
| The first frame is slow (device and pipeline creation) | Medium | A slower start after every game | One pipeline, a pipeline cache on `/data`, the splash screen held until the first frame; measure in Phase 0 against the 1.5 s budget |
| The launcher's GPU memory isn't all freed before a game | Low | Less memory for Cemu | Everything is destroyed with the device; the boot log's `[memory]` line before and after compares with 3.0.0's |
| The catalogue is out of date | Medium | A removed game shown, or a new one missing, until the side's scan finishes (seconds) | The scan's list replaces the side's when it finishes; a game whose file is gone is dimmed and says so; *Look for games now* in Settings |
| System fonts differ between firmwares | Low | A script falls back to boxes | Probe the known paths at start; log what was found; Settings > About says which fonts are in use |
| The in-game kit costs the game frames | Low | Menus stutter the game | The batch is a few dozen draws; measure in Phase 2; the ImGui path remains for one release |
| Scope grows | High | The release slips | The phases are each shippable; Phase 4 is optional by design |
| Artwork rights | — | — | No Nintendo logos or box art are bundled; covers keep coming from GameTDB at run time; the mockups use drawn stand-ins |

---

## 12. Open questions

1. **Pausing the Wii U.** Does Cemu's `CafeSystem` offer a pause that is safe to use from the menu
   (the README's known issue)? If so, the Quick Menu can pause both systems alike.
2. **Leaving a game without a restart.** `RestartToLibrary` exists because Cemu cannot end a game
   reliably in one process. The design hides the restart, but if Azahar can end a game cleanly, 3DS
   games could return without one.
3. **Recent games:** twelve per side (proposed), or keep today's four?
4. **Starting Cemu off the launcher's thread:** can `ps5emu::InitializeCore` run on a worker, so the
   Wii U side stays live during its first few seconds (5.5)?
5. **Music per side:** should switching sides also switch the music (the shop theme on one side, the
   setup theme on the other), or keep one choice for both?
6. **Profiles** (ProsperoEden has eight): wanted for shared consoles, or out of scope?
7. **The companion page:** is a LAN web service acceptable to the project, given HEN setups vary?

---

## Appendix A: feature parity

Every feature of today's UI, where it is now, and where it goes. ✓ means unchanged in behaviour;
**+** means improved.

### Start and shell

| Today | Where | In the new UI |
|---|---|---|
| Start screen: choose Wii U or 3DS, game counts, version | `StartScreen` | The side switch in the bar and on the touchpad (5.3); the chooser at first start and with *Start on: Ask each time*, with the counts; version in Settings > About and the Setup check **+** |
| "Starting Cemu" while its core starts | `StartScreen::ShowStarting` | On the Wii U side's Home, drawn from the catalogue, the first time per session (5.5) **+** |
| Tabs Home, Library, Settings on L1 / R1 and on the bar | `TabsKey` | ✓ |
| Clock | `menu-clock` | ✓, with the connected controllers and network status **+** |
| Hints, at most four | `SetHints` | ✓ |
| Back to the start screen with Circle on Home | `m_leaving` | The switch (touchpad, or the bar); the chooser when *Ask each time* is on **+** |
| Opening on the side last played | `settings.side` | ✓, at every start, and on the hub of the game just played **+** |
| A notice on Home (no `/data`, Cemu failed, a launch error) | `Notice()` | A card on Home, and the Setup check **+** |
| Background: bubbles (Wii U), waves (3DS), both (start) | `ui_host.cpp` `Background` | ✓ as each side's motif, under the focused game's own picture (7.4) **+** |
| Music (shop, setup, off), its volume, menu sounds | `ps5sound`, Settings > Audio | ✓ in Settings > Sound; more cues (8.2) **+** |
| Controllers joining and leaving (rescan every two seconds) | `Run`'s `frame` | ✓ |
| The update prompt over any screen: available, downloading, checking, installing, restart, failed | `UpdatePrompt` | The update sheet, with release notes **+** |

### Home

| Today | Where | In the new UI |
|---|---|---|
| Continue: the last game, publisher and year, status, Play and Details | `UpdateHome` | The row's focused cover and the hub preview **+** |
| Library or Settings buttons when there is no last game | `HeroActions` | The empty-library card; the Setup check |
| Artic Base button (3DS) | `HeroAction::Artic` | The 3DS side's first Library tile, "Play from your 3DS", and Settings > Nintendo 3DS > Artic Base |
| Recent games shelf and All games | `ShowIconTile` | The row (the side's games) and *All games* ✓ |
| Square: a game's page | `HomeKey` | Down, or *Game hub*, or the Game menu |

### Library and game pages

| Today | Where | In the new UI |
|---|---|---|
| Grid of box art (or icons), scroll bar, count | `UpdateLibrary` | The side's shelf, the index, the filter counts **+** |
| "Looking for games…", empty library text | `library-empty` | ✓ in the filter row and the empty card |
| L2 / R2 a page at a time | `LibraryKey` | Previous / next letter or year **+** |
| Cross plays, Square details, Triangle graphic packs | `LibraryKey` | Cross plays; Options: hub, packs, game settings **+** |
| Details: cover, kicker, title, chips, facts, description scroll, Play, Graphic packs (n on), L1 / R1 other games | `UpdateDetails` | The Game hub ✓, plus game settings and last-session stats **+** |
| Box art from GameTDB, icon fallback, arrivals refresh | `ps5boxart`, `Poll` | ✓, plus ambient colours **+** |
| Compatibility status from the list | `ps5compat` | ✓ on covers, hub, Home |

### Graphic packs (Wii U)

| Today | Where | In the new UI |
|---|---|---|
| Packs in Cemu's folder tree, on / off, position | `UpdatePacks` | ✓ |
| Presets per category, dropdown, "choosing one turns it on" | `ChoosePreset`, `OpenPicker` | ✓ |
| Description panel | `pack-detail-*` | ✓, plus "replaces game files · applies at next start" **+** |
| Community packs update from GitHub | Settings > Online | ✓ Settings > Online and updates |

### Settings

| Today (category > row) | In the new UI |
|---|---|
| Video (Wii U) > Upscaling to 4K, 120 Hz output, Performance overlay, Async shader compile | Wii U > Graphics (upscaling with a sample preview, async shaders); General > Display: 120 Hz output (badged Wii U while only Cemu uses it) and the performance overlay (one switch for both systems, today one each) |
| Video (3DS) > Internal resolution, Screen layout, Texture filter, Custom textures | 3DS > Graphics (resolution stepper, texture filter, custom textures); 3DS > Screens and borders (layout pictograms) |
| Audio > Game volume, Launcher music, Music volume, Menu sounds | General > Sound ✓ (one game volume for both, today one per system, overridable per game) |
| Audio (Wii U) > GamePad speaker | General > Sound, as a row badged Wii U ✓ |
| Vibration on / off (`rumble` in `ps5cemu.json`, switched on when a player's vibration is raised) | General > Controllers > Vibration ✓, now a visible switch **+** |
| Controls (Wii U) > Player 1–4 > Emulated controller, Motion, Vibration, Left / right deadzone, Buttons, Reset | Wii U > Controllers ✓; mapping with touchpad zones and reserved-button marks **+** |
| Controls (3DS) > Motion, Stick deadzone, Buttons, Reset | 3DS > Controls ✓ |
| Button mapping: capture with countdown, touchpad cancels, Square clears | ✓, the DualSense drawn with the input lit, swap on conflict **+** |
| USB devices (Wii U) > Skylanders, Infinity, Dimensions; figure folders text | Wii U > USB devices ✓ |
| Borders (3DS) > Border | 3DS > Screens and borders, with swatches and preview **+** |
| System (3DS) > Region, Language, Home Menu | 3DS > System and Home Menu ✓ |
| Game files > Game folder (folder browser with drives, counts, keys.txt, in use) | General > Games and folders, both systems' folders ✓; *Look for games now* **+** |
| Installs > Install from a folder (Wii U) with inspect, progress, cancel | Wii U > Install updates and DLC ✓ |
| Installs > Install a CIA file (3DS) with inspect, progress, cancel | 3DS > Install CIA files ✓ |
| Online > Box art from GameTDB, Community graphic packs, PS5CEMU-HAR updates | General > Online and updates ✓ |
| Diagnostics > Copy logs to USB, Clear shader caches (the side's), version, firmware, HEN / JIT, logs path, session | Help > Diagnostics ✓: Clear Wii U shader caches and Clear 3DS shader caches as two rows, held to confirm; cards with status **+**; Help > Setup check **+** |
| Settings only in `ps5cemu.json` (`radvDebug`, `pinCpuThreads`) | ✓, still only there |
| About > credits, paths, version | Help > About ✓ |
| Triangle: a setting's longer help | ✓ |
| Rows dimmed when unavailable (no DualSense, no Cemu, no Artic setup) | ✓, and say why when focused **+** |
| Cross twice for Reset, Clear shader caches, Artic Setup | Hold to confirm **+** |

### Artic Base (3DS)

| Today | In the new UI |
|---|---|
| The 3DS's address: Left / Right a number, Up / Down ±1, L1 / R1 ±10, remembered | ✓, plus a numeric keypad **+** |
| Connect and play; Artic Setup from an Old or a New 3DS | ✓ (setup held to confirm) |

### Launching

| Today | In the new UI |
|---|---|
| Loading screen: icon, name, "Starting", "Starting the 3DS", "Connecting to the 3DS" | The launch transition with the same captions (6.8) **+** |
| Background work stopped before a game (box art, scans, pack updates) | ✓ (`StopBackgroundWork`) |
| The launch sound, music fading out | ✓ |
| A failed launch shown on return | ✓ as a Home card, with *Try without packs / cheats* **+** |

### In a game (Wii U)

| Today | In the new UI |
|---|---|
| Touchpad + Options: the menu; + L1 swap TV and GamePad; + R1 corner screen | ✓ |
| Touchpad as the GamePad's touch screen; Wii Remote pointer | ✓ (not the UI's) |
| Menu head: box art, name, publisher, year | ✓, plus Running / Paused and play time **+** |
| Back to the game | *Resume* ✓ |
| Screens: main screen, the other in a corner, picture shape | ✓ (quick action *Screens*, and the category) |
| Graphics: upscaling, accurate barriers, async shaders, overlay | ✓ |
| Graphic packs while running, presets, "next start" marks | ✓ |
| USB devices: figures on slots, Empty | ✓ |
| Volume | ✓ (a slider in place) |
| Amiibo: choose, scan, message | ✓ (quick action *Amiibo*) |
| Controls: player, emulated controller, motion, vibration, deadzones, A and B | ✓ |
| Back to the library, Cross twice | *Quit to the library*, held **+** |
| Cemu's on-screen keyboard (with the DualSense) | The kit's keyboard **+** |

### In a game (3DS)

| Today | In the new UI |
|---|---|
| Touchpad + Options: the menu; + L1 swap screens; + R1 next layout; the game pauses | ✓ |
| Screens: layout, main screen, border | ✓ |
| Graphics: internal resolution, texture filter, performance overlay | ✓ |
| Speed: CPU clock, speed limit (this game only) | ✓ |
| Volume | ✓ (slider) |
| Save states: slot 1–5 with time, save, load (Cross twice) | Quick actions and the slot strip with thumbnails, held to load or replace **+** |
| Cheats: on / off per cheat, "no cheats" | ✓ |
| Amiibo: choose, take away | ✓ |
| Controls: motion, deadzone, A and B | ✓ |
| Back to the library, Cross twice | *Quit to the library*, held **+** |
| The port's 3DS keyboard (labels from the game, validation) | The kit's keyboard, same behaviour ✓ |

---

## Appendix B: voice and copy

The current copy is one of the app's strengths; the new screens keep its rules.

- **Say what it does, in the player's words.** "How the two screens share the TV", not "Layout mode".
- **One line under a setting, the rest behind Triangle.** The line fits at 26 px.
- **Name the consequence.** "Off can raise the frame rate, but some games then flicker."
- **Numbers over adjectives.** "2400 × 1440", "41 h played", "Downloading: 35 %".
- **Errors say what happened and what to do.** "No games in /data/ps5cemu/games yet. Choose a folder."
- **Sentence case, no exclamation marks, no "please"** except where something failed and must be
  retried.
- **British spelling**, as the code uses (colour, centre), except in names (the PS5's Control Center).
- **Systems by their names:** "Wii U" and "Nintendo 3DS" (or "3DS" where space is short), never the
  emulators' names in the player's way; Cemu and Azahar are credited in About and named where their
  behaviour matters ("Cemu has two GamePads at most").

---

## Appendix C: sources

Read in full (GitHub, and this repository):

- BlackBearReloaded, [ps5-homebrew-ui](https://github.com/blackbearreloaded/ps5-homebrew-ui):
  [README](https://github.com/blackbearreloaded/ps5-homebrew-ui/blob/main/README.md),
  [docs/CRAFT.md](https://github.com/blackbearreloaded/ps5-homebrew-ui/blob/main/docs/CRAFT.md),
  [docs/PERFORMANCE.md](https://github.com/blackbearreloaded/ps5-homebrew-ui/blob/main/docs/PERFORMANCE.md)
- BlackBearReloaded, [ProsperoEden README](https://github.com/blackbearreloaded/ProsperoEden)
- Swordpdf, [PS5SX2 README](https://github.com/Swordpdf/PS5SX2)
- Mihawk, [PS5_RetroArch README](https://github.com/mihawk-99/PS5_RetroArch)
- Rufidj, [Nativehbl](https://github.com/Rufidj/Nativehbl)
- retropassdev, [RetroPass](https://github.com/retropassdev/RetroPass)
- PCSX2, [`pcsx2/ImGui/FullscreenUI.cpp` and `ImGuiFullscreen.h`](https://github.com/PCSX2/pcsx2/tree/master/pcsx2/ImGui)
- This repository's [issues](https://github.com/premohq/PS5CEMU-HAR/issues) #3 to #24, its code, and
  its launcher preview tool (the "today" screenshots)

Through search results and summaries (the pages themselves were not reachable from the research
environment, so quotes are as the search returned them):

- PCSX2, [PCSX2 2.0 release notes](https://pcsx2.net/blog/2024/pcsx2-2-release); Steam Deck HQ,
  [PCSX2 2.0 coverage](https://steamdeckhq.com/news/pcsx2-gets-massive-stable-2-0-update-with-vulkan-support-automatic-fixes-per-game-settings-and-more/)
- DuckStation, [README](https://github.com/stenzek/duckstation) (fullscreen UI on Dear ImGui)
- libretro forums, [Retroarch interface confusing?](https://forums.libretro.com/t/retroarch-interface-confusing/18148)
  and [RetroArch hard to learn for newbies?](https://forums.libretro.com/t/retroarch-hard-to-learn-for-newbies/22941);
  GamingOnLinux, [RetroArch 1.8.5 replaces XMB with Ozone](https://gamingonlinux.com/2020/03/retroarch-185-is-out-replacing-the-xmb-ui-with-ozone-plus-lots-of-bug-fixing)
- Valve, [Updated Big Picture is now available for testing](https://store.steampowered.com/news/app/593110/view/3394051164709183116);
  Steam Client Beta, [Deck UI / new Big Picture feedback](https://steamcommunity.com/groups/SteamClientBeta/discussions/3/6118730946104413670/)
- Pratt IXD, [Design critique: Delta](https://ixd.prattsi.org/2023/02/design-critique-delta-ios-app/);
  iDownloadBlog, [Delta first impressions](https://www.idownloadblog.com/2016/12/21/delta-ios-emulator-beta-first-impressions/)
- Tech Insider, [LaunchBox vs Playnite vs ES-DE (2026)](https://tech-insider.org/launchbox-vs-playnite-vs-es-de-2026/)
- TweakTown, [PS5's UI is a clean and functionally layered evolution](https://www.tweaktown.com/news/75709/ps5s-ui-is-clean-and-functionally-layered-evolution-over-the-ps4/index.html);
  Screen Rant, [PS5 Activities criticised as distracting](https://screenrant.com/ps5-ui-activities-criticized-distracting-time-saving/);
  TechRadar, [PS5 interface "a 100% overhaul"](https://www.techradar.com/news/ps5-interface-to-be-revealed-soon-and-its-a-100-overhaul-of-the-ps4-ui);
  Seeking Tech, [haptic feedback when navigating PS5 menus](https://seekingtech.com/how-to-enable-haptic-feedback-when-navigating-the-ps5-system-menus/)
- Nintendo Everything, [Switch home menu design resources under 200 KB (CEDEC 2018)](https://nintendoeverything.com/nintendo-talks-about-switchs-os-home-menu-design-resources-have-less-than-200kb/);
  Nintendo Wire, [Nintendo on the Switch OS's design](https://nintendowire.com/news/2018/08/22/nintendo-talks-about-the-design-of-the-switchs-os-at-cedec-2018/)
- Microsoft, [Designing for Xbox and TV](https://learn.microsoft.com/en-us/windows/apps/design/devices/designing-for-tv);
  Android Developers, [TV layouts](https://developer.android.com/design/ui/tv/guides/styles/layouts);
  Smashing Magazine, [Designing for TV, part 1](https://www.smashingmagazine.com/2025/08/designing-tv-evergreen-pattern-shapes-tv-experiences/)
  and [part 2](https://www.smashingmagazine.com/2025/09/designing-tv-principles-patterns-practical-guidance/)
- GamingOnLinux, [Cemu 2.0 on Linux](https://gamingonlinux.com/2022/08/wii-u-emulator-cemu-20-out-goes-open-source-and-gets-linux-support)
  (comments on Cemu's cramped UI on the Steam Deck); Android Authority,
  [Azahar's dual-screen update](https://www.androidauthority.com/azahar-3ds-dual-screen-update-3605263/)
