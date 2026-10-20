/* Minimal startup for STM32L475VG.
 *
 * NO HANDLER BEYOND THE FAULTS: nothing in a victim enables an interrupt,
 * and an ISR that could fire inside the measured region is exactly what the
 * external instrument would record as jitter.
 *
 * TWO COPY LOOPS. `.data` as usual, and then the AES table section, which
 * the RAM variant places in RAM with its load address still in flash. When
 * the tables are left in flash the two addresses are equal and the second
 * loop does nothing - which is how one startup serves both variants, and
 * why no build-time switch reaches this file.
 *
 * THE FPU IS ENABLED BEFORE THE COPY LOOPS, AND THAT ORDER IS THE POINT.
 * This is a hard-float build (-mfloat-abi=hard -mfpu=fpv4-sp-d16, read off
 * the research tree's rig for this part), and a word-copy loop is exactly
 * what a compiler vectorises into VFP register pairs. With CP10/CP11 still
 * disabled the first such instruction takes a NOCP UsageFault inside
 * Reset_Handler - before the console exists, so the board boots into the
 * fault handler's spin loop and SAYS NOTHING AT ALL, which from the host
 * is indistinguishable from a part that was never flashed.
 *
 * It does not happen at the -O2 this repository builds at, and the two
 * other hard-float ports here do not do it, so this is NOT something the
 * board contract requires - it is four instructions that remove a failure
 * mode whose symptom is silence. Nothing in the measured window touches
 * the FPU either way; AES-128 has no floating point in it.
 */
#include <stdint.h>
#include "regs.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern uint32_t _tabload, _tabstart, _tabend;

extern int main(void);

void Reset_Handler(void)
{
    uint32_t *src = &_sidata, *dst = &_sdata;

    SCB_CPACR |= SCB_CPACR_CP10_CP11_FULL;
    __asm volatile("dsb; isb");

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
