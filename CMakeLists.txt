cmake_minimum_required(VERSION 3.10)
project(mbl C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Freestanding C99 flags (Zero stdlib dependencies)
add_compile_options(-m64 -nostdlib -ffreestanding -fno-stack-protector -fno-pic -fno-builtin -mno-red-zone -mno-sse -mno-80387 -mcmodel=large -Wall -Wextra)

# Static Library Target: libmbl.a
add_library(mbl_static STATIC
    src/efi_entry.c
    src/gop.c
    src/kbd.c
    src/bios_disk.c
    src/menu.c
    src/owfs.c
    src/main.c
    src/sutf/sutf8.c
    src/sutf/sucs_mode.c
)

target_include_directories(mbl_static PUBLIC include include/sutf)

# Convenience Target Alias
add_custom_target(libmbl.a ALL
    DEPENDS mbl_static
)

# Python3-based helper for the disk image builder
find_package(Python3 COMPONENTS Interpreter)

if(Python3_EXECUTABLE)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/mbl_test.img
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_image.py
        DEPENDS
            tools/build_efi.py
            tools/build_image.py
            tools/owfs_mkfs.py
            boot/test_kernel.asm
        COMMENT "Building bootable disk image (mbl_test.img)"
    )

    add_custom_target(mbl_disk_img ALL
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/mbl_test.img
    )
endif()
