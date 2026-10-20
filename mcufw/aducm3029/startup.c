/* Minimal startup for ADuCM3029.
 *
 * NO HANDLER BEYOND THE FAULTS: nothing in a victim enables an interrupt,
 * and an ISR that could fire inside the measured region is exactly what an
 * external instrument would record as jitter.
 *
 * TWO COPY LOOPS, as on every other part here. `.data` as usual, and then
 * the AES table section, which the RAM variant places in RAM with its load
 * address still in flash. When the tables are left in flash the two
 * addresses are equal and the second loop does nothing, which is how one
 * startup serves both variants.
 */
#include <stdint.h>
#include "regs.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern uint32_t _tabload, _tabstart, _tabend;

extern int main(void);

void Reset_Handler(void)
{
    uint32_t *src = &_sidata, *dst = &_sdata;

    while (dst < &_edata) *dst++ = *src++;
    for (dst = &_sbss; dst < &_ebss; ) *dst++ = 0;

    if (&_tabload != &_tabstart) {
        src = &_tabload;
        for (dst = &_tabstart; dst < &_tabend; ) *dst++ = *src++;
    }

    SCB_VTOR = 0x00000000u;     /* flash is zero-based on this part */
    __asm volatile("dsb; isb");
    main();
    for (;;) {}
}

static void Default_Handler(void) { for (;;) {} }

/* THE VECTOR TABLE ON THIS PART IS NOT 16 ENTRIES, AND THE TAIL IS NOT
 * OPTIONAL.
 *
 * Every STM32 port in this tree gets away with a 16-entry Cortex-M table
 * because nothing reads past it. The ADuCM3029's BOOT KERNEL does. ADI's
 * own startup_ADuCM3029.h lays out 104 words, being the stack pointer, 15
 * system handlers and external interrupts 0..87, and parks a block the
 * kernel inspects at the very end:
 *
 *     (pFunc)BLANKX4, (pFunc)BLANKX4,            // security_options
 *     (pFunc)BLANKX4, (pFunc)BLANKX4,
 *     (pFunc)0xA79C3203u, (pFunc)LASTCRCPAGE,    // ext 84, ext 85
 *     (pFunc)BLANKX4, (pFunc)BLANKX4             // ext 86, 87
 *
 * External IRQ n lives at index 16+n, so 0xA79C3203 belongs at index 100,
 * byte offset 0x190, and LASTCRCPAGE (ADI defines it 0) at 0x194.
 *
 * A 16-entry table leaves 0xFF there, which is what the first build of the
 * research rig shipped, and the part did not run: the kernel stalled at
 * 0x40102 on a `b .` with VTOR still 0x00040000, no fault raised and the
 * application never entered. Writing the table out in full is the fix, and
 * all of the above is reproduced from rigs/fw_aducm_aes/main.c, which is
 * where it was paid for.
 *
 * WHY THE ARRAY IS 512 WORDS AND NOT 104. ADI's script carves the first
 * ADUCM_SECTOR_SIZE = 0x800 of flash into a region of its own holding the
 * vector table followed by a `.checksum` section their startup fills with
 * blanks, and the boot kernel reads that region. The research rig reserves
 * the same 0x800 in its linker script. This port cannot: the SECTIONS
 * block is shared by every part (instrumentation/victim/ld/sections.ld)
 * and a port is not allowed to edit it, so the reservation has to come out
 * of the one thing the port does own at the base of flash, which is the
 * length of this array. Padding it to 0x800 with 0xFFFFFFFF puts the first
 * table at 0x800 exactly as the rig does, and the padding words are the
 * erased value, so the programmed image is byte-identical to the rig's
 * `--gap-fill 0xFF` over the same range.
 *
 * It is a uint32_t array rather than an array of function pointers because
 * two of its entries are magic NUMBERS, not addresses, and casting those
 * through a function-pointer type to please a declaration would obscure
 * the only interesting thing about this table.
 */
#define VEC_BLANK 0xFFFFFFFFu
#define VEC_DFLT  ((uint32_t)(uintptr_t)Default_Handler)

__attribute__((section(".isr_vector"), used))
const uint32_t g_vectors[512] = {
    /* 0      */ (uint32_t)(uintptr_t)&_estack,
    /* 1  -15 */ (uint32_t)(uintptr_t)Reset_Handler,
    /* 2  -14 */ VEC_DFLT,            /* NMI         */
    /* 3  -13 */ VEC_DFLT,            /* HardFault   */
    /* 4  -12 */ VEC_DFLT,            /* MemManage   */
    /* 5  -11 */ VEC_DFLT,            /* BusFault    */
    /* 6  -10 */ VEC_DFLT,            /* UsageFault  */
    /* 7..10  */ 0, 0, 0, 0,
    /* 11 -5  */ VEC_DFLT,            /* SVC         */
    /* 12 -4  */ VEC_DFLT,            /* DebugMon    */
    /* 13     */ 0,
    /* 14 -2  */ VEC_DFLT,            /* PendSV      */
    /* 15 -1  */ VEC_DFLT,            /* SysTick     */

    /* external 0..63. This victim runs interrupts off throughout, so they
     * are all the same stub; they are emitted so the table is the right
     * LENGTH, which is the part that matters here. */
    [16 ... 79] = VEC_DFLT,

    /* external 64..79, where ADI emits zeros */
    [80 ... 95] = 0,

    /* external 80..87, the block the kernel reads */
    [96]  = VEC_BLANK, [97]  = VEC_BLANK,   /* security_options */
    [98]  = VEC_BLANK, [99]  = VEC_BLANK,
    [100] = 0xA79C3203u,                    /* kernel magic, offset 0x190 */
    [101] = 0,                              /* LASTCRCPAGE, offset 0x194  */
    [102] = VEC_BLANK, [103] = VEC_BLANK,

    /* the rest of the reserved 0x800 sector, left at the erased value */
    [104 ... 511] = VEC_BLANK,
};
