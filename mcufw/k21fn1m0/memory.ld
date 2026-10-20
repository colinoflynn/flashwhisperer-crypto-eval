/* NXP Kinetis MK21FN1M0VMC12 memory map.
 *
 * Taken from rigs/fw_k21_aes/link.ld in the flash-accelerator-tests tree,
 * which is the register and layout source of truth for this part:
 *
 *     FLASH 0x00000000 1024K
 *     RAM   0x20000000 64K
 *
 * WHICH SRAM, AND WHY THIS HALF. The part has 128 KB of SRAM in two equally
 * sized logical arrays: SRAM_L growing down from 0x1FFFFFFF and SRAM_U
 * growing up from 0x20000000, contiguous in the address map. This script
 * uses SRAM_U only, 0x20000000..0x2000FFFF, which is the rig's choice and
 * is made for the sake of the RAM twin: a buffer that straddles the L/U
 * boundary is served by two different sides of the SRAM controller, so a
 * table placed across it would carry a placement artifact into the one
 * control this project requires to be bit-exactly constant. Spanning both
 * halves would give 128 KB and a twin nobody could defend. 64 KB is ample
 * for a byte S-box or four T-tables plus stack.
 *
 * THIS SCRIPT CARRIES A SECTIONS BLOCK, which the other parts here do not.
 * Two things this part needs have nowhere else to live, because
 * instrumentation/victim/ld/sections.ld is shared by every part and is not
 * a per-part file:
 *
 *   1. The flash configuration field at 0x400. See below; getting it wrong
 *      secures the part.
 *   2. A RAM-resident code section. RM S28.4 requires that the FMC's
 *      registers be programmed while the flash controller is idle, "for
 *      example, execute from RAM", because changing them under an
 *      in-flight flash access is documented as non-deterministic. The rig
 *      links its fmc_apply() into .ramfunc for exactly this reason and so
 *      does board.c.
 *
 * ld merges SECTIONS blocks in the order the fragments are included, and
 * this file is included first, so everything here is placed before
 * sections.ld gets to place anything. That is why the vector table is
 * emitted into an input section called `.vectors` rather than
 * `.isr_vector`: sections.ld opens with an `.isr_vector` output section at
 * the base of FLASH, and if the vectors answered to that name they would
 * be placed after the flash configuration field instead of at zero. With
 * the name changed, sections.ld's output section collects nothing and is
 * empty, and the vectors are pinned here at 0 by address.
 *
 * THE FLASH CONFIGURATION FIELD AT 0x400 IS NOT OPTIONAL ON A KINETIS, and
 * this is the part of the map that bricks a board rather than merely
 * mismeasuring one. The FTFE loads FSEC out of flash at 0x40C on every
 * reset. Erased flash reads 0xFF, and FSEC = 0xFF decodes as SEC = 0b11 =
 * SECURED, so an image that simply omits these sixteen bytes re-secures
 * the part at every boot: the debugger is locked out and recovery costs a
 * mass erase. That is not hypothetical on this bench, it has already
 * happened once (research/targets/k21fn1m0.md section 2f). startup.c emits
 * the field, this script pins it by absolute address rather than by
 * ordering so that a change in vector table size cannot slide it, and the
 * ASSERT below fails the link if it is missing.
 */
ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 1024K
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
  .vectors 0x00000000 : { KEEP(*(.vectors)) } > FLASH

  .flashconfig 0x00000400 : { KEEP(*(.flashconfig)) } > FLASH

  /* Code that reconfigures the FMC, copied to SRAM by Reset_Handler. Placed
     here rather than in sections.ld because only this part needs it, and
     ahead of .text so that the `*(.text*)` wildcard there cannot claim it
     first.

     THE TRAILING ALIGN(1024) IS LOAD-BEARING AND COSTS ABOUT 1 KB OF FLASH.
     This is the only part here with anything in RAM ahead of .aestab, and
     the shared sections.ld opens .aestab with `. = ALIGN(1024)` INSIDE the
     output section, after `_tabload = LOADADDR(.aestab)` has already been
     taken from the section's start. So if the RAM pointer is not already on
     a 1 KB boundary when .aestab opens, the alignment becomes padding at
     the FRONT of the section, `_tabload` points at that padding rather than
     at the first table, and the RAM variant's copy loop fills the tables
     with it. The S-box comes out zero, every ciphertext is wrong, and the
     fault looks like a broken cipher rather than a linker script. Leaving
     the pointer aligned here makes the alignment a no-op and the load
     address the table address. It is after `_eramfunc`, so the startup copy
     does not copy the padding. */
  .ramfunc : {
    . = ALIGN(4);
    _sramfunc = .;
    *(.ramfunc*)
    . = ALIGN(4);
    _eramfunc = .;
    . = ALIGN(1024);
  } > RAM AT > FLASH
  _ramfuncload = LOADADDR(.ramfunc);

  ASSERT(SIZEOF(.flashconfig) == 16,
         "k21fn1m0: flash configuration field missing or not 16 bytes - the part would boot SECURED")
}
