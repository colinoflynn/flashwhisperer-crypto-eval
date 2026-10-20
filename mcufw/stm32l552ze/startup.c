/* Minimal Cortex-M33 startup for STM32L552ZE, TZEN = 0 and the classic
 * non-secure map.
 *
 * NO HANDLER BEYOND THE FAULTS: nothing in a victim enables an interrupt,
 * and an ISR that could fire inside the measured region is exactly what
 * an external instrument would record as jitter.
 *
 * TWO COPY LOOPS. `.data` as usual, and then the AES table section, which
 * the RAM variant places in RAM with its load address still in flash.
 * When the tables are left in flash the two addresses are equal and the
 * second loop does nothing, which is how one startup serves both variants
 * and why no build-time switch reaches this file.
 *
 * THE FPU IS ENABLED HERE, which the M4 ports get away with not doing.
 * board.mk builds with -mfloat-abi=hard, so the compiler may emit an FP
 * instruction in any translation unit, and on this part CP10/CP11 come
 * out of reset denied: the result is a UsageFault on first use rather
 * than a wrong number. rigs/fw_l5_cache/startup.c does the same write for
 * the same reason.
 *
 * The vector table is the first sixteen entries, and entry 7 is
 * SecureFault on this core where the M4 parts have a reserved word. The
 * rest of the table is left out: with no interrupt enabled nothing can
 * reach it, and VTOR is set to the image base below.
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
    SCB_CPACR |= SCB_CPACR_CP10_CP11_FULL;
    __asm volatile("dsb; isb");
    main();
    for (;;) {}
}

static void Default_Handler(void) { for (;;) {} }

__attribute__((section(".isr_vector"), used))
void (* const g_vectors[16])(void) = {
    (void (*)(void))(&_estack),   /* 0  initial SP  */
    Reset_Handler,                /* 1  reset       */
    Default_Handler,              /* 2  NMI         */
    Default_Handler,              /* 3  HardFault   */
    Default_Handler,              /* 4  MemManage   */
    Default_Handler,              /* 5  BusFault    */
    Default_Handler,              /* 6  UsageFault  */
    Default_Handler,              /* 7  SecureFault */
    0, 0, 0,                      /* 8-10 reserved  */
    Default_Handler,              /* 11 SVCall      */
    Default_Handler,              /* 12 DebugMon    */
    0,                            /* 13 reserved    */
    Default_Handler,              /* 14 PendSV      */
    Default_Handler,              /* 15 SysTick     */
};
