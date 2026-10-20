/* nRF52840 registers, by hand, and the accelerator-word packing.
 *
 * Every address and bit position below was taken from one of two places and
 * from nowhere else: the nRF rigs in the research tree that have been run on
 * this silicon (rigs/fw_nrf_aes/main.c, rigs/fw_nrf_probe/main.c,
 * rigs/fw_map_nrf/main.c), and Nordic's own MDK headers nrf52840.h /
 * nrf52840_bitfields.h. Where the two overlap they agree. Anything neither
 * of them gave is not here.
 *
 * THERE IS NO FLASH_ACR ON THIS PART, AND NO WAIT-STATE FIELD ANYWHERE.
 * Nordic's NVMC has READY, CONFIG, the erase registers, and then three
 * cache registers at 0x540, 0x548 and 0x54C. There is no latency field to
 * program and nothing in the part announces one, so the LATENCY axis that
 * every ST part here sweeps simply does not exist on this one. The research
 * tree records this as a property of the part rather than as something not
 * yet found: research/reference/DATASHEETS.md's accelerator row for the
 * nRF52840 reads "none - no such register anywhere".
 *
 * THE ONE KNOB IS INSTRUCTION-SIDE, AND THAT IS THE WHOLE POINT OF THE
 * PART. NVMC.ICACHECNF enables a 2 KB direct-mapped cache that sits on the
 * ICODE bus. It caches instruction fetches. It does not see data accesses,
 * and that is measured rather than read off a datasheet: the research tree
 * walked flash as data and watched IHIT and IMISS both stay still, then
 * walked the same flash as code and watched both move
 * (research/targets/nrf52840.md section 4, summarised in DATASHEETS.md).
 *
 * So neither bit in the accelerator word can reach the data path this
 * measurement looks at. The word is still here and still round-trips,
 * because the capture has to record what the part was configured as, and
 * because "we set the only knob there is and nothing moved" is a result.
 * A NULL ON THIS AXIS IS THE EXPECTED OUTCOME, not a broken sweep or a
 * mis-wired board file. The research tree has already measured it: every
 * ICACHECNF setting leaks identically, and the part gives up a full
 * AES-128 key anyway through a 1-entry 16-byte read latch that no register
 * reaches and no Nordic document mentions.
 *
 * ---- packing of the opaque accelerator word ----------------------------
 *
 * The word IS NVMC.ICACHECNF, bit for bit, and no second register is
 * folded in. That is a deliberate choice and not laziness: the two M7
 * parts here had to pack because their caches live in three different
 * peripherals, whereas here there is exactly one accelerator register and
 * nothing else on the part that an accelerator word could honestly carry.
 * Packing a fabricated LATENCY field into the high bits so the word "looks
 * like" an ST one would invent an axis the silicon does not have.
 *
 *     bit 0   CACHEEN      0 = disabled, 1 = enabled.
 *                          Writing 0 ALSO INVALIDATES every cache entry -
 *                          Nordic's own header says so at
 *                          NVMC_ICACHECNF_CACHEEN_Disabled. That is what
 *                          makes the reset-equivalent dance in board.c
 *                          possible at all.
 *     bit 8   CACHEPROFEN  0 = IHIT/IMISS frozen, 1 = they count.
 *
 * Every other bit is reserved. ssb_acr_set refuses a word with any of them
 * set, and ssb_acr_get reads the register rather than reconstructing the
 * request, so a refusal is visible to the host as the old value coming
 * back.
 *
 * Useful words, which is all of them: 0x000 off, 0x001 cache on,
 * 0x101 cache on with the hit/miss counters running.
 */
#ifndef SS_REGS_H
#define SS_REGS_H

#include <stdint.h>

/* ---- NVMC: the accelerator, such as it is ------------------------------
 * Base and the three cache offsets from nrf52840.h's NRF_NVMC_Type; the
 * same three appear in rigs/fw_nrf_aes/main.c, which has been run on this
 * part. 0x544 between ICACHECNF and IHIT is reserved and is not touched. */
#define NVMC_BASE_REG     0x4001E000u
#define NVMC_ICACHECNF    (*(volatile uint32_t *)(NVMC_BASE_REG + 0x540u))
#define NVMC_IHIT         (*(volatile uint32_t *)(NVMC_BASE_REG + 0x548u))
#define NVMC_IMISS        (*(volatile uint32_t *)(NVMC_BASE_REG + 0x54Cu))

#define NVMC_ICACHECNF_CACHEEN     (1u << 0)
#define NVMC_ICACHECNF_CACHEPROFEN (1u << 8)
/* The bits the opaque word is allowed to carry. Anything else is refused. */
#define NRF_ACR_MASK      (NVMC_ICACHECNF_CACHEEN | NVMC_ICACHECNF_CACHEPROFEN)

/* ---- CLOCK -------------------------------------------------------------
 * The core runs at 64 MHz whichever HFCLK source is selected: Nordic's own
 * HFCLKSTAT.SRC enumeration names both of them 64 MHz, HFINT for the
 * internal RC and Xtal for the crystal. Starting the crystal therefore buys
 * frequency ACCURACY, which the UART needs, not a different core rate. */
#define CLOCK_BASE_REG    0x40000000u
#define CLOCK_HFCLKSTART  (*(volatile uint32_t *)(CLOCK_BASE_REG + 0x000u))
#define CLOCK_HFCLKSTARTED (*(volatile uint32_t *)(CLOCK_BASE_REG + 0x100u))
#define CLOCK_HFCLKSTAT   (*(volatile uint32_t *)(CLOCK_BASE_REG + 0x40Cu))

/* ---- UART0 -------------------------------------------------------------
 * Nordic's UART, not an ST USART, and it works by tasks and events rather
 * than by status flags: write 1 to a task register to start something, poll
 * an event register for 0 or 1 and clear it by writing 0. Taken whole from
 * rigs/fw_nrf_aes/main.c, which is the version that has run on silicon,
 * and checked against NRF_UART_Type.
 *
 * This is the legacy UART at 0x40002000, not UARTE. UARTE shares the base
 * address and is the same peripheral in a different mode: it moves bytes by
 * EasyDMA instead of one at a time through TXD/RXD. The byte-at-a-time
 * interface is what the console contract wants, so the legacy mode is the
 * right one here and is what every nRF rig in the research tree uses. */
#define UART0_BASE_REG    0x40002000u
#define UART_STARTRX      (*(volatile uint32_t *)(UART0_BASE_REG + 0x000u))
#define UART_STARTTX      (*(volatile uint32_t *)(UART0_BASE_REG + 0x008u))
#define UART_EV_RXDRDY    (*(volatile uint32_t *)(UART0_BASE_REG + 0x108u))
#define UART_EV_TXDRDY    (*(volatile uint32_t *)(UART0_BASE_REG + 0x11Cu))
#define UART_ENABLE       (*(volatile uint32_t *)(UART0_BASE_REG + 0x500u))
#define UART_PSELTXD      (*(volatile uint32_t *)(UART0_BASE_REG + 0x50Cu))
#define UART_PSELRXD      (*(volatile uint32_t *)(UART0_BASE_REG + 0x514u))
#define UART_RXD          (*(volatile uint32_t *)(UART0_BASE_REG + 0x518u))
#define UART_TXD          (*(volatile uint32_t *)(UART0_BASE_REG + 0x51Cu))
#define UART_BAUDRATE     (*(volatile uint32_t *)(UART0_BASE_REG + 0x524u))

/* ENABLE takes an enumerated value, not a bit: 4 is Enabled. */
#define UART_ENABLE_ENABLED 4u

/* BAUDRATE is a register-specific constant and NOT a divisor that can be
 * computed from the clock, so there is no arithmetic to write here - only
 * the constants Nordic publishes. board.c maps SS_BAUD onto these and
 * fails the BUILD for a baud rate that has no constant, rather than
 * programming something plausible. 115200's constant is the one the nRF
 * rigs use. */
#define UART_BAUDRATE_9600   0x00275000u
#define UART_BAUDRATE_57600  0x00EBF000u
#define UART_BAUDRATE_115200 0x01D7E000u
#define UART_BAUDRATE_230400 0x03AFB000u
#define UART_BAUDRATE_460800 0x075F7000u
#define UART_BAUDRATE_921600 0x0EBED000u
#define UART_BAUDRATE_1M     0x10000000u

/* ---- GPIO port 0 -------------------------------------------------------
 * OUTSET and DIRSET are the two the nRF rigs use; OUT, OUTCLR, DIR and
 * DIRCLR come from NRF_GPIO_Type in nrf52840.h. There is no clock-enable
 * register for GPIO on this part, which is why board.c has nothing
 * resembling the RCC lines an ST board file needs. */
#define P0_BASE_REG       0x50000000u
#define P0_OUTSET         (*(volatile uint32_t *)(P0_BASE_REG + 0x508u))
#define P0_OUTCLR         (*(volatile uint32_t *)(P0_BASE_REG + 0x50Cu))
#define P0_DIRSET         (*(volatile uint32_t *)(P0_BASE_REG + 0x518u))

/* ---- core --------------------------------------------------------------
 * Cortex-M4 architectural addresses, identical to every other M3/M4/M7
 * board file here. */
#define SCB_VTOR          (*(volatile uint32_t *)0xE000ED08u)

#define SYST_CSR          (*(volatile uint32_t *)0xE000E010u)
#define SYST_RVR          (*(volatile uint32_t *)0xE000E014u)
#define SYST_CVR          (*(volatile uint32_t *)0xE000E018u)

#define DEMCR             (*(volatile uint32_t *)0xE000EDFCu)
#define DEMCR_TRCENA      (1u << 24)
#define DWT_CTRL          (*(volatile uint32_t *)0xE0001000u)
#define DWT_CTRL_CYCCNTENA (1u << 0)
#define DWT_CYCCNT        (*(volatile uint32_t *)0xE0001004u)

#endif /* SS_REGS_H */
