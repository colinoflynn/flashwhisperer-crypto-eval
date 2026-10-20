/* Register definitions for ADuCM3029 (Analog Devices, Cortex-M3), by hand.
 *
 * NO VENDOR HEADERS, same reasoning as the STM32 ports: this part needs
 * about twenty registers and vendoring ADI's CMSIS tree for twenty
 * registers is more to keep in sync than it saves.
 *
 * PROVENANCE. Every address, bit position and divisor below was copied from
 * rigs/fw_aducm_aes/aducm3029_min.h in the research tree, which records for
 * each one where it came from (ADI's own ADuCM302x.h REG_/BITP_/BITM_
 * macros, ADI's system_ADuCM3029.c, ADI's mbed-os serial_api.c, and the
 * ADuCM3027/ADuCM3029 Rev. B datasheet for the architectural facts).
 * Nothing here was invented, and nothing was added that the rig header does
 * not establish. Where this port needs something the rig header does not
 * carry, the board file says so and does without it rather than guessing an
 * address: a wrong address on this part produces plausible numbers, not a
 * fault.
 *
 * THE ACCELERATOR WORD, AND WHY IT IS ONE BIT WIDE
 * ------------------------------------------------
 * The board contract wants one opaque word for the flash accelerator. On an
 * STM32 that is FLASH_ACR. This part has no FLASH_ACR and, more to the
 * point, no read-latency control anywhere in its memory map. Not a one-bit
 * LATENCY as on the L152RE, not a four-bit one as on the F4: there is no
 * wait-state field at all. FLCC_TIME_PARAM0/1 look like candidates and are
 * not, since every field in them (TNVS, TPGS, TPROG, TNVH, TRCV, TERASE,
 * TNVH1, TWK) times the PROGRAM/ERASE path rather than the read path.
 * FLCC_UCFG is keyhole DMA and FLCC_VOL_CFG is info-space remap.
 *
 * The only software-visible knob on the read path is FLCC_CACHE_SETUP.ICEN,
 * a single bit, and the cache it gates is INSTRUCTION-ONLY. There is no
 * data-cache enable to pair with it the way FLASH_ACR on an F4 has DCEN
 * beside ICEN, so the packed word is:
 *
 *   bit 0   ICEN    instruction cache enable. Written through
 *                   FLCC_CACHE_SETUP, READ BACK from FLCC_CACHE_STAT.
 *   bits 1+ unused. Not reserved for a future field, not zero-by-
 *           convention: there is nothing else on this part's read path to
 *           put there. ssb_acr_set refuses a word with any of them set.
 *
 * So the useful sweep on this part is two cells, 0x1 and 0x0, and the
 * honest statement is that neither of them is a wait-state change. A reader
 * comparing a trace count here against an STM32 cell should know that this
 * part has no defender knob on the data path at all.
 */
#ifndef ADUCM3029_REGS_H
#define ADUCM3029_REGS_H

#include <stdint.h>

#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))

/* ---- Cortex-M3 core (ARMv7-M) ---- */
#define SYST_CSR   REG(0xE000E010u)   /* SysTick control/status              */
#define SYST_RVR   REG(0xE000E014u)   /* reload value                        */
#define SYST_CVR   REG(0xE000E018u)   /* current value, 24-bit, counts DOWN  */
#define SCB_VTOR   REG(0xE000ED08u)

/* DWT is deliberately absent. The rig header establishes no DWT addresses
 * for this part, and CYCCNT is optional on a Cortex-M3 implementation, so
 * whether this die has one is NOT ESTABLISHED here. board.c refuses
 * TIMING=dwt rather than pointing a load at an address nobody has checked:
 * a DWT that is not implemented reads as zero, and zero cycles for every
 * window reads exactly like a constant-time result. */

static inline void irq_off(void) { __asm volatile("cpsid i" ::: "memory"); }
static inline void irq_on(void)  { __asm volatile("cpsie i" ::: "memory"); }

/* ---- flash controller and its cache (FLCC0, 0x40018000) ---- */
#define FLCC_CACHE_STAT  REG(0x40018058u)   /* bit 0 ICEN, read-only        */
#define FLCC_CACHE_SETUP REG(0x4001805Cu)   /* bit 0 ICEN                   */
#define FLCC_CACHE_KEY   REG(0x40018060u)   /* write-only unlock            */

#define FLCC_CACHE_ICEN  (1u << 0)

/* The unlock value ADI's own system_ADuCM3029.c writes to CACHE_KEY before
 * every SETUP write (CACHE_CONTROLLER_KEY). The key is consumed by the
 * write it precedes, so it has to be re-armed each time: writing SETUP
 * twice after one key write silently leaves the second write undone, which
 * would look exactly like a part that ignores the bit. */
#define FLCC_CACHE_KEY_VAL 0xF123F456u

/* ---- clocking (CLKG0, 0x4004C300) ---- */
#define CLKG_CLK_CTL1    REG(0x4004C304u)   /* HCLKDIVCNT 7:0, PCLKDIVCNT 15:8 */

/* HFOSC is 26 MHz and is the reset clock source (__HFOSC in ADI's
 * system_ADuCM3029.h). 26 MHz is also the part's maximum core clock, so
 * unlike the L152RE there is no "raise the clock until a wait state becomes
 * mandatory" experiment available here. There is nothing to make
 * mandatory. */
#define HFOSC_HZ         26000000u

/* ---- GPIO port 0 (0x40020000), the mux register only ---- */
/* Two bits per pin in CFG: 0 = GPIO, 1..3 = the multiplexed functions in
 * the datasheet's GPIO Multiplexing table (Rev. B p.31). P0_10 and P0_11
 * list UART0_TX and UART0_RX in the FIRST mux column and "Not available" in
 * the other two, so the value for both is 1.
 *
 * ONLY THE MUX REGISTER IS HERE. The port's output-enable and set/clear
 * data registers are not in the rig header, because the rig drives no
 * GPIO output, so their offsets are NOT ESTABLISHED by anything this port
 * has read. That is why ssb_trig_high/low in board.c are empty. */
#define GPIO0_CFG        REG(0x40020000u)
#define GPIO0_CFG_P0_10_Pos 20u
#define GPIO0_CFG_P0_11_Pos 22u

/* ---- UART0 (0x40005000), 16550-style with ADI's fractional divider ---- */
#define UART0_RX         REG(0x40005000u)
#define UART0_TX         REG(0x40005000u)
#define UART0_IEN        REG(0x40005004u)
#define UART0_LCR        REG(0x4000500Cu)
#define UART0_LSR        REG(0x40005014u)
#define UART0_FCR        REG(0x40005020u)
#define UART0_FBR        REG(0x40005024u)
#define UART0_DIV        REG(0x40005028u)
#define UART0_LCR2       REG(0x4000502Cu)
#define UART0_CTL        REG(0x40005030u)

#define UART_LSR_THRE     (1u << 5)   /* TX holding register empty */
#define UART_LSR_DR       (1u << 0)   /* RX data ready             */
#define UART_LCR_WLS_8BIT (3u << 0)
#define UART_FBR_FBEN     (1u << 15)
#define UART_FBR_DIVM_Pos 11u
#define UART_CTL_FORCECLK (1u << 1)   /* hold UCLK on instead of gating it */

/* 115200 8N1 from a 26 MHz PCLK.
 *
 * ADuCM302x baud = PCLK / (2^(OSR+2) * DIV * (M + N/2048)).
 *
 * The divisors are ADI's own, from serial_baud() in mbed-os
 * targets/.../ADuCM3029/api/serial_api.c: DIV=7, M=2, N=31, OSR=2.
 * Check: 26e6 / (16 * 7 * (2 + 31/2048)) = 115 201, 0.001 % error.
 *
 * NOTE ON ADI'S SOURCE, because it will mislead whoever checks this: the
 * constants there are written `uartdivn= 0031` and `uartdivc= 07`, and a
 * leading zero in C is OCTAL. 0031 is 25, not 31, which lands the baud at
 * 115 369, which is 0.15 % off and so inside tolerance, so their bug never
 * shows. The 9600 row settles which was meant: with N decimal 1734 it
 * computes to 9602, with N octal (988) it computes to 10 604, which no UART
 * would accept. The values here are therefore DECIMAL, deliberately, and
 * differ from a literal reading of ADI's file.
 *
 * ONE BAUD RATE ONLY. These four numbers are a solved divisor set for
 * 115200 at 26 MHz, not a formula, so board.c refuses any other SS_BAUD
 * rather than computing one from a divider model nothing here has checked
 * against silicon. */
#define UART_DIV_115200  7u
#define UART_DIVM_115200 2u
#define UART_DIVN_115200 31u
#define UART_OSR_115200  2u

#endif /* ADUCM3029_REGS_H */
