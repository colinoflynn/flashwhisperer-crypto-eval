/* Minimal startup for the RP2040.
 *
 * NO HANDLER BEYOND THE FAULTS: nothing in a victim enables an interrupt,
 * and an ISR that could fire inside the measured region is exactly what an
 * external instrument would record as jitter.
 *
 * THREE COPY LOOPS, where every other part here has two. `.data` as usual,
 * then the RAM-resident code, then the AES table section.
 *
 * THE RAM-RESIDENT COPY IS FIRST AND IS NOT OPTIONAL. ssb_acr_set writes
 * XIP_CTRL, and the research tree measured that writing that register file
 * from code executing out of the XIP window faults into lockup. So it lives
 * in `.ramfunc`, and nothing may call it before this loop has run. It does,
 * via ssb_init, so the order here matters: ramfunc before main, always.
 *
 * THE TABLE COPY IS GUARDED ON ITS DESTINATION BEING RAM, which is what the
 * nRF52840 startup beside this one does and for the same reason. The ST
 * startups test `_tabload != _tabstart` and assume the two are equal in the
 * flash arm; a store into the destination when they are not is harmless on
 * an ST part because a store to locked flash is discarded there, and they
 * have the silicon runs to prove it. THIS PART'S "FLASH" IS AN XIP WINDOW,
 * and a store into it is not discarded - it is a cache maintenance
 * operation. The pico-sdk invalidates a cache line by writing to its normal
 * cached address, which is precisely what an unguarded copy loop would do,
 * 4 KB of it, before main. Asking the question the loop actually means -
 * did the tables move to RAM - costs one compare and removes that entirely.
 *
 * VTOR IS SET TO 0x10000100, NOT TO 0x08000000 AND NOT TO 0. This part's
 * flash is an XIP window at 0x10000000 and its first 256 bytes are boot2, so
 * the vector table starts one page in. That is also the address boot2 itself
 * sets the pc from, so the two have to agree; memory.ld pins the table there
 * by address and asserts it.
 */
#include <stdint.h>
#include "regs.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern uint32_t _ramfuncload, _sramfunc, _eramfunc;
extern uint32_t _tabload, _tabstart, _tabend;

extern int main(void);

/* The base of the striped SRAM, from mcufw/rp2040/memory.ld. A destination
 * at or above this is in RAM; anything below it is the XIP window. */
#define RAM_ORIGIN 0x20000000u

void Reset_Handler(void)
{
    uint32_t *src, *dst;

    src = &_sidata; dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;

    for (dst = &_sbss; dst < &_ebss; ) *dst++ = 0;

    src = &_ramfuncload;
    for (dst = &_sramfunc; dst < &_eramfunc; ) *dst++ = *src++;

    if ((uintptr_t)&_tabstart >= RAM_ORIGIN && &_tabload != &_tabstart) {
        src = &_tabload;
        for (dst = &_tabstart; dst < &_tabend; ) *dst++ = *src++;
    }

    SCB_VTOR = 0x10000100u;
    __asm volatile("dsb; isb");
    main();
    for (;;) {}
}

static void Default_Handler(void) { for (;;) {} }

/* The sixteen architectural Cortex-M entries, IN `.vectors` AND NOT IN
 * `.isr_vector`. The shared sections.ld opens an `.isr_vector` output
 * section at the base of FLASH, which on this part is boot2's address; the
 * name is changed so that memory.ld can pin this table at 0x10000100 by
 * address instead. mcufw/k21fn1m0/startup.c does the same thing for the same
 * structural reason.
 *
 * The RP2040's own 32 peripheral vectors follow these and are left out:
 * nothing here enables a peripheral interrupt, so an entry for one would
 * only be a place for a stray IRQ to land quietly instead of hanging
 * visibly. */
__attribute__((section(".vectors"), used))
void (* const g_vectors[16])(void) = {
    (void (*)(void))(&_estack), Reset_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, 0, 0, 0, 0, Default_Handler, Default_Handler, 0,
    Default_Handler, Default_Handler,
};
