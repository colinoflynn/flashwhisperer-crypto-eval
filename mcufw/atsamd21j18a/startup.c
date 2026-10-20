/* Minimal startup for the ATSAMD21J18A.
 *
 * NO HANDLER BEYOND THE FAULTS: nothing in a victim enables an interrupt, and
 * an ISR that could fire inside the measured region is exactly what an
 * external instrument would record as jitter.
 *
 * TWO COPY LOOPS. `.data` as usual, and then the AES table section, which the
 * RAM variant places in RAM with its load address still in flash. One startup
 * serves both variants, so no build-time switch reaches this file.
 *
 * THE SECOND LOOP IS GUARDED ON THE DESTINATION BEING RAM, not on
 * `_tabload != _tabstart`, and that difference is this part's and not a
 * style choice. `_tabload` is the LOAD address of the whole `.aestab`
 * section and `_tabstart` is the first table inside it, after an
 * `ALIGN(1024)` that the section itself contains. In the flash build those
 * two are 0x000000b0 and 0x00000400: not equal, so the usual test copies
 * flash onto flash. On an STM32 that write is swallowed by the flash
 * controller's unlock gate and nothing happens, which is why the test has
 * been fine everywhere else in this tree. Here it would not be. Writing to
 * the SAM D21's main array address space loads the NVM page buffer, and with
 * `CTRLB.MANW` clear, which is its reset value per
 * rigs/cmsis/samd21/component/nvmctrl.h, writing the last word of a page
 * issues the page write automatically. The boot would rewrite the image it
 * is booting from.
 *
 * In the RAM build `.aestab` is the first thing placed in RAM, so its
 * `ALIGN(1024)` at 0x20000000 adds nothing and `_tabload` lines up with
 * `_tabstart` exactly. That is what makes the single source pointer right.
 *
 * THE TABLE IS THE REAL EXCEPTION TABLE. Cortex-M0 has no VTOR and parts
 * built on it have to forward every vector through ROM into SRAM stubs;
 * Cortex-M0+ does implement VTOR (samd21j18a.h sets __VTOR_PRESENT to 1) and
 * the SAM D21 boots with it at 0x00000000, which is also where this image is
 * linked. So these words are fetched straight out of flash.
 *
 * The table is the SAM D21's own, not a generic ARMv6-M one: 16 system
 * vectors then 28 peripheral vectors (EIC, NVMCTRL, DMAC, USB, EVSYS,
 * SERCOM0..5, TCC0..2, TC3..7, ADC, AC, DAC, PTC, I2S). No handler is
 * installed in any of them, but they are emitted so that a fault during
 * bring-up lands somewhere a debugger can name rather than in whatever
 * followed the table.
 */
#include <stdint.h>
#include "regs.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern uint32_t _tabload, _tabstart, _tabend;
extern uint32_t _ram_origin;

extern int main(void);

void Reset_Handler(void);

void Reset_Handler(void)
{
    uint32_t *src = &_sidata, *dst = &_sdata;

    while (dst < &_edata) *dst++ = *src++;
    for (dst = &_sbss; dst < &_ebss; ) *dst++ = 0;

    if (&_tabstart >= &_ram_origin) {
        src = &_tabload;
        for (dst = &_tabstart; dst < &_tabend; ) *dst++ = *src++;
    }

    /* Already the reset value on this part, since flash is based at zero and
     * the image is linked there. Written anyway, because a part recovered by
     * a warm reset out of some other image is not guaranteed to arrive here
     * with VTOR where the cold-reset value says. */
    SCB_VTOR = 0x00000000u;
    __asm volatile("dsb; isb");
    main();
    for (;;) {}
}

static void Default_Handler(void) { for (;;) {} }

__attribute__((section(".isr_vector"), used))
void (* const g_vectors[])(void) = {
    (void (*)(void))(&_estack),   /* 0x00  initial stack pointer            */
    Reset_Handler,                /* 0x04  reset                            */
    Default_Handler,              /* NMI                                    */
    Default_Handler,              /* HardFault                              */
    0, 0, 0, 0, 0, 0, 0,
    Default_Handler,              /* SVCall                                 */
    0, 0,
    Default_Handler,              /* PendSV                                 */
    Default_Handler,              /* SysTick                                */
    /* SAM D21 peripheral interrupts 0..27 */
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
};
