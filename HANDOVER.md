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
- Cheats (F12 or `` ` ``): `money`, `car`, `part`, `day`, `anim`, `rival N` (at Burger's), `agree` (rivals take every bet), `autodrive` (drives, steers along the road, can't crash), `autodrive pedals` (gas and shifting only, you steer), `help`.
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

**Recent fixes:**
- At Burger's, the parked car's bottom 8 rows are left undrawn (DS:1bde[2]), as in the original.
- The cursor shows over the crash dialog.
- Every race now draws the window's left pillar (2003, 1dcb:56e8), and steering across the middle while
  the starter is still there disqualifies you (msg 0x78), as in the original.

## Next steps (in this order)
1. **Police on Mulholland.**
   - DS:ab2c: a 1 in 4 chance; the police wait at segment 9aca.
   - The d908 state machine, d85c, the siren (bbb8, sound slot 11).
   - b584's a1bc & 0x40 path: a speeding fine "You have been fined $%2d" (DS:51e7), the 2009 ticket picture, message 0x52.
2. **The rear-view mirror.** Projection 1dcb:0ce4 (06ba/070e), draw 379e, opponent kinds 9+, clip x 0xf0..0x13f, rows 0x16..0x2a. In the Aqueduct: 1b02 / 9ce8 / 57a6 / a92a and the reversed table 628e. After the start the original shows the starter lady shrinking behind you, in the mirror and through the side window (a92a, picture table at DS:9a2a; seen in DOSBox screenshots).
3. **Race aftermath.** The King losing (1001:a51c), and the opponent's small rear views: the original uses pre-scaled copies (scale bytes from SR2_DATA via 37ce's a0ea calls, DS:aaa4 records), the rebuild still scales them by 12/d.
4. **The gas station.** Garage "Wanna tank?" hotspot id 15, mode 3, 3925:2104, dialog 22.
5. **Grudge Night.** Id 17, Wednesdays only (63e0() % 7 == 2), $30, fuel > 23, mode 4, 3925:1970 (race types 2/4 use tables 60be/61a6/6572).
6. **Street details.** Cars driving in and out with their motor sound (2a21:89fa, 1001:20a0/2176), the carhop (2a21:945a, picture 0x458), rival taunts (1001:9432).
7. **End of summer.** Day > 90 → DS:1bc6, 1001:36ac / 348a.
8. **Options menu** for every `opt_*` setting.

## Starter prompt for the new session
> We're continuing OpenRod 2, my C/SDL3 rebuild of Street Rod 2 in C:\Users\inact\SR2. Read HANDOVER.md in the repo first (rules, build/test commands, where we are, next steps), then NOTES.md's race sections ("The drag race", "The roads", "Steering, collisions and Mulholland Drive") and engine/src/scenes/race.c + engine/src/road.c. Keep exactly the same approach: faithful ports from out/sr_decompiled.c and the disassembler, original addresses in comments, findings into NOTES.md, modern touches only as opt_* options, commit + push after each step, build both build_dev and build (check openrod2.exe isn't running), test with scripted runs and screenshots. Next task: step 1 in HANDOVER.md.
