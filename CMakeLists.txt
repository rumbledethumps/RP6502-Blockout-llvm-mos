cmake_minimum_required(VERSION 3.18)
add_subdirectory(tools)
set(LLVM_MOS_PLATFORM rp6502)
find_package(llvm-mos-sdk REQUIRED)
project(MY-RP6502-PROJECT)
add_executable(blockout)
rp6502_executable(blockout)
target_sources(blockout PRIVATE
    src/colors.c
    src/bitmap_graphics_db_2modes.c
    src/blockout.c
    # src/main_gpt.c
    # src/main_blocks.c
    # src/main.c
)
