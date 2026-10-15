#!/bin/sh
# Downloads the SDL3 MinGW development package into engine/third_party/SDL3.
set -e
VERSION=3.4.18
cd "$(dirname "$0")/../engine/third_party"
curl -sSfL -o sdl.tar.gz "https://github.com/libsdl-org/SDL/releases/download/release-$VERSION/SDL3-devel-$VERSION-mingw.tar.gz"
tar xzf sdl.tar.gz
rm sdl.tar.gz
rm -rf SDL3
mv "SDL3-$VERSION" SDL3
echo "SDL3 $VERSION ready in engine/third_party/SDL3"
