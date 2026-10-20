/* Minimal startup for the CC1352R1F3.
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
 * VTOR IS NOT WRITTEN HERE, unlike the ST ports. The vector table on this
 * part is at address 0 because the boot ROM branches there - CCFG on the
 * die says the application vector table is at 0 - and VTOR resets to 0,
 * so a write would only restate it. The rig does not write it either.
 */
#include <stdint.h>

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

    __asm volatile("dsb 0xF; isb 0xF" ::: "memory");
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
