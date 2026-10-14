/* Minimal startup for STM32H723ZG.
 *
 * Same shape as the other parts: no handler beyond the faults, and two
 * copy loops - `.data`, then the AES table section whose load address is
 * in flash and whose run address is RAM only in the twin build. When the
 * tables stay in flash the two addresses are equal and the second loop
 * does nothing, which is how one startup serves both variants.
 *
 * Nothing here enables a cache. The H7 boots with both L1 caches OFF and
 * seven wait states, and that reset state is the baseline every
 * configuration is reached FROM - see ssb_acr_set in board.c.
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

    SCB_VTOR = 0x08000000u;
    __asm volatile("dsb; isb");
    main();
    for (;;) {}
}

static void Default_Handler(void) { for (;;) {} }

__attribute__((section(".isr_vector"), used))
void (* const g_vectors[16])(void) = {
    (void (*)(void))(&_estack), Reset_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, 0, 0, 0, 0, Default_Handler, Default_Handler, 0,
    Default_Handler, Default_Handler,
};
