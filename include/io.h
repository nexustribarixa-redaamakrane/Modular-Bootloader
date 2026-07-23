#ifndef MBL_IO_H
#define MBL_IO_H

#include <stdint.h>

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outw(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline void cli(void) {
    __asm__ volatile ("cli");
}

static inline void sti(void) {
    __asm__ volatile ("sti");
}

static inline void io_wait(void) {
    outb(0x80, 0x00);
}

static inline void hlt_loop(void) {
    while (1) {
        __asm__ volatile ("cli; hlt");
    }
}

static inline void mbl_poweroff(void) {
    /* QEMU / Bochs / VirtualBox ACPI poweroff port 0x604 */
    outw(0x604, 0x2000);
    /* Older QEMU PM1a_CNT port 0xB004 */
    outw(0xB004, 0x2000);
    /* Cloud Hypervisor port 0x600 */
    outw(0x600, 0x34);
    /* Halt loop fallback */
    hlt_loop();
}

#endif /* MBL_IO_H */
