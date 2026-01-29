cmake_minimum_required(VERSION 3.18)
add_subdirectory(tools)
set(LLVM_MOS_PLATFORM rp6502)
find_package(llvm-mos-sdk REQUIRED)
project(MY-RP6502-PROJECT)
add_executable(blockout)

rp6502_asset(blockout 0x10000 images/background-320x180.bin)
rp6502_asset(blockout 0x17080 images/start_screen-180x180.bin)
rp6502_executable(blockout
    background-320x180.bin.rp6502
    start_screen-180x180.bin.rp6502
)

target_sources(blockout PRIVATE
    src/colors.c
    src/bitmap_graphics_db_2modes.c
    src/blockout_render.c
    src/blockout_state.c
    src/blockout_math.c
    src/blockout_shapes.c
    src/blockout_pit.c
    src/blockout_demo.c
    src/blockout_input.c
    src/blockout.c
    src/ezpsg.c
    src/sound.c
)
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG" CACHE STRING "Flags used by the C compiler for Release builds." FORCE)
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG" CACHE STRING "Flags used by the C++ compiler for Release builds." FORCE)