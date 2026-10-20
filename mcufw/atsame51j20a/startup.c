/* Minimal startup for the Microchip ATSAME51J20A.
 *
 * NO HANDLER BEYOND THE FAULTS: nothing in a victim enables an interrupt,
 * and an ISR that could fire inside the measured region is exactly what an
 * external instrument would record as jitter.
 *
 * TWO COPY LOOPS. `.data` as usual, and then the AES table section, which
 * the RAM variant places in RAM with its load address still in flash. When
 * the tables are left in flash the two addresses are equal and the second
 * loop does nothing, which is how one startup serves both variants and why
 * no build-time switch reaches this file.
 *
 * THE PERIPHERAL VECTOR SLOTS ARE EMITTED, unlike the ST ports here, which
 * stop after the sixteen core entries. This part has 137 peripheral
 * interrupts and the rig in the flash-accelerator-tests tree fills all of
 * them, so that a fault during bring-up lands somewhere a debugger can
 * name rather than in whatever the linker put after the table. The cost is
 * half a kilobyte of flash that is never fetched.
 */
#include <stdint.h>
#include "regs.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern uint32_t _tabload, _tabstart, _tabend;

extern int main(void);

void Reset_Handler(void);

void Reset_Handler(void)
{
    uint32_t *src = &_sidata, *dst = &_sdata;

    while (dst < &_edata) *dst++ = *src++;
    for (dst = &_sbss; dst < &_ebss; ) *dst++ = 0;

    if (&_tabload != &_tabstart) {
        src = &_tabload;
        for (dst = &_tabstart; dst < &_tabend; ) *dst++ = *src++;
    }

    /* Written rather than left alone, so the image states where its own
     * table is instead of relying on the reset value agreeing with the
     * linker. Flash is based at zero on this part. */
    SCB_VTOR = 0x00000000u;
    __asm volatile("dsb; isb");
    main();
    for (;;) {}
}

static void Default_Handler(void) { for (;;) {} }

__attribute__((section(".isr_vector"), used))
void (* const g_vectors[])(void) = {
    (void (*)(void))(&_estack), Reset_Handler,
    Default_Handler,            /* NMI        */
    Default_Handler,            /* HardFault  */
    Default_Handler,            /* MemManage  */
    Default_Handler,            /* BusFault   */
    Default_Handler,            /* UsageFault */
    0, 0, 0, 0,
    Default_Handler,            /* SVC        */
    Default_Handler,            /* DebugMon   */
    0,
    Default_Handler,            /* PendSV     */
    Default_Handler,            /* SysTick    */
#define D16 Default_Handler, Default_Handler, Default_Handler, Default_Handler, \
            Default_Handler, Default_Handler, Default_Handler, Default_Handler, \
            Default_Handler, Default_Handler, Default_Handler, Default_Handler, \
            Default_Handler, Default_Handler, Default_Handler, Default_Handler
    D16, D16, D16, D16, D16, D16, D16, D16,      /* 0..127   */
    D16                                           /* 128..143 */
#undef D16
};
