/* Raspberry Pi RP2350 memory map, for a Pico 2.
 *
 *     FLASH 0x10000000 4096K   the XIP window onto an EXTERNAL QSPI device
 *     RAM   0x20000000  512K   the striped SRAM
 *
 * "FLASH" HERE IS NOT FLASH. It is the cached, allocating XIP window onto a
 * serial device that is not on the die. The region keeps the name because
 * the shared sections.ld and the three linker fragments are written in terms
 * of FLASH and TABS, and because for this project's purposes it is the right
 * name: it is the memory the table is read FROM, down the path under test.
 * Every address this port reports for a table in the flash arm therefore
 * starts 0x10, and a reported 0x2000xxxx is SRAM exactly as on every other
 * part.
 *
 * GETTING THAT RIGHT MATTERS MORE ON THIS PART THAN ANYWHERE ELSE IN THE
 * TREE. The research tree's standard build on this part performs NO FLASH
 * ACCESS AT ALL during the S-box encryption, which is why the paper records
 * it as not broken rather than as safe (research/targets/rp2350.md section
 * 1). A port that can place a table in flash AND PROVE THE ADDRESS IS IN THE
 * CACHED XIP WINDOW is what would settle that. A port that builds but links
 * the table somewhere the flash path never reads would answer the question
 * with a null and look like a result.
 *
 * WHICH WINDOW, THEN, SINCE THERE ARE FOUR AND THEY ARE NOT THE RP2040'S.
 * 0x10000000 cached and allocating (this one), 0x14000000 uncached,
 * 0x18000000 cache maintenance, 0x1C000000 uncached and untranslated. The
 * RP2040's 0x11/0x12/0x13 per-access aliases do not exist here and are NOT
 * unmapped: they fall inside this 64 MB cached window and alias flash at a
 * different offset, so a table linked at 0x11000000 would read correct bytes
 * from the wrong place and measure nothing.
 *
 * THE LENGTH IS THE BOARD'S FITTED DEVICE, NOT A PROPERTY OF THE DIE. A Pico
 * 2 carries 4 MB; the window is 64 MB wide and the die does not know what is
 * on the other end of it. The linker only needs the number as a bound.
 *
 * WHICH SRAM, AND WHY ONLY THE FIRST 512 KB. The part has 520 KB: eight
 * banks striped together as one 512 KB region at 0x20000000, plus two
 * unstriped 4 KB banks at 0x20080000 and 0x20081000. This map uses the
 * striped region only, for the sake of the RAM twin - the twin is a control
 * only if the table reaches RAM by the same path ordinary data takes, and
 * the unstriped banks are a different one. It is the same argument that
 * keeps the F429's CCM out of its map.
 *
 * THIS SCRIPT CARRIES A SECTIONS BLOCK, as mcufw/k21fn1m0/memory.ld and
 * mcufw/rp2040/memory.ld do, and for the same reason: things this part needs
 * have nowhere else to live, because instrumentation/victim/ld/sections.ld is
 * shared by every part. `ld` merges SECTIONS blocks in include order and this
 * file is included first, so everything here is placed before sections.ld
 * places anything.
 *
 *   1. THE VECTOR TABLE AT 0x10000000 EXACTLY, AND THE IMAGE_DEF BLOCK
 *      IMMEDIATELY AFTER IT. This part's bootrom does NOT look for an
 *      RP2040-style checksummed boot2 at offset 0 - there is no boot2 in
 *      this image at all. It searches the first 4 KB for a block loop and
 *      boots the IMAGE_DEF it finds, and since that block carries no
 *      ENTRY_POINT item the bootrom takes the initial SP and PC from the
 *      first two words of the image. So the table has to be first and the
 *      block has to be inside the first 4 KB, and both are pinned here by
 *      address and by input-section order rather than left to whichever
 *      object the linker reaches first. See startup.c and regs.h.
 *
 *      The table answers to `.vectors` rather than `.isr_vector` for the
 *      same reason k21fn1m0's does: sections.ld opens with an `.isr_vector`
 *      output section, and with the name changed that section collects
 *      nothing and this one owns the address.
 *
 *   2. A RAM-RESIDENT CODE SECTION. ssb_acr_set writes XIP_CTRL and
 *      QMI_M0_TIMING and sweeps the cache maintenance window, and all three
 *      are operations on the path the core is fetching instructions through.
 *      On the RP2040 the research tree measured that writing that register
 *      file from XIP-resident code faults into lockup; on this part the SDK
 *      makes every maintenance helper RAM-resident and the rig follows it.
 *      Nothing has been run here, so this port takes the cautious side of a
 *      question it cannot answer.
 */
ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x10000000, LENGTH = 4096K
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 512K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
  /* Two input sections, in this order, in one output section: the vector
     table at the image start and the IMAGE_DEF block right behind it. The
     order inside an output section is the order the wildcards are written
     in, which is why this is not left to two separate sections or to
     declaration order inside a translation unit. */
  .vectors 0x10000000 : {
    KEEP(*(.vectors))
    . = ALIGN(4);
    _image_def = .;
    KEEP(*(.image_def))
  } > FLASH

  /* Code that reconfigures the XIP cache and the QMI, copied to SRAM by
     Reset_Handler. Placed here rather than in sections.ld because only the
     two RP parts need it, and ahead of .text so that the `*(.text*)`
     wildcard there cannot claim it first.

     THE TRAILING ALIGN(1024) IS LOAD-BEARING, and the reason is k21fn1m0's,
     written out there: the shared sections.ld opens .aestab with
     `. = ALIGN(1024)` INSIDE the output section, after `_tabload =
     LOADADDR(.aestab)` has been taken from the section's start. With
     anything in RAM ahead of .aestab that does not end on a 1 KB boundary,
     the alignment becomes padding at the FRONT of the section, _tabload
     points at the padding, and the RAM arm's copy loop fills the tables with
     it. The S-box comes out zero, every ciphertext is wrong, and it reads as
     a broken cipher rather than as a linker script. */
  .ramfunc : {
    . = ALIGN(4);
    _sramfunc = .;
    *(.ramfunc*)
    . = ALIGN(4);
    _eramfunc = .;
    . = ALIGN(1024);
  } > RAM AT > FLASH
  _ramfuncload = LOADADDR(.ramfunc);

  ASSERT(ADDR(.vectors) == 0x10000000,
         "rp2350: the vector table must be the first thing in the image - the bootrom takes SP and PC from the first two words")
  ASSERT(SIZEOF(.vectors) >= 0x54,
         "rp2350: the IMAGE_DEF block is missing - without it the bootrom finds no image and the part comes up in USB boot")
  ASSERT(_image_def + 20 <= 0x10001000,
         "rp2350: the IMAGE_DEF block must lie inside the first 4 KB of the image")
}
