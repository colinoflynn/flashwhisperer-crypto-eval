/* nRF52840: 1M flash at 0x00000000, 256K SRAM at 0x20000000.
 *
 * NOTE THE FLASH BASE. It is 0, not 0x08000000. Every other part in this
 * tree so far is an ST one and sits at 0x08000000, so a map copied from a
 * neighbour and edited for size would link here, flash here, and fetch its
 * reset vector from the wrong place. Both numbers come from
 * rigs/fw_nrf_aes/link.ld in the research tree, which is the version built
 * and run on this silicon.
 *
 * WHY THIS RAM IS A VALID TWIN, AND WHY THE CHOICE IS EASY HERE. The twin
 * is only a control if the table reaches RAM by the same path ordinary data
 * takes, so that the one thing differing between the two arms is the
 * table's residence. On the ST parts that took some care: the F429 has to
 * leave the 64K CCM out because CCM is not on the same bus path, and the
 * two M7s have to pick DTCM precisely BECAUSE the larger SRAM sits behind
 * the L1 data cache and a table moved there would still be read through a
 * cache.
 *
 * This part has none of those. There is one SRAM, 256K contiguous at
 * 0x20000000, and it is the RAM the stack, .data and .bss already live in.
 * There is no TCM, no second RAM region at another address, no cacheable
 * alias of the same memory, and no data cache anywhere on the part for a
 * relocated table to fall behind - the only cache is the 2 KB instruction
 * cache on the ICODE bus. So there is no alternative region to choose
 * wrongly and no way for the twin to pick up a different access path on
 * the way.
 *
 * The research tree has measured the result: tables in RAM on this part
 * come out bit-exact (research/reference/DATASHEETS.md's nRF52840 row).
 * That is the twin behaving as a twin should.
 */
ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 1024K
  RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 256K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
