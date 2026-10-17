cmake_minimum_required(VERSION 3.20)
project(StreetRod2 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(SDL3_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL3/x86_64-w64-mingw32/lib/cmake/SDL3")
find_package(SDL3 REQUIRED CONFIG)

add_library(opl3 STATIC third_party/nuked_opl3/opl3.c)
target_include_directories(opl3 PUBLIC third_party/nuked_opl3)

add_library(sraudio STATIC src/audio/adlib.c src/audio/mus.c)
target_link_libraries(sraudio PUBLIC opl3 m)

add_executable(openrod2 WIN32
    src/main.c
    src/game.c
    src/car.c
    src/cheats.c
    src/data.c
    src/video.c
    src/sound.c
    src/options.c
    src/anim.c
    src/save.c
    src/rivals.c
    src/road.c
    src/scenes/street.c
    src/scenes/gas.c
    src/scenes/race.c
    src/scenes/king.c
    src/scenes/files.c
    src/assets/exe.c
    src/assets/lib.c
    src/ui/font.c
    src/ui/dialog.c
    src/scenes/intro.c
    src/scenes/license.c
    src/scenes/garage.c
    src/scenes/newspaper.c
    src/scenes/hood.c
    src/scenes/trans.c
    src/scenes/under.c
    src/scenes/deals.c
    src/ui/partlist.c
    src/ui/listbox.c
)
target_include_directories(openrod2 PRIVATE src)
target_link_libraries(openrod2 PRIVATE sraudio SDL3::SDL3)
target_compile_options(openrod2 PRIVATE -Wall -Wextra)
target_link_options(openrod2 PRIVATE -static-libgcc)

add_executable(mus2wav tools/mus2wav.c)
target_link_libraries(mus2wav PRIVATE sraudio)

# Ship SDL3.dll next to the game.
add_custom_command(TARGET openrod2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:openrod2>)
