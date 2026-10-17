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
- `build/openrod2.exe [datadir]` — finds the original files next to the exe, one level up, or in `datadir`.
- Test hooks: `--shot out.bmp <ms>` saves the 320x200 frame and exits; `--key-at <ms>` simulates Enter.
- Keys: Enter/Esc skip intro waits, M toggles music, F11 / Alt+Enter fullscreen; after the intro Esc quits, Enter replays.

## Status bar (2a21:1c5a / 1e86 / 1df4)
- Bottom 10 rows (y 190..199), scheme 11: "`Bankroll:`$" (DS:4844 → string) at x 0, money from the next
  8-px boundary; the rest of the bar holds centred status texts (2a21:1cde).
- In a race 2a21:1048 (after 1c5a: centred texts in x 0x58, width 0xa0) draws DS:4846 " Gear: "; 1e86
  skips the money for a label whose 2nd char is 'G' or ' ', so ad36/ad3a stay put. d908 prints the
  gear when it changes (2a21:1f5e: DS:1e1e "0".."4", "N", "D" for a124 + 4) at the next 8 px after
  the label; nothing before the race starts. Checked against the original (DOSBox screenshots).

## Cars driving in and out (2a21:89fa)
- Modes: 1 arrive / 2 leave at Burger's (1001:953c), 4 your car drives into the garage from the left,
  6 a car drives out to the right (the old one when switching cars, 1001:6912 param 3), 3 / 7 / 8
  just draw it. The garage (6912) is entered with 1 (after the street, a race, a load: drive in),
  2 (after hood / gearbox / underside: drawn), 0 (no redraw) or 3 (switch: out, then in). Start x
  -w (in) or the spot (out), distance w + spot (in), 0x140 - spot or w + spot (out).
- The sound (89fa with 7a44(your car, 1)): your car could run: 1001:20a0 starts the motor (3c57:0b2c,
  0ba6(0, 0x50)), DS:1bca = 1 and the timer interrupt (0001:284c, every tick) sets its pitch from the
  distance d = |ad5c - b866| between the car and its stop: min(d * (d >> 1), 0x4e20) >> 1 (16-bit),
  volume 0x50, or full when the car has a blower (bit 0x20 of 1001:4804 / the rival's +0xd -> b8d4);
  2176 stops it. Your car can't run: sound slot 10 (the hum) instead, off afterwards.
- The siren (sound slot 11) warbles in the same interrupt: DS:2004 counts 0..0x28 per tick, bend
  0x2000 at 0, 0x3fff at 0x14 (2c57:104e).
- Under the car (2925:2b26 before 2e60): the creeper 1198 slides from (0x50, 0xac) 2 px right and 1 up
  per step (18e2(2)) while x < 0x80, with the car and the jack 1020 (at 0x90, 0xa3) drawn over it.
- List boxes (2a21:4ebc): id -7 (the up arrow at the top) one back, -10 (down) one on while top + 1 <
  count, -8 / -9 a page (the list's rows).

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
- Per frame (9.1 Hz: 11fc ends with 2a21:1924(8), a wait for 8 ticks of the 72.8 Hz timer; d908): distance += DS:8016[speed] (1/64 segment), opponent the same; drag strip
  segments 200 (DS:9ae2) .. 0x17c (DS:a2aa). Opponent (cb98): own engine stepping with shift point
  (skill - 10) * 200 + shift rpm, but held to your speed + 10 when side by side and to 70/80% of its top
  speed when far ahead (cb00 sets a speed directly). b1b2 at the line: winner, time = steps*10/6 tenths,
  avg speed. Start (8ef4): "Get ready !", 0x6e + rand(0x67) ticks, moving = "Ya jumped the light" (0x81).
- Results (b584): win -> DS:512a[stake] message + money, pink slips = the rival's car (rival leaves the
  game); loss -> 0x54 / 0x53; blown engine removes engine+manifold+carbs (b46e), dropped trans (b4da);
  dialog 23 shows the time ("Your winning time ...", "In %d.%d sec you covered only %2d%% ...").
- View geometry (0dcb:062c/0684): x = 160 - (camera_x - x) * 4 / d, y = 60 + (h - 1900) * 4 / d with
  d = distance/8 + 4 (distance in 1/64 segment); ground h 2000, road edges 1500/3300, centre 2400, you
  at 2560 (a564), opponent at 2200 (a9cc). View clip rows 0x12..0x65, mirror x 0xf0..0x13f rows 0x16..0x2a.
- Opponent picture (0dcb:af98, table DS:b7b6 built in 37ce from DS:aa9e): kind by distance ahead A:
  >= 0x3c0 none; >= 0x80 kinds 3..8 (medium rear, pre-scaled smaller); < -0x38 behind (mirror, 9..);
  -0x38..0x40: same lane -> 2, behind -> 9, left -> 0 (side view at x 0 cut to w - x + 0x40 columns);
  0x41..0x7f: left -> 1 (big rear, right edge at x, top y - h + 0x11) else 3. Changes step one kind per
  frame (not ported). The copies (37ce, 0dcb:ba72 + b7fa): a mask byte keeps the rows and columns
  whose bit 0x80 >> (n & 7) is set (new size = bits set * (w & ~7) / 8 + bits among the first w & 7).
  The masks: SR2_DATA's eighth block (396 bytes at file offset 1850) is read to DS:8e3c (2a21:6e6a
  patches DS:14ea, the block table DS:14dc / sizes 14f0), and each DS:aa9e record's +6 / +8 / +0xc
  is pointed at 8e3c + 0x42 * set + 0 / 0x16 / 0x2c. Kinds 3..5 = medium rear by +6's bytes 9..11,
  6..8 = kind 5 shrunk again by +8's bytes 9..11, 10..12 = the front (+0xa) by +0xc's bytes 9..11;
  the police (set 5, DS:aafa = aae4): one copy of 2036 by +6's byte 0 for kinds 2..6, none for 7 / 8,
  2039 by +0xc's bytes 0..2. af98 stands kinds 4..8 on the ground row of their slice (DS:9ef0[(d +
  1) >> 3], beyond 15 the highest road row 975e), kinds 2 / 3 below row 100 at 0x65.
- Starter (0dcb:a746, a5aa): torso 0x816 at (8, 0x12), arm frames of 0x817 next to it; frame 0, then
  frame 1 from "Go" (DS:ad0a); slides left 8 px per frame once you roll (DS:a2b4 += 4), gone after 0x58.
- Timing (8d9e/8ef4): 0xf + 0x19 ticks, then "Get ready !" for 0x6e + rand(0x67) ticks, then "Go !".
  The acceptance message is read at Burger's before the race screen appears.
- Race screen: palette 1dc8, 2013 roof at top, dashboard by model (patch[0] high byte: 0 -> 2011,
  1 -> 2019, 2 -> 2012) at y 100, gauge centres DS:1c0a/1c10 (tach) and 1c22/1c28 (speedo), gear plate
  table DS:1cd0 at (0x108, 0x27). Opponent pictures SR2_DATA -> DS:aa9e (patch[0] low byte). The road is
  road.c (see "The roads").

## The roads (segment 0dcb; rebuild road.c)
- Tables in far segment 0x3726 (DS:86f8.. hold it), one word per segment, 0x3de segments: 27b6 left side
  flags, 2f72 right side flags, 372e heading, 3eea road flags, 46a6 height, 5146 / 5902 terrain height
  left / right, 665a object position; 4e62 (Aqueduct) and 60be/61a6/6572 (copies of 0xc0.. for race
  types 2/4). Built at a new game (2866(1) + 24d8(100)) and at every load (2866(0) + 24d8(DS:7dfe)):
  the random state 2a21:6180 (state = state * 0x015a4e35 + 1, ((state >> 16) & 0x7fff) % n) is saved
  in the player block at DS:a9f8 after the town part, so a load rebuilds the same roads; DS:7dfe picks
  one of 10 layouts of 13 pieces (DS:7c3e): 1/4/6 right bend (1d04), 2/3/5 left (1dfa), 7/8 turn-offs
  (217e/23d4, only where the terrain differs), 9 bridge (2282, terrain -500, one lane), 10/11 S-bends
  (1eea/2034). Bends turn the heading 7 per segment (6 + 1; DS:96ac, 5 + 1 on slow PCs). Hills: the drag
  strip gets one hump (DS:7c1c up and down at 0xdc and 0x101), past it the height changes by DS:7d42
  per 10 segments. Terrain beside the road rises/falls 200 per segment over 380..399 / 400..419 /
  500..519 / 600..619 / 700..719. Scenery flags come from the random state (2866, chances scaled by
  DS:add8 = 1 on slow PCs). The town part (segments 0..0xb3) is only built in a new game.
- Road flags: low byte 0x80 one lane (edges 2000/2800), 0x20/0x40 road turns off, 8 centre dash, 0x10
  white line (0x17b drag finish, 0x3bf Mulholland), 2 roadside post (kind 9), 4 / 0x1000 object at
  665a; high byte 0x100 kind 5, 0x200/0x400 bend signs (kinds 0x10/0x11), 0x800 bridge sign (0xf),
  0xf000 a gantry over the road (55ce) with sign kind 0xd / 0xc / 0xe. Side flags: 1 lamp post (kind 1),
  4/8 kinds 7/8 at the edge, 0x20 kind 0, 0x40 kind 10, 0x80 kind 0xb (2: far out, 3 sizes smaller),
  0x100 kind 4 (0x200 farther), 0x800/0x1000 turn-off signs (3/6), right 0x2000/0x4000 S-bend signs
  (0x12/0x13). Objects are drawn for the nearest 10 segments only, sizes clamp(9 - slice, 0, 6).
- Projection 07e4 per frame: slice 0 at d 4, slices 1..14 at d = 12 - sub/8 + 8(i - 1), slice 14 at
  116; per slice x of the road edges (1500/3300), shoulders (1500 - 400 / 3300 + 400, Mulholland 200),
  far out (-100 / 4900), ground y and terrain y, with turn = a56a - heading[seg + i] and rise =
  height[seg] - height[seg + i] (x += turn, y += 2 rise). 0ce4 does the same for the mirror.
- Backdrop 3e66: spare page with the city 2002 at row 102 and the hills 2055 at the bottom (37ce);
  horizon hz = y(d 0xf0, rise to the previous segment) clamped 0x13..0x51 (0x12 becomes 0x52!); city
  rows 184-hz.. into rows 18..hz-13 scrolled by heading*2, hills rows 163..182 at hz-12 scrolled by
  heading*4 (8-pixel steps); grass (colour 9) from the highest road point below hz+8 down; road
  strips beyond that crest are not drawn.
- Painter 6a32, slices 14 -> 1: road strip 4322 (rows between the slice and the next nearer one: grass
  9, road 1 or white 15, centre dash 4 of width DS:7e52[slice]), then the roadside objects (5158 ->
  4154), then the opponent (af98) when he is on that segment. Shapes: SR2_SHP, index DS:80a2 (offset,
  size per kind), each kind = count of sizes, 16-byte size records of up to 6 (colour, span count)
  pairs, then spans of 3 signed bytes (dx, dy, length), mirrored for the left side. Palette 1dc8:
  register 1 black road, 9 green grass, 4 white, 10 red.
- Mulholland (add6 0x10): backdrop 3cca is one picture (2056 at page row 102) lifted by the rise to 4
  segments back (+-8), no grass fill; hillsides (colour 14) are polygons in 6a32 from the screen edge
  at the terrain height to the shoulder (and the ravine before a bridge, tables DS:7e1e/7e22/7e3a);
  bridges (road flag 0x80) get railings 5404/555e (colour 10), right flag 0x8000 a guard rail
  (colour 5, posts every other segment from DS:96f4); side flag 0x10 rock piles (51e0, kinds 7/8,
  tables 7ed0/7e9a/7ece); road flags 4 / 0x1000 put kind 7 / 0x14 on the road at 665a * width / 16,
  and in the nearest segment 69d4 checks them against the car's corners (DS:bb88 / add4).
- Not ported yet: the town view (bit 0 roads, 52e2 picture objects, 4e70).
- The rear-view mirror (11fc after the windshield; rebuild road.c mirror_* / aq_mirror):
  - Projection 0ce4 with 06ba / 070e: x = 0x118 - (cam - (lat + turn*d/4)) / d, y = (rise*d - eye +
    h) / d + 0x24 (a quarter of the scale), turn = road heading - a56a, rise = height - a56c (the
    other way round); slices 0 at d 8, then 8 + s/8, +8 .. 32 + s/8, 40, 48, 56, from segment seg
    backwards (Mulholland clamps slice 0's turn to +-0x46 past +-0x3c); the terrain rows read
    forwards (seg, seg+1, ..) and the 0x79e row array (a7ea) is written twice per slice (quirks).
  - 36d6 clips to x 0xf0..0x13f, rows 0x16..0x2a. 57a6: picture 2005 (glass and frame, put on the
    spare page at (0xe8, 0x66) by 370a) copied to (0xf0, 0x14) and 16 x 2 of it under the stalk at
    (0x110, 0x12); the horizon strip 2063 (Mulholland 2037, 370a(1) at (0xe8, 0x81)), 2 rows (0x14)
    at the farthest ground row - 2 (- 10); the Aqueduct has stars instead (85a0: DS:7f8e 9 points
    colour 2, 7fb2 3 points colour 5, x - heading*2 % 320 / 4).
  - 5aee paints slices 7..1 like 6a32 does ahead, every array pointer one slice in: the flags of
    segment seg - 1 - k, road strips 4852 (grass from x 0xf0, rows < 0x2b, dash width DS:7e52[13 + k],
    finish white when a55e >= 0x169 / Mulholland 0x3ad), objects only for k < 7 at size slice
    (k >> 1) + 7, gantries (drag) at slice k + 5, Mulholland hillsides, bridges (5404 with
    (seg - k, k < 4)) and guard rail; then the opponent; 5a2c draws the frame's sides (x 0xf0 / 0x13f
    colour 2, 0xf1 / 0x13e colour 4).
  - The starter (a85c, a92a in the Aqueduct) once she has left the window (a7ac clear) at segment
    9e6a (a746 keeps it at min(you + 1, him) while she's there): pictures DS:9a2a (a5aa: 2000, 2022,
    2057, 2058; Aqueduct 2078..2081) by slice (k - 1 < 3 ? / 2 : - 1, at most 3, none at 6), her
    feet always at row 0x27 (a85c subtracts y - 0x2a), in the Aqueduct on the stripe's left edge.
  - The opponent (af98 kinds 9..12): the front picture (DS:aa9e record +0x0a, 40 x 18), 10..12
    pre-shrunk copies (record +0x0c scale bytes, unknown: the rebuild scales 12/d), at 06ba(d, a9cc,
    10 - (a56a - his heading)), 070e(d, 2000, his height - yours), bottom centre.
  - Aqueduct: 1b02 projects 8 slices (d 2 first) with the saved wall offsets, turn = -a56a, the
    middle at +a56a; 9ce8 turns them round the mirror's far middle with the windshield's tilt and
    runs 905c with b194 = 7 (no collisions, bushes at size (slice - 1) / 2 + 6), table read from the
    reversed copy 628e (segments seg - 8 + k, then seg - 1 - k). Checked against DOSBox screenshots.

## Steering, collisions and Mulholland Drive (rebuild race.c)
- Setup 1001:bc88: drag 200..0x17c, Mulholland 0x172..0x3c0 (Aqueduct 8..0x156); tire wear 300/200
  (700/400 on Mulholland); a race needs fuel > 6 / > 16 / > 11 and burns 5 / 15 / 10 (car +0x24,
  b584); time 0x444 / 0x777 / 0x555. Police on Mulholland: 1 in 4 (DS:ab2c, waiting at segment
  9aca = 0x1e0 + rand(0xf0)) - not ported yet.
- Steering in the 6 Hz part of the timer interrupt (0001:2e8c): input 3b32 (keys set +-1, mouse adds);
  if it pushes further, a102 += 2 * input (to +-24), else a102 = 0; no input -> 0. The wheel a104 =
  a102, springs back by a104/4 + 1 per step. Per frame (1dcb:1774): a56a -= a102/2 + 1 (when moving
  and |a56a - heading[seg] - a102| < 200); a564 -= ((frame distance / 8 + a10c) * (a56a -
  heading[seg])) >> 2 (a10c = DS:521e[tire grade * 5 + wear / 2000]); Mulholland clamps a564 to the
  lanes +-150; a flat tire (past DS:a10a) pulls right 25 per frame. After the finish (and in demo
  mode) 12ca puts a564 on the road and a56a = the road's heading. Lanes (12ca): 1500..3300, one lane
  2000..2800 on road flags 0xe0; the same four segments on (985c/a558) for the opponent.
- Dashboard: wheel pictures DS:78ce[dash * 3 + (0 straight, 1 left, 2 right)] at (0x50, 160), the
  knob (DS:7e84 shape, spans at 3725:0000) at DS:1c80[a104]; the wheel's top DS:78e0 (pic, x, y)
  over the view (573e).
- Collisions 1dcb:148a (6 Hz, after steering), car from a564 - 30 to a564 + 70: hillside (terrain >
  0 and past lane - 150/4 left, + 150/2 right), off the road (lane -+ 0x104, Mulholland 0x96: falling
  when the terrain drops, 7da2: 8 frames sliding 100 sideways and sinking, car totaled), bridge rails,
  posts (road flag 2) and trees (right side flags 0xc0 of the next segment) off the shoulder; on
  Mulholland also things on the road above 30 mph, rocks, turn-offs above 60, the guard rail above
  39 (below: bounced back 300, heading +30, speed -10). Any hit: a1bc |= 0x21.
- The opponent (d4cc/cf9a): lateral a9cc steers 1/8 of the way to an aim: the middle far off,
  weaving +300/-200 round the middle 3..12 segments ahead, behind you he moves over, beside you he
  passes or blocks; cf9a looks 6 segments ahead for rocks, gantry posts, objects, you and bridges.
  He slows to 59 in turn-offs and once per bend to his top speed - 30 (- 3 when behind). d908: right
  behind him and faster (9a48, from af98) = crash (a1bc |= 3); side by side (picture 0) steering left
  knocks him back a segment and 10 mph; a rough rival (skill byte +1: rand(((-s - 15) >> 2) + 2) ==
  0) shoves you 300 right, +-15 heading, two steps of braking.
- Crash results (b584 + af0a): windshield 2064 over the view (rows 0..89 at 11, the rest at 101), dash
  edge 2048/2065, corner 2066, past 90 mph also DS:5136's three pieces; pink slips: car gone (msg
  0x5b, 0x5c with tires); 91+ mph or fallen: totaled, scrap body value / 10 + 19 (msg 0x5d); below 31:
  msg 0x5e (0x5f + new tires); else dialog 24 (61..90: rand(40) + 100) or 25 (31..60: rand(20) + 100)
  Fix it (pay) / Junk it (40% of the price). Bets are lost. ac68: tires from the spares, else $50.

## The police on Mulholland (rebuild race.c police / ticket)
- be82: Mulholland and not The King: DS:ab2c = 1 with a 1 in 4 chance (2a21:6180(4) == 0), waiting at
  segment 9aca = 0x1e0 + rand(0xf0). d908 after cb98: state 1 at that segment: unless you are 7+
  segments ahead of the opponent ab2c = 0; else ab2c = 2 and d85c swaps the opponent for the police:
  c5d8 / c6f6 of rival 0x15 (The King's car figures, top speed for a33c = 60%, a2b0 = 80%), cb00 to
  the opponent's speed, DS:a80a = ac32 (rival 10's record: its skill byte, and the standings after),
  the picture table b7b6 from 9a94 (37ce: record DS:aafa = aae4 = set 5 of aa9e - big rear 2035,
  medium 2036 pre-shrunk once, front 2039 - but the opponent's own side view), 8d0a(5): palette
  registers 12 / 3 from DS:4d1e[5] (your car's paint changes too). State 2: once you are no more than
  15 segments ahead, ab2c = 3 and the siren (sound slot 11, 2c57:0fcc: voice 2 timbre "police"
  volume 0x7f, pitch range 5, bend 0x2000, note 0x50; off: note off, bend 0x2000). State 3: they get
  more than 2 segments past you: a1bc |= 0x41. Any other end of the race with ab2c >= 2 and a1bc set
  (lost, crashed, broken) also |= 0x41; the siren stops.
- cb98: with ab2c >= 2 and you at 0x33+ mph the police hold their speed to yours + 0x20. Running into
  their back is no crash, and they don't shove you (d908).
- b584 (a1bc & 0x40, you didn't win): siren off, adc0 the ticket: 2051 (the officer's hand) at
  (0, 0x45), 2009 (the ticket) at (88, 0x14), your name at (0x5e, 0x48) and the car's name at
  (0x5e, 99) cut to the ticket's width - 8 (ad36), scheme 13; new tires first if they blew (ac68,
  its shortfall adds to the fine); fine 20 / 50 / 75 below 0x33 / 0x51 / above (your speed);
  "You have been fined $%2d for speeding" (DS:51e7) in the status bar for 0xfa ticks, then paid
  (return 3: counted as lost, bets untouched, no repair dialog). Without the money: message 0x52,
  all your money (return 4) and a626 runs a9fc: the jail - Burger's (1073) behind bars (1153 / 1154 /
  1155, blits table DS:5070), cars still coming and going (999a / 9b26 every 0x118 ticks), dialog 21
  "Wanna quit ?" - then 1bc6 |= 4, which ends the game (36ac). Not rebuilt yet (End of summer step).

## The Aqueduct (add6 8; rebuild road.c aq_* / race.c)
- Setup: segments 8..0x156 (bc88), tire wear 700/400 like Mulholland ((add6 & 7) == 0), fuel > 11,
  burns 10 (b584), time 0x555 (9e3c); race screen 1dcb:8840 instead of 37ce. The road heading
  (DS:9ad8) and height (9dc8) point at constant 0 words (3722:0002 / 0000): the channel is straight.
  Starter 0x813 + arm 0x814 (2067/2068, a5aa); every race draws 2003 at (0, 0x12), the window's left
  pillar (56e8, after the starter).
- Table 1dcb:33ac in the far segment at 4e62 (one word per segment, 0x172 of them; a copy reversed at
  628e for the mirror): 2 at 3..6 of every 0x18 (crossings: the middle stripe stops, no bumping),
  1 every 6 (joints: black lines across the floor and up the walls), 4 / 8 at 0x11 / 0x10 of every
  0x18 (dark patches on the left / right wall inset a quarter; 0x89 / 0x88 and 0x119 / 0x118 are
  0x100 / 0x200, the whole height), 0x10 blocks at 4, 0x4c, 0xdc, 0x13c, 0x16c, 0x80 pillar pairs at
  0x7c, 0x10c, 0x20 / 0x40 bushes (left a quarter in / right an eighth in), 0x400 finish at 0x155.
  Past the end it reads the next table (5146, terrain left).
- 11fc: 18ec projects 15 slices (d as 07e4) of floor y (h 2000 + 985e), floor edges 0x5c8 / 0xcf8
  (+ a2b8 / a7a6), middle, wall top y (0x67c + a5fa) and wall tops 0x438 / 0xe88 (+ 9a5a / 9b14),
  the middle stripe 0x960 +- 0x28 near and far, and the car's sides a564 - 0x6e / + 0x208 (9ac8 /
  9a58) at d 4; 062c clamps the camera to 0x564..0xd5c; 0684 adds 9a56. a094 saves those offsets
  (aa9c / b8be / bb8a / b8e6; ad6a / b166 stay 0), clears them and works out the tilt 9e70: a car
  side past a floor edge puts you on a wall (9d9c left: tilt v*0x6c/400, 9e4e right: v*-0x5a/400,
  the offsets lift the view, you slide back down 15 per frame (+25 near the top) unless in demo /
  autodrive); 9f08 (not on a crossing): a car side on the stripe or across it since last frame bumps
  the view +-4 and costs 2 mph above 10; 9f9a adds b8bc (rolling), keeps b190 = |tilt|, ad68 = which
  of 7 turned shapes (-45..45, DS:7fbe), and a pitch b7b0 from the tilt's change (into 986a). All
  points then turn round the far middle (0379, sin table 02b4 at DS:1504, 255 = 1).
- Sky 8b16: rows 0x12..far floor (or all when tilted) colour 1, stars (0dcb:01da driver entry 19:
  18 points DS:7f2e colour 2, 6 points 7f76 colour 4, x + heading), clouds = SR2_SHP kinds 23 / 24
  (81da = 23, 81dc = 3 kinds; this format holds 7 turned versions per size: count, 7, 4-byte
  pointers, 16-byte size records, spans; 2a21:7446) drawn by 8288 (b190 >= 0x37: the upright one
  turned span by span as lines).
- Painter 905c: concrete colour 5 (walls / floor polygons), floor edges black, wall tops colour 15,
  stripe colour 2 edged black; per slice far to near the finish (white), joints, wall patches (8ece;
  862a: covering the bottom row across x 0x5a / 0x13e sets a7d2 1 / 4), bushes (kind 25, size
  slice / 2); then near to far 8b5c boxes (sides / front colour 2, top 5, black edges unless < 3 px):
  0x10 a block across the middle third, 1/7 of the wall high (its top touching the bottom row:
  8674 -> a7d2 0x20 / 0x60), 0x80 two thin pillars full height (their feet); the opponent (af98).
  Polygons are clipped by 7f30 (Sutherland-Hodgman, 0350 / 0327 for the cuts) and filled by a0f6.
- 1dcb:1cd2 (148a for the Aqueduct): car side < 0x439 or > 0xdec = over the top of a wall (b8c8 1 /
  -1), or a7d2 set: crash (a1bc |= 0x21). a1c0 then rolls the car: 8 frames of b8bc += 0x20 (or
  -0x22), a564 += +-0x28, heading - first / 6, the view dropping 9a56 = k * speed / 6; below 31 mph
  one frame tipped 6 and dropped 10. cee0: every other frame, if this step would pass sub 0x38 with
  a block or pillar two segments on (0x90), a2d8 checks their feet at d 5 / 13 (8674) and stops you
  at 0x38 so the next picture catches the hit.
- Opponent: d4cc without bends or bridges, sideways by 1001:d2a8 (also you on autodrive / past the
  line in 12ca, target the middle -0x140 / +0xa0, lanes 985c / a558 = the wall tops): five lines
  across the channel (a third up the left wall .. halfway up the right; fixed 0x514.. in demo); a
  block or pillar in the next 8 segments picks one by where the other car is, else pass him; step =
  (target - x) * 6 / (segments to it, less 2, + 1) / 6. cb98 skips the bends ((add6 & 0xe) == 0).
  af98: up a wall he is lifted (b8be - x + 0x5a0) * (b8e6 - aa9c + 0x154) / (400 + b8be) and tilted
  (ae9c: up to 0x2d by how far up, + the view's tilt); 7 turned copies made by a0ee (0dcb:c342) at
  setup, the side and big rear views only upright. The rebuild turns the picture itself (approx.).
- d908 for drag, Mulholland and Aqueduct: while the starter is in the window (a7ac), a564 left of the
  middle + 0xa0 = a1bc 0x101, b584: message 0x78 "You just bumped the starter...", bets untouched.
  12ca's autopilot steering also waits for her to go.
- Results: The King: win 0x50 (Aqueduct) / 0x4f, loss 0x55 (drag) / 0x56; a626 then: lost to him ->
  47a6 = 0, ad03 = 0, 47a0 -= 3, 47a2 -= 2; beat him -> 47a6 = 1 (drag) / 2 (Mulholland), Aqueduct ->
  a51c and 1bc6 |= 2 (not rebuilt yet). Aqueduct wins count in 47a2.

## The King beaten (1001:a51c, 3925:27da; rebuild scenes/king.c)
- a626 after b584 when the Aqueduct pink-slip race against The King is won: a51c: palette 1dec, the
  screen cleared to colour 8 (21be with DS:1e44 = 200 rows), picture 1083 ("Well, you've done it...")
  at (8, 0x14), 3d80(0x2ee); sound vector 19 (SPECIAL playlist); 3925:27da: SR2_END (read by 2714:
  0x1f8 bytes = 84 frame rects w, h, x word, y, -; 0x2dc bytes = 183 steps frame, x, y, delay) - the
  sheet 1181 over 1211; a step with a non-zero delay is shown (b028 of the changed rectangle, paced
  by 2a21:1924(10)), the next step first restores the background there (x rounded to 8); a negative
  delay pastes 1195 (0x88 x 0x2e from (0x60, 0x68)) into the background at (0x88, 0x92) and the
  sheet becomes 1195; a negative frame ends it, then 18e2(0x50) retraces; vector 18 (MAIN). Then
  1bc6 |= 2 -> the game's ending 36ac (not rebuilt: the rebuild returns to the garage).

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
