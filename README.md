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
(or next to the executable), then run `build\streetrod2.exe`.

Reverse-engineering notes are in [NOTES.md](NOTES.md).
