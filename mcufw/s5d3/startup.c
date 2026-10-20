/* Minimal startup for the Renesas Synergy S5D3. Untested on silicon.
 *
 * NO HANDLER BEYOND THE FAULTS, as on every part here: nothing in a victim
 * enables an interrupt, and an ISR that could fire inside the measured
 * region is what an external instrument would record as jitter.
 *
 * TWO COPY LOOPS. `.data` as usual, then the AES table section, which the
 * RAM variant places in RAM with its load address still in flash. With the
 * tables left in flash the two addresses are equal and the second loop does
 * nothing, which is how one startup serves both variants.
 *
 * WHAT IS MISSING HERE AND IS MISSING DELIBERATELY. The rig's startup emits
 * two more blocks that this one cannot: the reset vectors at 0x00000000 and
 * the option-setting memory at 0x00000400, which carries OFS0, OFS1 and the
 * Security MPU region registers. Both need their own output sections, and
 * the SECTIONS block here is shared by every part in the tree and places
 * only .isr_vector, .aestab, .text, .data and .bss. So this image's vector
 * table lands at 0x00000500 where the FLASH region begins, nothing is
 * emitted below it, and the part would fetch its initial SP and PC from
 * erased flash. memory.ld records the same gap. Flashing this image means
 * solving that first, and the option words in particular are not optional:
 * the rig reads them off the die before the first flash and reproduces them
 * byte-exact, because an erased OFS1 is a different reset clock source.
 *
 * VTOR IS SET FROM THE VECTOR TABLE'S OWN ADDRESS rather than from a
 * constant, so it stays correct if the FLASH origin in memory.ld is ever
 * changed. The ST ports here hardcode 0x08000000 and can, since their flash
 * base is fixed; this part's is a choice recorded in one place.
 */
#include <stdint.h>
#include "regs.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern uint32_t _tabload, _tabstart, _tabend;

extern int main(void);

void Reset_Handler(void);
static void Default_Handler(void) { for (;;) {} }

__attribute__((section(".isr_vector"), used))
void (* const g_vectors[16])(void) = {
    (void (*)(void))(&_estack), Reset_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, 0, 0, 0, 0, Default_Handler, Default_Handler, 0,
    Default_Handler, Default_Handler,
};

void Reset_Handler(void)
{
    uint32_t *src = &_sidata, *dst = &_sdata;

    while (dst < &_edata) *dst++ = *src++;
    for (dst = &_sbss; dst < &_ebss; ) *dst++ = 0;

    if (&_tabload != &_tabstart) {
        src = &_tabload;
        for (dst = &_tabstart; dst < &_tabend; ) *dst++ = *src++;
    }

    SCB_VTOR = (uint32_t)(uintptr_t)g_vectors;
    __asm volatile("dsb 0xF; isb 0xF");
    main();
    for (;;) {}
}
