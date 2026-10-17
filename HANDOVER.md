# OpenRod 2 — session handover (2026-10-16)

## What this is
OpenRod 2 is a from-scratch rebuild of **Street Rod 2** (1990, DOS) in plain C + SDL3. It reads the original game files at runtime and is a faithful port of SR.EXE, reverse-engineered function by function.
- Repo: `C:\Users\inact\SR2`, public at github.com/rnold182/openrod2, branch `main`.
- Last commit: see `git log`. Everything is pushed.
- Releases: v0.1 and v0.2 ("Mulholland Drive"), both pre-alpha. The zips are in `dist\`, which is gitignored.

## Rules the user set (keep them)
- **Faithful port.** Comment every function with its SR.EXE address, e.g. `/* 1001:9e3c */`, and record every RE finding in `NOTES.md`.
- **Modern touches are options.** Anything that differs from the original goes behind an `opt_*` variable (`engine/src/options.c`). They're for a future options menu and are listed in NOTES "Rebuild deviations / modern options".
- **Never commit the original files** (SR.EXE, LIB1-3, *.MUS, MSR.SND, SR2_*, HOTROD*.SAV).
- **AdLib sound must stay exact.** It's the ported driver plus Nuked-OPL3.
- **Saves stay in the original `HOTROD<n>.SAV` format**, compatible both ways.
- **README build steps are Windows / PowerShell only.**
- **After every finished step:** commit and push. Before handing back, build the user's folder too (`cmake --build build`). The user plays `build\openrod2.exe`, so check first that it isn't running (`tasklist`).
- **Releases only at big milestones** (a new race, a new feature). Ask the user before publishing.
- **Keep the 57000 rpm reading.** After the finish line the rev counter shows ~57000 rpm in neutral while braking. The user loves this original "feature": never fix it.
- Explain results in plain words. The user isn't a programmer.

## Build / test
- Toolchain: Ninja and GCC (WinLibs, on PATH).
  - `cmake --build build_dev` is for tests.
  - `cmake --build build` builds the user's exe (Release).
  - New `.c` files must be added to `engine/CMakeLists.txt`.
- Python: use `"/c/Program Files/Python313/python"`. It has Pillow, capstone and numpy.
- Test data: a copy of the original files lives in the old session's scratchpad (`...\scratchpad\data`). In a new session, copy the original files from the repo root into your own scratchpad and pass that folder as the first argument.
- Scripted run, from `build_dev`:
  ```
  ./openrod2.exe "<data dir>" --new-game Tester --script "300:cheat:car 3;350:cheat:agree;400:cheat:autodrive;700:click:20,90;900:cheat:rival 3;3500:click:140,140;3800:click:90,61;4100:click:90,45;4700:click:100,100;20000:shot:../capture/a.bmp;20500:quit"
  ```
  - That script clicks: hit the street (20,90); the challenge (140,140); dialog 19 race type (drag 90,45 / Mulholland 90,61); the first bet button (90,45); then dismisses the message (100,100).
  - Script step kinds: `key:NAME`, `hold:NAME`, `release:NAME`, `text:abc`, `click:x,y`, `move:x,y`, `cheat:cmd`, `shot:file.bmp`, `quit`. Times are in ms; a script can have up to 160 steps.
  - Screens are 320x200 BMPs. Upscale them with Pillow and look at the PNG.
- Environment flag `SR2_DEBUG=1` logs:
  - hotspots;
  - one race line about every 2 s (pos, speed, rpm, x, heading, opponent);
  - "race over: result ...";
  - the gantry segment ("road: gantry over segment N").
- Cheats (F12 or `` ` ``): `money`, `car`, `part`, `day`, `anim`, `rival N` (at Burger's), `agree` (rivals take every bet), `autodrive` (drives, steers along the road, can't crash), `autodrive pedals` (gas and shifting only, you steer), `police` (next Mulholland race), `kingend`, `save N`, `help`.
- RE tools:
  - `out/sr_decompiled.c` is the Ghidra output. Ghidra segment = image segment + 0x1000; DS = 0x3e08. So `FUN_1dcb_x` is image 0dcb:x and `FUN_2a21_x` is 1a21:x.
  - Extract one function: `awk 'f && /^\/\/ ==== FUN_/ {exit} /==== FUN_1001_9e3c @/ {f=1} f' out/sr_decompiled.c`.
  - Disassemble (needed when Ghidra is wrong, e.g. 1dcb:24d8 and the timer ISR 0001:2620): `tools/sr_dis.py 0dcb:24d8 300`.
  - `tools/dialogs.py` lists the dialogs. In-game messages come from the table at DS:11ea.
  - Exported pictures: `out/images/LIB1-3/<id>_WxH_flags.png`. Picture ids 2000+ are in LIB3.
- Deep-dive notes are in `NOTES.md`. The race sections are "The drag race", "The roads" and "Steering, collisions and Mulholland Drive".

## Where things stand
**Done:**
- Intro, license and new game.
- The whole garage: engine bay, transmission, under the car, tires, paint and body, the newspaper, buying and selling with haggling, the calendar, car info, workshop animations, cursors.
- Save and load.
- Burger's Bungalow: rivals arrive, The King, look for an opponent, pop the hood, challenge, bets.
- **Drag race and Mulholland Drive.** Code: `engine/src/scenes/race.c` and `engine/src/road.c`.
  - Road tables built from the save's seed: DS:a9f8 is `header_raw + 0x20`, the layout is `word_7dfe`.
  - The original renderer: backdrop, road strips, SR2_SHP shapes, gantries, hillsides, bridges, guard rails, rocks, things on the road.
  - Steering: keys and mouse, at 6 Hz.
  - Roadside collisions (1dcb:148a), including falling off.
  - The opponent's lane AI (d4cc/cf9a), slowing for bends, rear-end crash, side bumps and shoves.
  - Crash screen (af0a) with the results by speed: dialogs 24/25 Fix it / Junk it, totaled, tires.
  - Fuel use, and the steering wheel and knob on the dashboard.
- Timing (fixed last): the timer runs at 72.8 Hz.
  - Race frames run at 9.1 Hz; 11fc ends with 2a21:1924(8).
  - The engine steps on its own 6.07 Hz clock (every 12th tick).
  - Race times count those steps.

- **The Aqueduct** (add6 8): its own renderer in `road.c` (`aq_*`: 33ac table, 18ec/a094 projection
  and tilt, 8b16 sky, 905c painter, 8b5c boxes, 8288 turned shapes, 7f30 clipping), collisions
  (1cd2, a2d8), rolling over (a1c0), opponent steering d2a8 and his lifted / turned picture.
  The race view is drawn once per game frame there (it moves the car) and kept for the display.

- **The rear-view mirror** in all three races (road.c `mirror_*`, `aq_mirror`): road, scenery,
  hills, the starter shrinking behind you, the opponent's front.

- **The police on Mulholland**: they take the opponent's place, siren, pull you over, the ticket and
  the fine (cheat `police` forces them on the next Mulholland race).

- **The King beaten** (scenes/king.c: farewell text 1083 and the SR2_END animation; cheat `kingend`).
- The opponent's shrunk pictures now use the game's own masks (SR2_DATA block at DS:8e3c), ahead and
  in the mirror.

- Garage and street animations: cars' motor sound at Burger's and in the garage, your car driving
  into the garage, switching cars (old out, new in), sliding under the car; list arrows fixed.

**Recent fixes:**
- At Burger's, the parked car's bottom 8 rows are left undrawn (DS:1bde[2]), as in the original.
- The cursor shows over the crash dialog.
- Every race now draws the window's left pillar (2003, 1dcb:56e8), and steering across the middle while
  the starter is still there disqualifies you (msg 0x78), as in the original.

## Next steps (in this order)
1. **The drive between places** (the user asked for it). 1001:8c68 (garage → Burger's 1bc8 = 2, gas
   station 3, Grudge Night 4): fuel -2, add6 = 0 (town), b7ae = 0 (not a race), bc88 / 0736 (start
   segment by 1bc8: 1 → 10, 2 / 4 → 0x41, 3 → 0x73), 8d9e → 2a21:77f4 with the race screen and
   dashboard. Skipped when you click during 8d9e's 3d80(0xf) / 3d80(0x19) waits (DS:3bf6).
   - 8ef4's drive branch: 20a0 motor, status DS:5006, gear 1; per frame rpm a10e += DS:501a[gear]
     (max 0x125c), speed = rpm / 5024[gear], next gear at 0x1195 up to a11c; ends (2176) when L or R
     of segment pos + 1 has bit 1 (build_town marks R(0x2c), L(0x6c), L(0x9c)) or on a click.
   - 1774 when b7ae == 0: a562 = 0x30 (0x20 on slow PCs) a frame, the autopilot (12ca), 1758(1) once
     L(pos + 7) & 1, 1758(-1) once R(pos + 6) & 1 (turning in, DS:7ba0 / 7b9e).
   - Not ported: the town view - 4322's branch for road flag 1, 52e2 picture objects (the buildings,
     side flags 4 / 8 / 0x10 / 0x80 / 0x100 / 0x8000, gantry pictures 0x2000), 4e70 (sidewalks /
     crossings, see the mirror's 5aee too), 3e66 for town, 1758, 8c2c, and starting the drive from
     the garage (rebuild garage.c G_STREET etc.) and arriving (street / garage modes).
2. **The gas station.** Garage "Wanna tank?" hotspot id 15, mode 3, 3925:2104, dialog 22.
3. **Grudge Night.** Id 17, Wednesdays only (63e0() % 7 == 2), $30, fuel > 23, mode 4, 3925:1970 (race types 2/4 use tables 60be/61a6/6572).
4. **Street details.** The carhop (2a21:945a, picture 0x458), rival taunts (1001:9432).
5. **End of summer and the other endings.** Day > 90 → DS:1bc6, 1001:36ac / 348a. Also jail when you can't pay a police fine (b584 returns 4 → 1001:a9fc, 1bc6 |= 4; see NOTES "The police on Mulholland"), and after beating The King (scenes/king.c ends with 1bc6 |= 2, currently back to the garage).
6. **Options menu** for every `opt_*` setting.

## Starter prompt for the new session
> We're continuing OpenRod 2, my C/SDL3 rebuild of Street Rod 2 in C:\Users\inact\SR2. Read HANDOVER.md in the repo first (rules, build/test commands, where we are, next steps), then NOTES.md's race sections ("The drag race", "The roads", "Steering, collisions and Mulholland Drive") and engine/src/scenes/race.c + engine/src/road.c. Keep exactly the same approach: faithful ports from out/sr_decompiled.c and the disassembler, original addresses in comments, findings into NOTES.md, modern touches only as opt_* options, commit + push after each step, build both build_dev and build (check openrod2.exe isn't running), test with scripted runs and screenshots. Next task: step 1 in HANDOVER.md.
