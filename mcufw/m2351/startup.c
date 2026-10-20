/* Minimal startup for the Nuvoton M2351 (M2351KIAAE), Cortex-M23.
 *
 * NO HANDLER BEYOND THE FAULTS: nothing in a victim enables an interrupt, and
 * an ISR that could fire inside the measured region is exactly what an
 * external instrument would record as jitter.
 *
 * TWO COPY LOOPS, as on every part here. `.data` as usual, and then the AES
 * table section, which the RAM variant places in RAM with its load address
 * still in flash. One startup serves both variants and no build-time switch
 * reaches this file.
 *
 * THE SECOND LOOP'S GUARD IS NOT THE ONE THE OTHER PORTS USE, and the reason
 * is a defect in the usual test rather than anything about this part. Every
 * other startup here copies when `&_tabload != &_tabstart` and relies on the
 * two being equal in the flash arm. They are not equal in the flash arm on
 * any part in this tree. `_tabload` is `LOADADDR(.aestab)`, the section's
 * load base, while `_tabstart` is a symbol 1 KB-aligned INSIDE that section,
 * so the flash arm of this build has `_tabload = 0x40` against
 * `_tabstart = 0x400` - and the STM32L152RE, the RA2A1 and the XMC1100 all
 * show the same 0x3C0 gap in their own images. The guard is therefore true
 * in both arms, and the flash arm runs a copy loop that writes a thousand
 * words into the address range it is executing from.
 *
 * On the ST parts those stores land on a locked flash controller and are
 * discarded, which is why nothing has noticed. On this part the APROM is
 * writable only through the FMC ISP registers and what an ordinary store to
 * it does is NOT ESTABLISHED, so this port tests the thing the guard means
 * instead: copy only when the tables are linked into RAM. `_sram_start`
 * comes from memory.ld.
 *
 * NO VTOR WRITE, unlike the ST ports. The table below is linked at the base
 * of flash, which is both where this core fetches the initial SP and PC at
 * reset and the reset value of VTOR, so writing it would mean writing a
 * register this port has no measured address for in order to change nothing.
 * The rig this port is copied from does not touch it either.
 *
 * The table IS the live exception table, which is worth saying because the
 * two rigs this one's shape came from needed veneers: the XMC1100 has a
 * configuration sector at the base of flash and no VTOR, and the RA2A1 has
 * option-setting memory that must be re-emitted. This part has neither.
 */
#include <stdint.h>
#include "regs.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern uint32_t _tabload, _tabstart, _tabend;
extern uint32_t _sram_start;

extern int main(void);

void Reset_Handler(void);
static void Default_Handler(void) { for (;;) {} }

/* The ARMv8-M Baseline table starts the same way the ARMv7-M one does, and
 * this is the rig's table unchanged. The reserved slots are left as zeros
 * rather than filled with a handler: Baseline has no MemManage, BusFault or
 * UsageFault to point at, and a zero there is what the rig flashed.
 *
 * It goes in `.isr_vector` because that is the name the shared sections
 * script collects, not the `.vectors` the rig's own link script uses. */
__attribute__((section(".isr_vector"), used))
void (* const g_vectors[16])(void) = {
    (void (*)(void))(&_estack),
    Reset_Handler,
    Default_Handler,        /* NMI       */
    Default_Handler,        /* HardFault */
    0, 0, 0, 0, 0, 0, 0,
    Default_Handler,        /* SVC       */
    0, 0,
    Default_Handler,        /* PendSV    */
    Default_Handler,        /* SysTick   */
};

void Reset_Handler(void)
{
    uint32_t *src = &_sidata, *dst = &_sdata;

    while (dst < &_edata) *dst++ = *src++;
    for (dst = &_sbss; dst < &_ebss; ) *dst++ = 0;

    if ((uintptr_t)&_tabstart >= (uintptr_t)&_sram_start) {
        src = &_tabload;
        for (dst = &_tabstart; dst < &_tabend; ) *dst++ = *src++;
    }

    DSB();
    ISB();
    main();
    for (;;) {}
}
