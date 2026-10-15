# Street Rod 2 rebuild — reverse-engineering notes

## SR.EXE
- 16-bit DOS MZ, Microsoft C (1990 runtime), compressed with EXEPACK.
- `tools/unexepack.py SR.EXE out/SR_unpacked.exe` → clean image, 2721 relocs.
- DGROUP (DS) = segment 0x3e08. `tools/sr_dis.py seg:off [n]` disassembles the unpacked image;
  `sr_dis.calls_to(linear)` finds near/far callers.

## LIB1 / LIB2 / LIB3 (EGA graphics; LIBxC = CGA versions, ignored)
- `u16 count`, then `count` × 28-byte records:
  `u16 w, u16 h, u16 flags, u8 table[16], u32 offset, u16 size`.
- Data starts at `2 + 28*count`; each blob is `size+1` bytes, starting and ending with a 0xBC sentinel.
- Counts 4 / 212 / 83 (checked by the loader).
- Compression (decoder at 26b3:000c): byte `0x00`/`0xFF` + count c → c==0 ? literal table[0]/table[1] : (c+1)× that byte.
  A byte found in `table[k]` → `COUNTS[k]` × `VALUES[k]` (constant tables at DS:4b72 / DS:4b82). Anything else → literal.
- Decoded data = 4 full bitplanes, plane-sequential (plane0 all rows, then plane1…). Flag 0x2xx images are single-plane.
- flags high byte 0x01 = sprite (masked), low byte 4 = planes.

## Picture IDs (as used by the game code)
- 1..4 = LIB1 entries 0..3 (1 title, 2 sparkle sprite strip, 3 California Dreams logo, 4 credits),
  1000.. = LIB2 entry 0.., 2000.. = LIB3 entry 0.. (mapping at 2000:04aa).
- load_image = 2000:0d14; draw-at = 1a21:22a8(id, x, y, clear); 1a21:21be(id, clearColour).
  The draw position globals (DS:1e40/1e42) reset to 0,0 after every draw.

## Intro (1a21:0e82)
- Logo (pic 3) at (40,40) on black, wait 400 ticks; title (pic 1) wait 400; credits (pic 4) 20+600; black.
- Waits (1a21:0dca) end early only on Enter/Esc; 'M' toggles music.

## Game flow (top level 1001:4dd6 in Ghidra naming = image 0x4de6)
- Boot: start playlist MAIN, intro, copy protection (0:3388, "What is the %d letter in line %d on page %d?",
  24 random questions; this copy is cracked so it always passes; the rebuild skips it entirely).
- New game (1001:550e): license picture 1002 at (16,6), palette 1dda, dialog 2, name max 15 chars,
  trailing spaces trimmed; init 1001:5400 → money = $1200.  Old Game (1001:59e2) → HOTROD*.SAV list.
- Garage (1001:6912): pictures 1018 @0,0 + 1020 @(144,163) + 1198 @(80,172), palette 1dda,
  reg 12 = car paint (5 without a car), dialog 0 hotspots. Current car ptr DS:aa02, car list DS:aa04.

## UI system (all data-driven from SR.EXE)
- Dialog table DS:3ad8 → 36 widget lists, 18-byte widgets: flags(type = flags & 0xffd8, bit0 enabled,
  bit1 last), text ptr, colour scheme, id, x1,y1,x2,y2, keys (2 PC scancodes, low = primary, 0xFF none).
  Types: 0 hotspot, 8/0x40 window, 0x10 button, 0x80 label, 0x100 centred text, 0x200 field, 0x400 row, 0x800 list item.
  Renderer 2a21:44a8; frame 2a21:3ff6 (3px band in fill colour + 1px line in border colour at -2, style 2 adds -1).
- Colour schemes DS:1d06 (EGA/VGA), 5 bytes: text fg, text bg, fill, border, style (1 single, 2 double, 3 solid).
- Font: segment 0x26e6, offset 6: 91 glyphs (' '..'z') × 9 bytes (width, 8 rows). Text is drawn opaque.
  '`' is a 2-px space; '[' '\' ']' '^' are scroll-arrow glyphs.
- Messages (1001:3db4): table DS:11ea (layout, line1, line2); layouts DS:1e46 use speech-bubble pictures
  1057/1058/1059, text in scheme 14, centred, +6 px, 9 px line spacing; stay 350 ticks (DS:1f4a) or until key/click.
- Ghidra output: out/sr_decompiled.c (tools/dc.py <image offset>); Ghidra segment = our segment + 0x1000.

## Palettes
- Screens remap the 16 EGA palette registers (setter at 1b00:0daa / 1b00:0dfe → int86 0x10).
  Value v>7 gets |0x10, i.e. values are standard 16-colour IRGB indices.
- Tables in DS: 1da6 identity/current, 1db8 black (fades), 1dc8 in-car dashboard, 1dda, 1dec, 1dfe, 1e2e (=1dc8).
- Palette regs 6 and 12 = player car paint, from 3-byte table at DS:4d1e.
- Carb slots: DS:209c per intake grade, but 2a21:3b34 sets the stock manifold's entry to 4 on the small
  engine (picture 0x425) and 5 on the others.

## Sound
- `*.MUS` = AdLib Visual Composer songs (70-byte header, 240 ticks/beat, percussive mode).
- `MSR.SND` = timbre bank: `u8 maj, u8 min, u16 n, u16 defsOffset`, n × 9-byte names, n × 28 × i16 AdLib params.
- Song slots (SR.EXE): 1 msrbeg, 2 msr2, 3 msr24, 4 msr21, 5 pause. MSR20.MUS is never used.
- Playlists (DS:73a0 / 73b0): MAIN = 1,1,5,2,5,3,3,loop — started at boot *before* the intro and
  plays throughout; SPECIAL = 4,4,stop (then MAIN resumes). At song end PLAY.C chains to the next
  entry without silencing voices (only pitch range + tempo are reset).
- Driver = AdLib's ADLIB.C/PLAY.C compiled in (segment 2c57). Ported 1:1 in engine/src/audio/adlib.c:
  F-number table built with x1.06 per semitone (not 2^(1/12)), 25 bend steps/semitone, percussion
  note-on sets the BD bit without clearing it, PLAY.C caches last note-on volume (aftertouch doesn't).
- Timer: music rate = tempo*tickBeat/60 Hz (min 19); game tick (DS:1f66) = 1193182/16384 = 72.8 Hz.
- Sound API vector table copied from DS:73c2 to DS:bb8c (index 18 = start MAIN, 19 = SPECIAL, 23/24 = pause/resume).
- SFX are bank timbres (hitcar, police, motor, crash…) played on voice 3.
- `build/mus2wav.exe song.mus MSR.SND out.wav` renders a single song via Nuked-OPL3.

## Building the rebuild (engine/)
- Toolchain: GCC (WinLibs via winget) + CMake + Ninja; SDL3 3.4.18 MinGW dev package in `engine/third_party/SDL3`.
- `cmake -S engine -B build -G Ninja -DCMAKE_BUILD_TYPE=Release` then `cmake --build build`
  (with WinLibs `mingw64/bin` on PATH).
- `build/streetrod2.exe [datadir]` — finds the original files next to the exe, one level up, or in `datadir`.
- Test hooks: `--shot out.bmp <ms>` saves the 320x200 frame and exits; `--key-at <ms>` simulates Enter.
- Keys: Enter/Esc skip intro waits, M toggles music, F11 / Alt+Enter fullscreen; after the intro Esc quits, Enter replays.

## Status bar (2a21:1c5a / 1e86 / 1df4)
- Bottom 10 rows (y 190..199), scheme 11: "`Bankroll:`$" (DS:4844 → string) at x 0, money from the next
  8-px boundary; the rest of the bar holds centred status texts (2a21:1cde).

## Mouse cursor (2a21:2b44 / 2d90)
- Software cursor drawn by the game from mouse mickeys (int 33h fn 0/3/0Bh only), clamped to 0..314 x 0..194.
- LIB1 picture 2 = three 16x16 cursors stacked: 0 wrench (garage family, 1001:6912 sets it),
  1 pointing hand (menus, newspaper), 2 gas nozzle. Hot spots at DS:200c (x, y per cursor).
- 2d90(-4) hides it while something moves, 2d90(-2) shows it again. Only 4 colour planes, colour 0 is
  transparent (no separate mask), so the white hand is faint on the newspaper in the original too.

## Workshop animations (sprite mover, vector DS:a0da = 2a21:a2f2)
- Video driver tables DS:7a40 / 7adc (20 far pointers) are copied to DS:a0ae; entry 11 = 1a21:a2f2.
- a2f2(clipTop, clipBottom, x, y0, y1, sprite, altSprite): slides the sprite from y0 to y1 one pixel
  per step inside the band, flipping between the two sprites (bolt sheets hold two thread frames).
- Engine bay (band 4..0xb9): 381e(pic, 0) part comes down from 4-h to its seat; 381e(pic, 1) a part
  that won't fit stops 0x14 short, then message 24, then it vanishes (39a4); 34a2(1) a bolt drops in
  from y -8; 369a a bolt that comes out falls from y-9 to 0xb9 (band 4..0xb8). Removal is instant.
- Transmission (band 0x0e..0x94): 30f2 gearbox rises from 0x94 (x 200-w, or 0xb8-w when it won't fit);
  2e92(1) bolt (frame 4 of 1081) drops from y 8; 2fc4 a bolt falls out to 0x94; 322e = gearbox gone.
- DS:2018 = 0 while a whole screen is redrawn (moves suppressed), 1 otherwise.
- Sound table DS:73c2 (far pointers, copied to DS:bb8c): slot 2 paint gun (voice 5 "part" 0x5a),
  5 bolt hum (voice 4 vol 0x40 note 0x55), 9 "hitcar" 0x2e (gearbox down, body job), 10 part hum
  (voice 4 vol 0x5a note 0x52), 13 "turn" 0x61+rand(8) clink, 14 "turn" 0x3d clunk, 17 "turn1" 0x10
  (bolt turned), 18 init: voice 4 = "paint", voice 5 = "part".

## Saved games (1001:5d82 save, 1001:59e2 load, 1001:58b2 slot scan)
- `<drive>:HOTROD<n>.SAV`, n = 0..14, in the current directory (the rebuild uses the game folder).
- Order: 0x38 player block (DS:a9d8: name +0, money long +0x24, aa00/aa01 = 1 in a new game, list heads
  +0x2a current car, +0x2c other cars, +0x32 spares - only tested for non-zero), current car, car list,
  spare part nodes, 12 bytes DS:479c, 396 bytes DS:ab7e (rivals, initially SR2_DATA block 0),
  long DS:1f6a (action clock), long elapsed ticks (1f6e - 1f72), word DS:7dfe (100 new), 0x80 DS:9b40 ads.
- Car record 0x2a: +0 price, +2 model, +4 colour, +5 class, +6 trans, +8 engine, +0xa intake,
  +0xc/e/10 carbs, +0x12 tires, +0x14..1b bolts, +0x1c exhaust, +0x1e muffler, +0x20 diff, +0x22 wires,
  +0x23 timing, +0x24 fuel (0x28 new), +0x26 body, +0x28 next; each present part field is followed in
  the file by its 8-byte node (value, wear, part, next). Pools: cars DS:aec2 x16, nodes DS:b1be x190.
- Saving copies the save's name over the player's name. Blank name (spaces/underscores) on an empty
  slot: "The game must have a name !"; a name that matches a save picks that slot; dialog 29 confirms
  replacing. Screens: picture 1001 (police book), 1003 name note, 1004/1005 load/save, 1006 forget it.

## The street: Burger's Bungalow (1001:a626, dialog 20)
- Garage "Hit the street" (4dd6 case 2): car runnable (7a44 street), fuel > 8 tenths (msg 36), time +0x222.
  Gas tank (case 15) = mode 3 + 3925:2104 (gas station, dialog 22); Grudge Night (case 17, $30, Wednesdays,
  fuel > 23) = mode 4 + 3925:1970.
- Backdrop 1073 + 1149 (the guy by the door = "Look for opponent") at (0x122, 0x57), palette 1dda.
- Timeouts: first car after 0x8c ticks, then every 0x118 ticks a car arrives (999a) or the parked one
  leaves (953c(2)). Arrivals: random(100) < king chance (9802: progress 0 -> 50 or 10, 1 -> 5, 2 -> 8,
  else 12) brings The King (rival 21), otherwise one of the rivals closest to your top speed (9848).
- Rival parked: x = ((320 - w) / 2 + 4) & ~7, bottom 0xa8; hotspots from the model's hot boxes:
  roof box = "Click to challenge" (down to y 160), hood box = "Pop the hood" (a3ba: the stock car of
  that model in the bay, no panel, 800 ticks, once per rival, +0x111 time; message 67 otherwise).
- 9be0 "Look for opponent": dialog 16 list of rivals with +0xb == 1, "Call" sends the parked one away
  first; empty list -> message 41.
- 9e3c challenge: fuel < 7 -> msg 65. Dialog 19: drag (DS:add6 = 1, bets dialog 17: kicks/$10/$50),
  Mulholland (0x10, fuel > 16) / Aqueduct (8, fuel > 11) (dialog 18: $25/$150/pink slips). Stakes 0..5.
  Not enough money -> 66. 9c8e decides (reputation DS:aa00/aa01 vs rival +0xf/+0x10, engine+trans grade
  (c866), stock top speed (c6f6)); yes -> message from DS:504c, no -> DS:5038 (two tries). 800 ticks
  without an answer -> "Chicken!". Race time: drag 0x444, Aqueduct 0x555, Mulholland 0x777; DS:479c++.
- The King (975e progress): 1 -> ignores you, 2..6 -> messages 48..61 via DS:5054, 0 -> stage DS:47a6:
  0 drag (msg 62), 1 Mulholland (63), 2 Aqueduct for pink slips (64).
- Rivals (DS:ab7e, 22 x 18): +0..2 skills, +4 portrait, +6 name, +8 model, +0xa colour, +0xb met
  (1) / out (-1), +0xc races, +0xd tuning (3 bumpers, 4 roof, 0x20 blower), +0xf/+0x10 reputation.
  New game (534c/51f8): rivals 0-6 / 7-13 / 14-20 get unique models of class <3 / 3..5 / >=6.
- Not rebuilt yet: the race (be82 setup, b584 drive, 0dcb segment), cars' engine sound when driving
  in/out, the carhop (2a21:945a, picture 0x458) and rival taunts (9432), police.

## The drag race (9e3c -> be82 -> 8d9e/2a21:77f4 -> b584)
- Car figures (c294 into DS:a100 for you, c5d8 = c294 of the rival's stock model into DS:a1e4): top speed
  v = min(130, power*13/10); per gear g: ratio = 570000 / (DS:5242[trans*5+g] * v) (20 if 0), three
  torque bands {lo, hi, accel, decel}: [0, 5288[eng*2]], [5288, 528a], [528a, 0x1644]; base =
  526a[eng*5+g] * power / 100; band 0 accel = base / (g ? 5 : 1) (x2, x3 on racing tires, in 1st),
  +5% no muffler / +3% muffler 1, +5% exhaust 1, -5%/+5% diff 0/2; band 1 = base, band 2 = base/10;
  decel = base / (5 - g). Gears DS:52de[trans] (3/3/4/4), automatic = trans grade 0, shift rpm 52d8[eng].
- Your engine runs in the timer interrupt 0001:2620 (Ghidra misses it; disassemble), every 12th tick
  (72.8 / 4 / 3 = 6 Hz): gas adds band accel to rpm (limiter 0x1644; > 15 - wear/1000 steps on it blows
  the engine: DS:a1bc |= 5), off gas subtracts decel; rpm > 5000 wears engine + trans; a shift (left
  button / key) goes up on gas or in band > 0, down otherwise; a power shift above 5000 rpm can drop the
  transmission ((100 + wear/100) in 3277: a1bc |= 0x11); brake: speed -= speed/16 + 2; speed = rpm / ratio.
  The interrupt also drives the engine sound: AdLib voice 3 "motor", bend rpm*3 - 0x1194 (2c57:0ba6),
  volume |rpm-1500|/128 + 0x50, full without a muffler; PC speaker via port 0x42 otherwise.
- Per frame (18.2 Hz, d908): distance += DS:8016[speed] (1/64 segment), opponent the same; drag strip
  segments 200 (DS:9ae2) .. 0x17c (DS:a2aa). Opponent (cb98): own engine stepping with shift point
  (skill - 10) * 200 + shift rpm, but held to your speed + 10 when side by side and to 70/80% of its top
  speed when far ahead (cb00 sets a speed directly). b1b2 at the line: winner, time = steps*10/6 tenths,
  avg speed. Start (8ef4): "Get ready !", 0x6e + rand(0x67) ticks, moving = "Ya jumped the light" (0x81).
- Results (b584): win -> DS:512a[stake] message + money, pink slips = the rival's car (rival leaves the
  game); loss -> 0x54 / 0x53; blown engine removes engine+manifold+carbs (b46e), dropped trans (b4da);
  dialog 23 shows the time ("Your winning time ...", "In %d.%d sec you covered only %2d%% ...").
- Race screen: palette 1dc8, 2013 roof at top, dashboard by model (patch[0] high byte: 0 -> 2011,
  1 -> 2019, 2 -> 2012) at y 100, gauge centres DS:1c0a/1c10 (tach) and 1c22/1c28 (speedo), gear plate
  table DS:1cd0 at (0x108, 0x27). Opponent pictures SR2_DATA -> DS:aa9e (patch[0] low byte). The road is
  the rebuild's own drawing for now (the 0dcb renderer is not ported).

## Rebuild deviations / modern options (planned options menu)
- Newspaper paging: next page starts at the first ad not fully shown (original repeated one entry).
- Hover highlight (`ui_highlight`, on by default) and its colour (`ui_highlight_colour`, EGA 0..15,
  default 14 yellow): button outlines, hotspot corners, list rows drawn in the highlight colour.
- `opt_game_cursor` (on): the game's own cursors instead of the Windows arrow.
- `opt_animations` (on): the workshop moves above; off = instant (sounds stay). Cheat console: `anim`.
- `opt_anim_speed` (100%, slider 25..400% in the options menu): speed of those moves. Cheat: `anim N`.
- New game: an empty name is not accepted (the original allowed it).
- `opt_save_info` (on): save/load lists show the pointed-at save's file time and in-game date in the
  status bar (no format change: the time is the file's). Arrow keys move the choice; clicking the
  chosen game again loads it.
- `opt_tune_music_duck` (12 = 9 dB): music quieter on the timing-light screen (0 = original).
- The bolt clink holds its key 15 ms (the original let go at once; with the emulated chip's exact
  timing the slow attack would make it silent).

## Tools
- Python RE tools need Pillow, numpy and capstone (installed for C:\Program Files\Python313).
