/* Minimal startup for the Renesas RA2A1 (R7FA2A1AB3CFM), Cortex-M23.
 *
 * NO HANDLER BEYOND THE FAULTS: nothing in a victim enables an interrupt, and
 * an ISR that could fire inside the measured region is exactly what an
 * external instrument would record as jitter.
 *
 * TWO COPY LOOPS, as on every part here. `.data` as usual, and then the AES
 * table section, which the RAM variant places in RAM with its load address
 * still in flash. When the tables are left in flash the two addresses are
 * equal and the second loop does nothing, which is how one startup serves
 * both variants and why no build-time switch reaches this file.
 *
 * NO VTOR WRITE, unlike the ST ports. The vector table is linked at
 * 0x00000000 (memory.ld region VECT), which is both where this core fetches
 * the initial SP and PC at reset and the reset value of VTOR, so writing it
 * would be writing a register this port has no measured address for in order
 * to change nothing. ARMv8-M Baseline implements VTOR optionally; the rig
 * this port is copied from does not touch it either.
 *
 * The option-setting block below is the reason the code region starts at
 * 0x500. See memory.ld.
 */
#include <stdint.h>
#include "regs.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern uint32_t _tabload, _tabstart, _tabend;

extern int main(void);

/* Read off this die at 0x00000400..0x0000043F before its first flash, and
 * reproduced word for word from rigs/fw_ra2_aes/startup.c. OFS0 = 0xFFFFFFFF
 * leaves the watchdog off; OFS1 = 0xFFFFCEDF has bit 8 clear, which is what
 * starts HOCO at reset; the rest are the Security MPU region registers in
 * their disabled state.
 *
 * Do not tidy these to 0xFFFFFFFF. That would stop HOCO and the part would
 * come up on MOCO at about 500 kHz, which is a working board running a
 * hundredth as fast rather than an error.
 *
 * ONE DIFFERENCE FROM THE RIG, NOT FIXED HERE because it is outside this
 * port: the rig's own Makefile converts with `objcopy --gap-fill 0xFF`
 * precisely so the holes at 0x40..0x3FF and 0x440..0x4FF are not programmed
 * as zeros. build.mk here runs a plain `objcopy -O binary`, so those holes
 * come out 0x00. The option-setting words themselves are emitted either way,
 * so OFS0 and OFS1 are safe; whether programming zeros into the two reserved
 * holes matters on this die is NOT ESTABLISHED, and a first flash should
 * check it rather than assume.
 */
__attribute__((section(".option_setting"), used))
const uint32_t option_setting[16] = {
    0xFFFFFFFFu, 0xFFFFCEDFu, 0x000FFFFCu, 0x000FFFFFu,
    0x000FFFFCu, 0x000FFFFFu, 0x000FFFFCu, 0x000FFFFFu,
    0x200FFFFCu, 0x200FFFFFu, 0x407FFFFCu, 0x407FFFFFu,
    0x400DFFFCu, 0x400DFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
};

void Reset_Handler(void);
static void Default_Handler(void) { for (;;) {} }

/* The ARMv8-M Baseline table starts the same way the ARMv7-M one does, and
 * this is the rig's table unchanged: the Security extension is not
 * implemented on this die, so there is one table and no non-secure alias. */
__attribute__((section(".vectors"), used))
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

    if (&_tabload != &_tabstart) {
        src = &_tabload;
        for (dst = &_tabstart; dst < &_tabend; ) *dst++ = *src++;
    }

    DSB();
    ISB();
    main();
    for (;;) {}
}
