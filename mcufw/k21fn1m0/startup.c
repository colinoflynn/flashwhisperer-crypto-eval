/* Startup for the NXP Kinetis MK21FN1M0VMC12.
 *
 * UNTESTED ON SILICON. Nothing in this directory has been run on a board.
 *
 * Two things happen here that no other part in this tree needs, and both of
 * them are copied from rigs/fw_k21_aes/startup.c, which is the version that
 * has been run on this silicon. Each one fails without an error message.
 *
 * 1. THE FLASH CONFIGURATION FIELD at 0x400..0x40F. The FTFE loads FSEC out
 *    of flash at 0x40C on every reset. Erased flash reads 0xFF and
 *    FSEC = 0xFF is SEC = 0b11 = SECURED, so an image that does not emit
 *    these sixteen bytes re-secures the part every time it boots: the
 *    debugger is locked out and recovery costs a mass erase. The board on
 *    the bench already arrived secured once and was silently mass-erased by
 *    J-Link on first connect (research/targets/k21fn1m0.md section 2f).
 *
 *    The byte values below are the rig's, unchanged. Its decode, which is
 *    reproduced here because the one field that matters is easy to get
 *    subtly and permanently wrong:
 *
 *      FSEC = 0xFE   SEC    = 0b10  unsecure (0b00, 0b01 and 0b11 are SECURE)
 *                    FSLACC = 0b11  factory access granted
 *                    MEEN   = 0b11  MASS ERASE ENABLED
 *                    KEYEN  = 0b11  backdoor key access disabled
 *
 *    MEEN is the one to get right. MEEN = 0b10 disables mass erase, and on
 *    a part that later ends up secured, no backdoor key plus no mass erase
 *    leaves no way back in at all. 0b11 keeps the recovery path this board
 *    has already needed once.
 *
 *    FOPT stays 0xFF (the erased default) and FPROT0-3 stay 0xFF, which is
 *    "no program flash protected". Nothing here writes its own flash, and a
 *    protected region would also block the programmer.
 *
 *    memory.ld pins the field at 0x400 by address and asserts it is sixteen
 *    bytes long, so the link fails rather than the board locking.
 *
 * 2. THE WATCHDOG IS ENABLED OUT OF RESET. WDOG_STCTRLH resets with WDOGEN
 *    set, so a part that is never fed resets itself in the low hundreds of
 *    milliseconds, which from the host looks exactly like a victim that
 *    stops answering partway through a capture. Disabling it needs an
 *    unlock sequence with a DEADLINE: the two magic halfwords must be
 *    followed by the STCTRLH write within 256 bus clocks. That is why
 *    wdog_disable() is the first thing Reset_Handler does, ahead of every
 *    copy loop, and why it is always_inline - a copy over a few KB in
 *    between would miss the window.
 *
 * THREE COPY LOOPS, where the other parts here have two. The extra one is
 * .ramfunc: RM S28.4 says the FMC's registers must be programmed while the
 * flash controller is idle, "for example, execute from RAM", so board.c's
 * apply function is linked into SRAM and has to be put there before
 * anything calls it. The table loop is the shared one: when the tables are
 * left in flash the load and run addresses are equal and it does nothing,
 * which is how one startup file serves both residence variants.
 *
 * NO HANDLER BEYOND THE FAULTS, as on every other part here: nothing in a
 * victim enables an interrupt, and an ISR that could fire inside the
 * measured region is exactly what an external instrument would record as
 * jitter.
 */
#include <stdint.h>
#include "regs.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern uint32_t _sramfunc, _eramfunc, _ramfuncload;
extern uint32_t _tabload, _tabstart, _tabend;

extern int main(void);

/* Emitted as bytes in address order: 8 backdoor key, FPROT3..FPROT0, FSEC,
 * FOPT, FEPROT, FDPROT. Copied byte for byte from
 * rigs/fw_k21_aes/startup.c. */
__attribute__((section(".flashconfig"), used))
const uint8_t FLASH_CONFIG_FIELD[16] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  /* backdoor key, unused */
    0xFF, 0xFF, 0xFF, 0xFF,                          /* FPROT3..FPROT0       */
    0xFE,                                            /* FSEC, see above      */
    0xFF,                                            /* FOPT                 */
    0xFF,                                            /* FEPROT   (reserved)  */
    0xFF                                             /* FDPROT   (reserved)  */
};

__attribute__((always_inline)) static inline void wdog_disable(void)
{
    WDOG_UNLOCK = WDOG_UNLOCK_KEY1;
    WDOG_UNLOCK = WDOG_UNLOCK_KEY2;
    __asm volatile("" ::: "memory");
    WDOG_STCTRLH = WDOG_STCTRLH_OFF;
}

void Reset_Handler(void);

void Reset_Handler(void)
{
    uint32_t *src, *dst;

    wdog_disable();            /* FIRST: the unlock window is 256 bus clocks */

    src = &_ramfuncload;
    for (dst = &_sramfunc; dst < &_eramfunc; ) *dst++ = *src++;

    src = &_sidata;
    for (dst = &_sdata; dst < &_edata; ) *dst++ = *src++;

    for (dst = &_sbss; dst < &_ebss; ) *dst++ = 0;

    /* The table copy is conditioned on the tables being IN RAM, not on the
     * load and run addresses differing, and the difference matters on this
     * part.
     *
     * The shared sections.ld takes `_tabload` from the start of .aestab and
     * then aligns the location counter to 1 KB inside it, so in the FLASH
     * variant the two symbols differ by that alignment even though nothing
     * needs moving. The other ports here copy anyway, which on an ST part
     * is a write into flash address space that evidently does no harm.
     * Nothing in rigs/fw_k21_aes establishes what the FTFE does with a bus
     * write into program flash space, and boot is not where to find out,
     * so this port declines to make it. SRAM_U is the only RAM in the map
     * (see memory.ld), so its base is the whole test. */
    if ((uintptr_t)&_tabstart >= 0x20000000u) {
        src = &_tabload;
        for (dst = &_tabstart; dst < &_tabend; ) *dst++ = *src++;
    }

    /* The vector table is at 0, which is also VTOR's reset value, so this is
     * a no-op on a clean reset. It is written anyway so that a warm start
     * from a debugger that left VTOR somewhere else lands in the same place
     * a cold one does. */
    SCB_VTOR = 0x00000000u;
    __asm volatile("dsb; isb");

    main();
    for (;;) {}
}

static void Default_Handler(void) { for (;;) {} }

/* `.vectors`, not `.isr_vector`: memory.ld pins this at address 0 ahead of
 * the flash configuration field, and the shared sections.ld would otherwise
 * claim the vectors under its own name and place them after it. See the
 * note at the top of memory.ld. */
__attribute__((section(".vectors"), used))
void (* const g_vectors[16])(void) = {
    (void (*)(void))(&_estack), Reset_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, 0, 0, 0, 0, Default_Handler, Default_Handler, 0,
    Default_Handler, Default_Handler,
};
