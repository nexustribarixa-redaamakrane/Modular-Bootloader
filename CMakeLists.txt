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

# Optional Python3-based helpers (font extraction + disk image builder)
find_package(Python3 COMPONENTS Interpreter)

# Font table for stage3, extracted from the embedded 8x16 font in mbl_sutf_gui.c
if(Python3_EXECUTABLE)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/boot/font8x16.inc
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/extract_font.py
                ${CMAKE_CURRENT_SOURCE_DIR}/src/mbl_sutf_gui.c
                ${CMAKE_CURRENT_SOURCE_DIR}/boot/font8x16.inc
        DEPENDS tools/extract_font.py src/mbl_sutf_gui.c
        COMMENT "Generating boot/font8x16.inc"
    )
endif()

# Custom Command: NASM Stage 3 Flat Binary (mbl_stage3.bin - boot GUI)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/mbl_stage3.bin
    COMMAND nasm -f bin -I ${CMAKE_CURRENT_SOURCE_DIR}/boot ${CMAKE_CURRENT_SOURCE_DIR}/boot/stage3.asm -o ${CMAKE_CURRENT_BINARY_DIR}/mbl_stage3.bin
    DEPENDS boot/stage3.asm ${CMAKE_CURRENT_SOURCE_DIR}/boot/font8x16.inc
    COMMENT "Assembling Stage 3 Boot GUI (mbl_stage3.bin)"
)

add_custom_target(mbl_stage3_bin ALL
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/mbl_stage3.bin
)

# Bootable disk image: packs stage1/stage2/stage3/kernel at the LBAs the
# stage2 loader expects (see tools/build_image.py and boot/stage2.asm).
if(Python3_EXECUTABLE)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/mbl_disk.img
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_image.py
                --stage1 ${CMAKE_CURRENT_BINARY_DIR}/mbl_core.bin
                --stage2 ${CMAKE_CURRENT_BINARY_DIR}/mbl_stage2.sys
                --stage3 ${CMAKE_CURRENT_BINARY_DIR}/mbl_stage3.bin
                --out ${CMAKE_CURRENT_BINARY_DIR}/mbl_disk.img
        DEPENDS
            ${CMAKE_CURRENT_BINARY_DIR}/mbl_core.bin
            ${CMAKE_CURRENT_BINARY_DIR}/mbl_stage2.sys
            ${CMAKE_CURRENT_BINARY_DIR}/mbl_stage3.bin
            tools/build_image.py
            boot/test_kernel.asm
        COMMENT "Building bootable disk image (mbl_disk.img)"
    )

    add_custom_target(mbl_disk_img ALL
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/mbl_disk.img
    )
endif()
