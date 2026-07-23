cmake_minimum_required(VERSION 3.10)
project(mbl C ASM_NASM)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Freestanding C99 flags (Zero stdlib dependencies)
add_compile_options(-nostdlib -ffreestanding -fno-stack-protector -Wall -Wextra)

# Static Library Target: libmbl.a
add_library(mbl_static STATIC
    src/mbl_core.c
    src/mbl_framebuffer.c
    src/mbl_gui.c
    src/mbl_sutf_gui.c
    src/mbl_loader.c
    src/sucs_mode.c
    src/sutf8.c
    src/sutf16.c
    src/vsutf.c
    src/extsutf_fixed.c
    src/esutf.c
)

target_include_directories(mbl_static PUBLIC include)

# Convenient Target Alias libmbl.a
add_custom_target(libmbl.a ALL
    DEPENDS mbl_static
)

# Custom Command: NASM Stage 1 Flat Binary (mbl_core.bin - 512 Bytes)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/mbl_core.bin
    COMMAND nasm -f bin ${CMAKE_CURRENT_SOURCE_DIR}/boot/stage1.asm -o ${CMAKE_CURRENT_BINARY_DIR}/mbl_core.bin
    DEPENDS boot/stage1.asm
    COMMENT "Assembling Stage 1 MBR Bootsector (mbl_core.bin)"
)

add_custom_target(mbl_core_bin ALL
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/mbl_core.bin
)

# Custom Command: NASM Stage 2 Flat Binary (mbl_stage2.sys)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/mbl_stage2.sys
    COMMAND nasm -f bin ${CMAKE_CURRENT_SOURCE_DIR}/boot/stage2.asm -o ${CMAKE_CURRENT_BINARY_DIR}/mbl_stage2.sys
    DEPENDS boot/stage2.asm
    COMMENT "Assembling Stage 2 VBE Loader (mbl_stage2.sys)"
)

add_custom_target(mbl_stage2_sys ALL
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/mbl_stage2.sys
)
