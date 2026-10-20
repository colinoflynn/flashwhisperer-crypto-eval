/* Minimal startup for the nRF52840.
 *
 * NO HANDLER BEYOND THE FAULTS: nothing in a victim enables an interrupt,
 * and an ISR that could fire inside the measured region is exactly what an
 * external instrument would record as jitter.
 *
 * TWO COPY LOOPS, as on every part here. `.data` as usual, and then the AES
 * table section, which the RAM variant places in RAM with its load address
 * still in flash. One startup serves both variants and no build-time switch
 * reaches this file.
 *
 * THE SECOND LOOP IS GUARDED ON ITS DESTINATION BEING RAM, which is where
 * this file differs from the ST startups beside it, and the difference is
 * deliberate. Those test `_tabload != _tabstart` and expect the two to be
 * equal in the flash variant. They are not: `ld` gives `.aestab` a load
 * address that ignores the 1 KB alignment inside the section, so every port
 * in this tree links the flash variant with `_tabload` at base + 0x40 and
 * `_tabstart` at base + 0x400, the test passes, and the loop runs a
 * 4.25 KB copy whose destination is flash. On the ST parts that is merely
 * wasted boot time, because a store to locked flash there is discarded and
 * they have the silicon runs to prove it. On THIS part nothing has been run
 * at all, and whether a store to flash with NVMC.CONFIG left at its
 * read-only reset value is discarded or faults is not established here. A
 * hard fault in the first microseconds of boot would present as a board
 * that never says hello, which is the least diagnosable failure there is.
 *
 * So the guard asks the question the loop actually means - did the tables
 * move to RAM - instead of inferring it from two addresses that do not
 * compare the way the ST files assume. The RAM variant is unaffected: its
 * destination is RAM and the copy runs exactly as before.
 *
 * VTOR IS SET TO 0 HERE, not to 0x08000000. This part's flash is based at
 * zero. Writing an ST base would point the vector table at a reserved
 * address, and the part would keep running until the first exception.
 */
#include <stdint.h>
#include "regs.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern uint32_t _tabload, _tabstart, _tabend;

extern int main(void);

/* The base of this part's one SRAM, from mcufw/nrf52840/memory.ld. There is
 * no second RAM region on the nRF52840, so a destination at or above this
 * is in RAM and anything below it is flash. */
#define RAM_ORIGIN 0x20000000u

void Reset_Handler(void)
{
    uint32_t *src = &_sidata, *dst = &_sdata;

    while (dst < &_edata) *dst++ = *src++;
    for (dst = &_sbss; dst < &_ebss; ) *dst++ = 0;

    if ((uintptr_t)&_tabstart >= RAM_ORIGIN && &_tabload != &_tabstart) {
        src = &_tabload;
        for (dst = &_tabstart; dst < &_tabend; ) *dst++ = *src++;
    }

    SCB_VTOR = 0x00000000u;
    __asm volatile("dsb; isb");
    main();
    for (;;) {}
}

static void Default_Handler(void) { for (;;) {} }

/* The sixteen architectural Cortex-M entries. The nRF52840's own interrupt
 * vectors follow these and are left out: nothing here enables a peripheral
 * interrupt, so an entry for one would only be a place for a stray IRQ to
 * land quietly instead of hanging visibly. */
__attribute__((section(".isr_vector"), used))
void (* const g_vectors[16])(void) = {
    (void (*)(void))(&_estack), Reset_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, 0, 0, 0, 0, Default_Handler, Default_Handler, 0,
    Default_Handler, Default_Handler,
};
