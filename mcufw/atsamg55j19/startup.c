/* Minimal startup for the ATSAMG55J19.
 *
 * UNTESTED ON SILICON. See the note at the top of board.c.
 *
 * NO HANDLER BEYOND THE FAULTS: nothing in a victim enables an interrupt,
 * and an ISR that could fire inside the measured region is exactly what the
 * external instrument would record as jitter. The peripheral slots are
 * emitted anyway, transcribed from the research tree's rig, so that a fault
 * during bring-up lands somewhere a debugger can name rather than running
 * off the end of a sixteen-entry table.
 *
 * THE WATCHDOG IS ON AT RESET AND THIS IS WHERE IT IS TURNED OFF. WDT_MR
 * comes out of reset with the watchdog enabled at roughly 16 s on this
 * family, and it is write-once per reset, so there is exactly one place the
 * write can go and this is it. Nothing else in this tree boots with a live
 * watchdog; omitting the line gives a part that answers for a quarter of a
 * minute and then resets in the middle of a long capture.
 *
 * THE VECTOR TABLE IS AT 0x00400000, NOT AT ZERO. The boot alias at
 * 0x00000000 mirrors whatever GPNVM1 selects, so the same words are visible
 * at 0 on a part set to boot from flash. VTOR is written from the table's
 * own address anyway, because "it worked through the alias" and "the table
 * is where this image thinks it is" are different claims.
 *
 * TWO COPY LOOPS. `.data` as usual, and then the AES table section, which
 * the RAM variant places in RAM with its load address still in flash. One
 * startup serves both variants and no build-time switch reaches this file;
 * what decides is where the tables ended up.
 *
 * AND THE TEST FOR THAT IS NOT `_tabload != _tabstart`, WHICH IS WHAT THE
 * EARLIER PORTS USE. It reads as "the load address differs from the run
 * address, so there is something to copy", and it is never false. The
 * shared sections script sets `_tabload = LOADADDR(.aestab)`, which is
 * where the SECTION begins, while `_tabstart` is taken after an
 * `ALIGN(1024)` inside it, so the two differ by the alignment padding in
 * the flash build as well: measured here at 0x00400108 against 0x00400400,
 * and the same shape on the F401 and the L152 at 0x08000040 against
 * 0x08000400. On an STM32 the resulting copy writes to a locked flash
 * controller and is absorbed; this part's EEFC would instead take those
 * writes into its page latch, which is not something to find out during a
 * capture. The destination is tested against the SRAM base instead, which
 * is the thing actually being asked.
 */
#include <stdint.h>
#include "regs.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern uint32_t _tabload, _tabstart, _tabend;

extern int main(void);

void Reset_Handler(void);
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
    /* SAM G55 peripheral interrupts 0..49. */
    Default_Handler,            /*  0 SUPC      */
    Default_Handler,            /*  1 RSTC      */
    Default_Handler,            /*  2 RTC       */
    Default_Handler,            /*  3 RTT       */
    Default_Handler,            /*  4 WDT       */
    Default_Handler,            /*  5 PMC       */
    Default_Handler,            /*  6 EFC       */
    Default_Handler,            /*  7 FLEXCOM7  */
    Default_Handler,            /*  8 FLEXCOM0  */
    Default_Handler,            /*  9 FLEXCOM1  */
    0,                          /* 10 reserved  */
    Default_Handler,            /* 11 PIOA      */
    Default_Handler,            /* 12 PIOB      */
    Default_Handler,            /* 13 PDMIC0    */
    Default_Handler,            /* 14 FLEXCOM2  */
    Default_Handler,            /* 15 MEM2MEM   */
    Default_Handler,            /* 16 I2SC0     */
    Default_Handler,            /* 17 I2SC1     */
    Default_Handler,            /* 18 PDMIC1    */
    Default_Handler,            /* 19 FLEXCOM3  */
    Default_Handler,            /* 20 FLEXCOM4  */
    Default_Handler,            /* 21 FLEXCOM5  */
    Default_Handler,            /* 22 FLEXCOM6  */
    Default_Handler,            /* 23 TC0       */
    Default_Handler,            /* 24 TC1       */
    Default_Handler,            /* 25 TC2       */
    Default_Handler,            /* 26 TC3       */
    Default_Handler,            /* 27 TC4       */
    Default_Handler,            /* 28 TC5       */
    Default_Handler,            /* 29 ADC       */
    Default_Handler,            /* 30 ARM/FPU   */
    0, 0, 0, 0, 0, 0, 0, 0,     /* 31..38 reserved */
    0, 0, 0, 0, 0, 0, 0, 0,     /* 39..46 reserved */
    Default_Handler,            /* 47 UHP       */
    Default_Handler,            /* 48 UDP       */
    Default_Handler,            /* 49 CRCCU     */
};

void Reset_Handler(void)
{
    uint32_t *src, *dst;

    /* First, before anything that could take sixteen seconds. */
    WDT_MR = WDT_MR_WDDIS;

    src = &_sidata;
    for (dst = &_sdata; dst < &_edata; ) *dst++ = *src++;
    for (dst = &_sbss; dst < &_ebss; ) *dst++ = 0;

    if ((uintptr_t)&_tabstart >= SS_SRAM_BASE && &_tabload != &_tabstart) {
        src = &_tabload;
        for (dst = &_tabstart; dst < &_tabend; ) *dst++ = *src++;
    }

    SCB_VTOR = (uint32_t)(uintptr_t)g_vectors;
    __asm volatile("dsb; isb");
    main();
    for (;;) {}
}
