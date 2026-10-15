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

## Rebuild deviations / modern options (planned options menu)
- Newspaper paging: next page starts at the first ad not fully shown (original repeated one entry).
- Hover highlight (`ui_highlight`, on by default) and its colour (`ui_highlight_colour`, EGA 0..15,
  default 14 yellow) — both to be exposed in the options menu.
