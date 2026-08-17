cmake_minimum_required(VERSION 3.10)
project(mbl C ASM_NASM)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Freestanding C99 flags (Zero stdlib dependencies)
add_compile_options(-m32 -nostdlib -ffreestanding -fno-stack-protector -fno-pic -fno-builtin -mno-sse -mno-80387 -Wall -Wextra)

# Static Library Target: libmbl.a
add_library(mbl_static STATIC
    src/main.c
    src/vga.c
    src/kbd.c
    src/menu.c
    src/owfs.c
    src/bios_disk.c
    src/sutf/sutf8.c
    src/sutf/sucs_mode.c
)

target_include_directories(mbl_static PUBLIC include include/sutf)

# Convenient Target Alias libmbl.a
add_custom_target(libmbl.a ALL
    DEPENDS mbl_static
)

# Custom Command: NASM Stage 1 Flat Binary (stage1.bin - 512 Bytes)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/stage1.bin
    COMMAND nasm -f bin ${CMAKE_CURRENT_SOURCE_DIR}/boot/stage1.asm -o ${CMAKE_CURRENT_BINARY_DIR}/stage1.bin
    DEPENDS boot/stage1.asm
    COMMENT "Assembling Stage 1 MBR Bootsector (stage1.bin)"
)

add_custom_target(stage1_bin ALL
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/stage1.bin
)

# Custom Command: NASM Stage 2 Flat Binary (stage2.bin)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/stage2.bin
    COMMAND nasm -f bin ${CMAKE_CURRENT_SOURCE_DIR}/boot/stage2.asm -o ${CMAKE_CURRENT_BINARY_DIR}/stage2.bin
    DEPENDS boot/stage2.asm
    COMMENT "Assembling Stage 2 Loader (stage2.bin)"
)

add_custom_target(stage2_sys ALL
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/stage2.bin
)

# Python3-based helper for the disk image builder
find_package(Python3 COMPONENTS Interpreter)

if(Python3_EXECUTABLE)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/mbl_test.img
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_image.py
        DEPENDS
            tools/build_boot.py
            tools/build_image.py
            tools/gcc_intel_to_nasm.py
            tools/owfs_mkfs.py
            boot/stage1.asm
            boot/stage2.asm
            boot/bootimg.asm
            boot/test_kernel.asm
        COMMENT "Building bootable disk image (mbl_test.img)"
    )

    add_custom_target(mbl_disk_img ALL
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/mbl_test.img
    )
endif()
