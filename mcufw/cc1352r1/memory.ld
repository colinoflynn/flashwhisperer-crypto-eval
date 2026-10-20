/* CC1352R1F3 (LAUNCHXL-CC1352R1): flash based at 0x00000000, 80K SRAM at
 * 0x20000000. Taken from the rig that ran on this board,
 * rigs/fw_cc1352_aes/link.ld in the research tree.
 *
 * FLASH IS DECLARED 0x50000 AND NOT THE FULL 352K, DELIBERATELY. The last
 * sector carries CCFG, and on that die CCFG_TAP_DAP_0.CPU_DAP_ENABLE is
 * what makes cJTAG debug work while the ROM serial bootloader is disabled.
 * Overwriting it costs the board with no recovery path. Declaring the
 * region short means the image physically cannot reach that sector
 * whatever the linker is asked to do, which is a stronger guarantee than
 * remembering not to. This image also carries no CCFG of its own: the ROM
 * branches to the vector table at address 0 because the CCFG already on
 * the part says so.
 *
 * THE TWIN GOES IN THE ORDINARY SRAM AT 0x20000000, NOT IN GPRAM. This
 * part has 8 KB of memory at 0x11000000 when VIMS_CTL.MODE = GPRAM, and
 * it is tempting as a table home because it is fast. It is the wrong
 * place for this measurement twice over. It is the SAME PHYSICAL ARRAY as
 * the VIMS cache, so a table living there is a table living in the
 * accelerator being measured rather than outside it. And it EXISTS ONLY
 * IN ONE MODE: the moment the accelerator word selects CACHE or OFF, the
 * region the tables were linked into is not addressable. A control has to
 * be the same control in every cell of the sweep, and that one would
 * change shape with the register under test. The 80K at 0x20000000 is
 * ordinary SRAM, is there in every VIMS mode, and does not overlap the
 * GPRAM window.
 */
ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 0x50000
  RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 80K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
