/* The section layout, SHARED BY EVERY PART AND EVERY LIBRARY.
 *
 * Three files make one linker script, split along what each half owns:
 *
 *   mcufw/<part>/memory.ld      MEMORY, ENTRY, _estack   - a PART property
 *   link_flash.ld / link_ram.ld one REGION_ALIAS line    - the axis
 *   this file                   the SECTIONS block       - shared
 *   eval/lib/<name>/tab_[tsd].ld  which input sections hold the tables
 *                                                        - a LIBRARY property
 *
 * REGION_ALIAS MUST COME AFTER `MEMORY` AND BEFORE THE SECTIONS THAT USE THE
 * ALIAS, which is why the first three are three files rather than two. `ld`
 * rejects the other order with "memory region `FLASH' for alias `TABS' does
 * not exist".
 *
 * The tables get collected BY SECTION NAME. Most libraries' AES tables are
 * file-static and at least one ships inside a prebuilt archive, so none of
 * them can be moved, or even addressed, from C. Compiled with
 * -fdata-sections, each table gets its own input section named after its
 * symbol, and collecting those gives one residence axis that works
 * identically for a source library and a binary one, moving ONLY the data:
 * same code, same instruction stream, same indices.
 *
 * -fdata-sections IS LOAD-BEARING, NOT A SIZE OPTIMISATION. A library added
 * to libs/ that is not compiled that way does not get this axis at all, and
 * a prebuilt archive that was not built that way cannot be given it.
 *
 * NO KEEP() ON THE TABLE SECTIONS, DELIBERATELY. An encrypt-only build must
 * be allowed to garbage-collect the decryption tables; KEEP would drag them
 * into every image and, with the RAM variant, into RAM. An empty slot is
 * reported as address 0 by SS_V_LIBINFO, which is the truthful answer.
 *
 * ALIGNMENTS: T-tables on a 1 KB boundary, then the S-box on 256. A 256-byte
 * table at offset 0 occupies a whole number of flash read words on every
 * part measured so far, which is the BEST case for the defender and the one
 * worth quoting as a floor.
 */
SECTIONS
{
  .isr_vector : { KEEP(*(.isr_vector)) } > FLASH

  /* Give both residency arms the same 1 KB-aligned flash load address.
     Align TABS separately because some RAM regions (and K21's preceding
     .ramfunc) do not end on a 1 KB boundary. Neither pad contains tables. */
  .tab_flash_align : { . = ALIGN(1024); } > FLASH
  .tab_vma_align (NOLOAD) : { . = ALIGN(1024); } > TABS

  .aestab :
  {
    . = ALIGN(1024);
    _tabstart = .;

    /* The word T-tables, 1 KB each. */
    _tabT = .;
    INCLUDE tab_t.ld
    . = ALIGN(4);
    _tabT_end = .;

    /* The byte S-box. */
    . = ALIGN(256);
    _tabS = .;
    INCLUDE tab_s.ld
    . = ALIGN(4);
    _tabS_end = .;

    /* Decryption tables. Present only if something references them, which
       an encrypt-only build does not - so normally empty, reported as 0. */
    _tabD = .;
    INCLUDE tab_d.ld
    . = ALIGN(4);
    _tabD_end = .;

    _tabend = .;
  } > TABS AT > FLASH
  /* The load address of _tabstart, NOT of the section. ALIGN(1024) is
     inside .aestab, so a future unaligned TABS origin could put padding
     before the first table byte. Before the pre-alignment sections above,
     LOADADDR(.aestab) copied the wrong bytes on xmc1100, aducm3029 and
     k21fn1m0; the RAM twin came up holding padding. This expression also
     makes the flash-arm load and run addresses equal, so startup does not
     try to copy flash bytes over its executing code. */
  _tabload = LOADADDR(.aestab) + (_tabstart - ADDR(.aestab));

  .text :
  {
    . = . + CODEPAD;
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
  } > FLASH

  _sidata = LOADADDR(.data);
  .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT > FLASH
  .bss  : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM

  ASSERT(_ebss + 4096 <= _estack,
         "less than 4 KB of stack headroom above .bss")

  /DISCARD/ : { *(.ARM.exidx*) *(.comment) *(.ARM.attributes) }
}
