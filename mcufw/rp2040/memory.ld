/* Raspberry Pi RP2040 memory map, for a Pico.
 *
 *     FLASH 0x10000000 2048K   the XIP window onto an EXTERNAL QSPI device
 *     RAM   0x20000000  256K   the striped SRAM
 *
 * "FLASH" HERE IS NOT FLASH. It is the cached, allocating XIP window onto a
 * serial device that is not on the die. The region keeps the name because
 * the shared sections.ld and the three linker fragments are written in terms
 * of FLASH and TABS, and because for this project's purposes it is the right
 * name: it is the memory the table is read FROM, down the path under test.
 * Every address this port reports for a table in the flash arm therefore
 * starts 0x10, not 0x08 and not 0x00, and a reported 0x2000xxxx is SRAM
 * exactly as on every other part. The address is the check that the table is
 * where the arm says it is, so it is worth saying which window is which:
 * 0x10 cached+allocating (this one), 0x11 cached non-allocating, 0x12
 * non-cached allocating, 0x13 full bypass. A table linked into 0x11..0x13
 * would read correct bytes down a path the flash arm is not supposed to be
 * measuring.
 *
 * THE LENGTH IS THE BOARD'S FITTED DEVICE, NOT A PROPERTY OF THE DIE. A Pico
 * carries 2 MB; the XIP window is 16 MB wide and the die does not know what
 * is on the other end of it. The linker only needs the number as a bound.
 *
 * WHICH SRAM, AND WHY ONLY THE FIRST 256 KB. The part has 264 KB: six banks
 * striped together as one 256 KB region at 0x20000000, plus two unstriped
 * 4 KB banks at 0x20040000 and 0x20041000 (the SDK's scratch_x/scratch_y).
 * This map uses the striped region only, for the sake of the RAM twin - the
 * twin is a control only if the table reaches RAM by the same path ordinary
 * data takes, and the unstriped banks are a different one. It is the same
 * argument that keeps the F429's CCM out of its map. The bootrom also drops
 * its copy of boot2 at 0x20041F00 to run it, so leaving those two banks out
 * means the stack cannot be asked whether it minds sharing with it.
 *
 * THIS SCRIPT CARRIES A SECTIONS BLOCK, as mcufw/k21fn1m0/memory.ld does and
 * for the same reason: two things this part needs have nowhere else to live,
 * because instrumentation/victim/ld/sections.ld is shared by every part.
 * `ld` merges SECTIONS blocks in include order and this file is included
 * first, so everything here is placed before sections.ld places anything.
 *
 *   1. BOOT2, AT 0x10000000, 256 BYTES, AND WITHOUT IT THERE IS NO XIP AT
 *      ALL. The bootrom copies the first 256 bytes of flash to SRAM, checks
 *      that the last word is a CRC-32/MPEG-2 over the first 252, runs it,
 *      and expects it to configure the QSPI link and jump to 0x10000100. A
 *      checksum that does not match sends the part to USB bootloader mode
 *      instead, which from the host looks like a board that never says
 *      hello. See mcufw/rp2040/boot2_w25q080.c for where the bytes come
 *      from and why they are carried as bytes.
 *
 *   2. A RAM-RESIDENT CODE SECTION. XIP_CTRL cannot be written from code
 *      that is executing out of the window the cache serves. The research
 *      tree's rig established this the expensive way: writing XIP_FLUSH from
 *      XIP-resident code arrives at pc=0xFFFFFFFE in a HardFault that
 *      faulted again, while the identical write from a halted debugger
 *      completes cleanly. CTRL specifically was left an open question there
 *      and made moot by doing it from SRAM, which demonstrably works, and
 *      this port does the same. board.c puts ssb_acr_set in .ramfunc.
 *
 * THE VECTOR TABLE IS PINNED BY ADDRESS HERE, at 0x10000100, and it answers
 * to `.vectors` rather than `.isr_vector` for the same reason k21fn1m0 does:
 * sections.ld opens with an `.isr_vector` output section at the base of
 * FLASH, and a vector table answering to that name would be placed wherever
 * the FLASH pointer had got to after boot2 and after .ramfunc's load
 * address - not at 0x10000100, which is the address boot2 jumps to and which
 * is not negotiable. With the name changed, sections.ld's output section
 * collects nothing, and the table is where the bootrom's loader puts the pc.
 */
ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x10000000, LENGTH = 2048K
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 256K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
  .boot2   0x10000000 : { KEEP(*(.boot2)) }   > FLASH
  .vectors 0x10000100 : { KEEP(*(.vectors)) } > FLASH

  /* Code that reconfigures the XIP cache, copied to SRAM by Reset_Handler.
     Placed here rather than in sections.ld because only the two RP parts
     need it, and ahead of .text so that the `*(.text*)` wildcard there
     cannot claim it first.

     THE TRAILING ALIGN(1024) IS LOAD-BEARING, and the reason is k21fn1m0's,
     written out there: the shared sections.ld opens .aestab with
     `. = ALIGN(1024)` INSIDE the output section, after `_tabload =
     LOADADDR(.aestab)` has been taken from the section's start. With
     anything in RAM ahead of .aestab that does not end on a 1 KB boundary,
     the alignment becomes padding at the FRONT of the section, _tabload
     points at the padding, and the RAM arm's copy loop fills the tables with
     it. The S-box comes out zero, every ciphertext is wrong, and it reads as
     a broken cipher rather than as a linker script. It is after _eramfunc,
     so the startup copy does not copy the padding. */
  .ramfunc : {
    . = ALIGN(4);
    _sramfunc = .;
    *(.ramfunc*)
    . = ALIGN(4);
    _eramfunc = .;
    . = ALIGN(1024);
  } > RAM AT > FLASH
  _ramfuncload = LOADADDR(.ramfunc);

  ASSERT(SIZEOF(.boot2) == 256,
         "rp2040: boot2 missing or not 256 bytes - the bootrom would reject it and the part would come up in USB boot instead of running this image")
  ASSERT(ADDR(.vectors) == 0x10000100,
         "rp2040: the vector table must sit immediately after boot2, which is where boot2 sets the pc from")
}
