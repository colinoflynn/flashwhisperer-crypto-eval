/* Minimal startup for the Infineon XMC1100-Q024F0064 (Cortex-M0).
 *
 * NO HANDLER BEYOND THE FAULTS: nothing in a victim enables an interrupt,
 * and an ISR that could fire inside the measured region is exactly what an
 * external instrument would record as jitter.
 *
 * TWO COPY LOOPS, as on every part here. `.data` as usual, and then the
 * table section, which the RAMTAB=1 variant places in RAM with its load
 * address still in flash. When the tables are left in flash the two
 * addresses are equal and the second loop does nothing, which is how one
 * startup serves both variants and why no build-time switch reaches this
 * file.
 *
 * TWO THINGS DIFFER FROM THE ARMv7-M PORTS IN THIS TREE.
 *
 * There is no VTOR on Cortex-M0, so the table below is NOT the live
 * exception table and nothing here points the core at it. XMC1000 vectors
 * every exception through the boot ROM at 0x00000000, which forwards to
 * stubs in SRAM at 0x2000000C..0x200000BF - the region memory.ld excludes.
 * What the ROM actually reads out of this table is the first two words: the
 * initial stack pointer at 0x10001000 and the reset vector at 0x10001004.
 * The rest is emitted for a debugger's benefit and for symmetry with the
 * other ports, and is never fetched. The victim runs with interrupts off
 * through the measured region, so nothing depends on the remapped table
 * being populated.
 *
 * There is no option-setting block to re-emit. The XMC1000 equivalents of
 * one (BMI, CLK_VAL1 and CLK_VAL2, the protection words) live in the flash
 * configuration sector at 0x10000000..0x10000FFF, which is below this
 * image's origin and outside its write range, so they are left alone by
 * construction rather than by being copied back.
 *
 * The section name is .isr_vector, not the .vectors the research rig uses,
 * because the shared sections script is what KEEPs it and that is the name
 * it keeps.
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

    __asm volatile("dsb; isb");
    main();
    for (;;) {}
}

static void Default_Handler(void) { for (;;) {} }

__attribute__((section(".isr_vector"), used))
void (* const g_vectors[16])(void) = {
    (void (*)(void))(&_estack), Reset_Handler,
    Default_Handler, Default_Handler, 0, 0, 0, 0, 0, 0, 0,
    Default_Handler, 0, 0, Default_Handler, Default_Handler,
};
