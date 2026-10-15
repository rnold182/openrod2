# OpenRod 2

A from-scratch rebuild of *Street Rod 2* (California Dreams, 1990) for modern Windows, in C with SDL3.
It reads the original game's files at runtime, so you need your own copy of the DOS game.

## Building

1. Install GCC (e.g. `winget install BrechtSanders.WinLibs.POSIX.UCRT`) and CMake.
2. `sh tools/get_sdl3.sh`
3. `cmake -S engine -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build`

## Running

Put the original files (`SR.EXE`, `LIB1`–`LIB3`, `*.MUS`, `MSR.SND`, `SR2_*`) in the repository root
(or next to the executable), then run `build/streetrod2.exe`.

Reverse-engineering notes are in [NOTES.md](NOTES.md).
