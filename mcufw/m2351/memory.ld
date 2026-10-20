/* Nuvoton M2351 (M2351KIAAE): 512K APROM at 0x00000000, 96K SRAM at
 * 0x20000000.
 *
 * A PART PROPERTY, and the only linker file a port has to write. The SECTIONS
 * block is shared (instrumentation/victim/ld/sections.ld) and the table input
 * section names belong to the library (eval/lib/<name>/tab_[tsd].ld).
 *
 * THE IMAGE STARTS AT THE BASE OF FLASH and there is nothing in the way. The
 * part's user configuration words, CONFIG0 and CONFIG1, live in a separate
 * ISP address space at 0x00300000 and are not reachable through the APROM
 * range at all, so unlike the XMC1100 and the RA2A1 there is no configuration
 * block or option-setting memory for this image to step on or to re-emit.
 *
 * THESE ARE THE SECURE ADDRESSES. The part boots Secure with the SAU
 * disabled and this victim, like rigs/fw_m2351_aes/, is a single secure image
 * that never configures the SAU or the SCU. The non-secure views at
 * 0x10000000 and 0x30000000 are not used, and on this part they are not
 * aliases of the same storage anyway: research/targets/m2351.md section 5b
 * measured the flash to be a partition, each physical address answering at
 * exactly one of the two.
 *
 * 512K is the datasheet figure for this part number, reproduced from the
 * rig's link script, and it is NOT measured. Reads past the end of flash
 * return 0xFFFFFFFF on this die rather than faulting, so the extent cannot be
 * read off it non-destructively. The victim is a few tens of KB, so the
 * declared length is slack rather than a claim.
 *
 * THE RAM REGION IS A VALID TWIN, which is the only question that matters
 * about it here, because RAMTAB=1 aliases TABS to RAM and the whole residence
 * control rests on the two arms differing in the table's memory and in
 * nothing else. Three things make it one on this part:
 *
 *   There is one SRAM. 96K at 0x20000000, no second bank, no TCM, no
 *   cacheable and non-cacheable alias of the same array, so there is no way
 *   to pick the wrong RAM here - which is not true of the two M7s on this
 *   bench, where the twin has to be DTCM or it is measuring the L1.
 *
 *   The structure that would spoil it is on the flash side only. The 4 KB
 *   direct-mapped cache this part holds is in the flash read path; a
 *   Cortex-M23 has no L1 of its own, so a table moved into SRAM is not read
 *   through any cache at all.
 *
 *   It has been measured, not argued. research/targets/m2351.md section 3
 *   normalises every geometry number against an SRAM twin in this region,
 *   and the AES matrix has the tables in SRAM coming out bit-exactly constant
 *   and 1.07 to 1.13 times FASTER than any flash configuration. A twin that
 *   is both flat and faster is the strongest form of the control: there is no
 *   residual timing structure for it to be hiding.
 *
 * Headroom: the victim's tables are at most about 4.25K and .bss is small
 * next to 96K, so the 4K stack assertion in sections.ld has room. That
 * assertion is the check, not this comment.
 */
ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 512K
  RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 96K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);

/* The base of RAM as a symbol, which startup.c needs to tell the two
 * residence arms apart. It is here because this is the file that knows where
 * RAM is; startup.c says what it does with it and why the usual test does
 * not work. */
_sram_start = ORIGIN(RAM);
