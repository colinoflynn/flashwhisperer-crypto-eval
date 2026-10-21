/* ADuCM3029: 256K flash @ 0x00000000, SRAM bank 0 @ 0x20000000.
 *
 * A PART PROPERTY, and the only linker file a port has to write. The
 * SECTIONS block is shared (instrumentation/victim/ld/sections.ld) and the
 * table input-section names belong to the library (eval/lib/<name>/tab_*.ld).
 *
 * FLASH IS BASED AT ZERO on this part, not at 0x08000000: it is an ADI
 * Cortex-M3, not an ST one. startup.c sets VTOR to 0 to match.
 *
 * WHY RAM STARTS AT 0x20000200 AND NOT AT 0x20000000
 * ---------------------------------------------------
 * The low 0x200 bytes of SRAM are reserved on this part and this port does
 * not use them. ADI's own GCC linker script for the ADuCM3029 (mbed-os
 * .../ADuCM3029/TOOLCHAIN_GCC_ARM/ADuCM3029.ld) keeps that block aside for
 * a RELOCATED VECTOR TABLE: the part's table is 104 words, which rounds up
 * to a 512-byte aligned region, and ADI reserves it at the base of SRAM so
 * an application that moves its vectors into RAM has somewhere aligned to
 * put them. This victim does not relocate its vectors, so it never writes
 * there, but the reservation is reproduced rather than reclaimed for two
 * reasons: rigs/fw_aducm_aes/link.ld in the research tree reserves exactly
 * the same 0x200 and the measurements this port has to be comparable with
 * were taken under that map, and the board cannot currently be recovered if
 * it stops booting, so 512 bytes is a cheap price for staying inside a
 * layout ADI itself ships. Do not round this down to 0x20000000.
 *
 * WHY 16K AND NOT MORE. The datasheet's SRAM-mode-0 map and ADI's linker
 * script each describe a larger contiguous region and they do NOT agree
 * with each other about it. 16 KB for bank 0 is the figure both do agree
 * on, so this stays inside the intersection.
 *
 * WHAT 16 KB DOES AND DOES NOT LEAVE ROOM FOR. The reference victim fits
 * with room to spare with its table twin resident in RAM: refsbox is 4 KB
 * of tables and links with over 11 KB of stack headroom. LibTomCrypt's
 * DEFAULT twin does not fit at all - twelve 1 KB tables leave 2 560 bytes
 * where sections.ld asserts 4 096 - and board.mk declares that cell in
 * BOARD_NO_CELLS with the measured numbers. So the assertion in sections.ld
 * is what decides this at link time, and 16 KB is not room to spare for
 * every library: it is room to spare for the ones with up to about 8 KB of
 * tables.
 */
ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 256K
  RAM  (rwx) : ORIGIN = 0x20000200, LENGTH = 16K - 0x200
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
