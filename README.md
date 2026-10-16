# OpenRod 2

A from-scratch rebuild of *Street Rod 2* (California Dreams, 1990) for modern Windows, in C with SDL3.
It reads the original game's files at runtime, so you need your own copy of the DOS game.

## Building (Windows, PowerShell)

1. Install the compiler (GCC with Ninja) and CMake, then open a new PowerShell window so they are on `PATH`:

   ```powershell
   winget install BrechtSanders.WinLibs.POSIX.UCRT
   winget install Kitware.CMake
   ```

2. Download SDL3 (run from the repository folder):

   ```powershell
   powershell -ExecutionPolicy Bypass -File tools\get_sdl3.ps1
   ```

3. Configure and build:

   ```powershell
   cmake -S engine -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ```

## Running

Put the original files (`SR.EXE`, `LIB1`–`LIB3`, `*.MUS`, `MSR.SND`, `SR2_*`) in the repository root
(or next to the executable), then run `build\openrod2.exe`.

## Playing

Status: **pre-alpha**. Working: the garage (engine bay, transmission, under the car, tires, paint,
body work), the newspaper (buying cars and parts), selling, the calendar, saving and loading
(compatible with the original `HOTROD*.SAV` files), Burger's Bungalow (rivals, challenges, bets),
the **drag race** and **Mulholland Drive** (crashes and repairs included). Not yet: the Aqueduct
race, the gas station, Grudge Night, police, the end of the summer, the options menu.

Race controls: hold **Up** (or the right mouse button) for gas, **Space** / **Enter** (or a left
click) to shift — up while on the gas, down when off it — and **Down** to brake. Steer with **Left** /
**Right** (or by moving the mouse): the steering winds up while you hold it, and the car keeps
sliding until its heading matches the road again, so steer back out of every bend. Don't jump the
flag, don't sit on the rev limiter, and don't hit the hillside, the rocks or the other car.

Other keys: **F11** or **Alt+Enter** fullscreen, **M** music on/off, **F12** or **`** cheat
console (`help` lists the commands).

Reverse-engineering notes are in [NOTES.md](NOTES.md).
