/* Minimal startup for the RP2350.
 *
 * NO HANDLER BEYOND THE FAULTS: nothing in a victim enables an interrupt,
 * and an ISR that could fire inside the measured region is exactly what an
 * external instrument would record as jitter.
 *
 * THREE COPY LOOPS, where every other part here has two. `.data` as usual,
 * then the RAM-resident code, then the AES table section.
 *
 * THE RAM-RESIDENT COPY IS FIRST AND IS NOT OPTIONAL. ssb_acr_set writes
 * XIP_CTRL and QMI_M0_TIMING and sweeps the cache maintenance window, and
 * nothing may call it before this loop has run. ssb_init does, so the order
 * here matters: ramfunc before main, always.
 *
 * THE TABLE COPY IS GUARDED ON ITS DESTINATION BEING RAM, which is what the
 * nRF52840 and RP2040 startups beside this one do. The ST startups test
 * `_tabload != _tabstart` and rely on a store to locked flash being
 * discarded, which they have silicon runs to back. THIS PART'S "FLASH" IS AN
 * XIP WINDOW, and a store into it is neither discarded nor harmless: the
 * cached window is a maintenance target, and whether writes land, fault or
 * invalidate depends on XIP_CTRL.WRITABLE_M0, which this port never sets and
 * therefore never wants to find out about from a copy loop running before
 * main. Asking the question the loop actually means - did the tables move to
 * RAM - costs one compare.
 *
 * VTOR IS SET TO 0x10000000. This part's flash is an XIP window and, unlike
 * the RP2040's, its first bytes are NOT a second-stage loader: the bootrom
 * takes the initial SP and PC from the first two words of the image itself,
 * so the vector table is the first thing in it. memory.ld pins it there and
 * asserts it.
 *
 * AND THE FIVE WORDS AFTER THE VECTOR TABLE ARE WHY THE PART BOOTS AT ALL.
 * The RP2350 bootrom searches the first 4 KB of flash for a picobin block
 * loop and boots the IMAGE_DEF it finds there; with no block it finds no
 * image and comes up in USB bootloader mode, which from the host is a board
 * that never says hello. They are emitted below as data in their own input
 * section, which memory.ld places immediately behind the vector table.
 */
#include <stdint.h>
#include "regs.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern uint32_t _ramfuncload, _sramfunc, _eramfunc;
extern uint32_t _tabload, _tabstart, _tabend;

extern int main(void);

/* The base of the striped SRAM, from mcufw/rp2350/memory.ld. A destination
 * at or above this is in RAM; anything below it is the XIP window. */
#define RAM_ORIGIN 0x20000000u

void Reset_Handler(void)
{
    uint32_t *src, *dst;

    src = &_sidata; dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;

    for (dst = &_sbss; dst < &_ebss; ) *dst++ = 0;

    src = &_ramfuncload;
    for (dst = &_sramfunc; dst < &_eramfunc; ) *dst++ = *src++;

    if ((uintptr_t)&_tabstart >= RAM_ORIGIN && &_tabload != &_tabstart) {
        src = &_tabload;
        for (dst = &_tabstart; dst < &_tabend; ) *dst++ = *src++;
    }

    SCB_VTOR = 0x10000000u;
    __asm volatile("dsb; isb");
    main();
    for (;;) {}
}

static void Default_Handler(void) { for (;;) {} }

/* The sixteen architectural Cortex-M entries, IN `.vectors` AND NOT IN
 * `.isr_vector`. The shared sections.ld opens an `.isr_vector` output section
 * at the base of FLASH; the name is changed here so that memory.ld can pin
 * this table at 0x10000000 by address, with the IMAGE_DEF block behind it, in
 * one output section whose internal order is the order the wildcards are
 * written in. mcufw/k21fn1m0/startup.c does the same for the same structural
 * reason.
 *
 * The RP2350's own peripheral vectors follow these and are left out: nothing
 * here enables a peripheral interrupt, so an entry for one would only be a
 * place for a stray IRQ to land quietly instead of hanging visibly. */
__attribute__((section(".vectors"), used))
void (* const g_vectors[16])(void) = {
    (void (*)(void))(&_estack), Reset_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, 0, 0, 0, 0, Default_Handler, Default_Handler, 0,
    Default_Handler, Default_Handler,
};

/* The picobin IMAGE_DEF block: marker, one IMAGE_TYPE item saying ARM Secure
 * EXE for an RP2350, a LAST item, a link that points back at this block
 * because it is a single-block loop, and the end marker. The values and
 * where each one came from are in regs.h; four of the five are byte-for-byte
 * what the research tree's rig carries in its own built image.
 *
 * NO ENTRY_POINT ITEM, deliberately - which is what makes the vector table's
 * address above load-bearing. */
__attribute__((section(".image_def"), used, aligned(4)))
const uint32_t g_image_def[5] = {
    PICOBIN_MARKER_START,
    PICOBIN_IMAGE_TYPE,
    PICOBIN_ITEM_LAST,
    PICOBIN_SELF_LOOP,
    PICOBIN_MARKER_END,
};
