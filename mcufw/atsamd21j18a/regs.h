/* Microchip ATSAMD21J18A registers, by hand, and the accelerator-word packing.
 *
 * NO VENDOR HEADERS, same as the other parts here. Every address and every
 * bit position below was read out of one of two places and nowhere else:
 *
 *   rigs/fw_samd21_aes/main.c          the flash-timing rig for this die
 *   rigs/fw_samd21_sbsfu/              its SBSFU port, and
 *   rigs/cmsis/samd21/                 Microchip's device and component
 *                                      headers (Keil.SAMD21_DFP 1.3.2)
 *
 * both in the flash-accelerator-tests tree. Nothing here is from memory and
 * nothing here is inferred from another part in the family.
 *
 * THE ACCELERATOR ON THIS PART IS NVMCTRL.CTRLB, AND IT IS THREE FIELDS.
 * There is no FLASH_ACR analogue, no prefetch enable and no separate
 * instruction-cache enable: the SAM D21 has ONE 64-byte unified cache on the
 * NVM read path, and the whole read-path configuration is
 *
 *   CTRLB.RWS      [4:1]    wait states, 0..15
 *   CTRLB.READMODE [17:16]  how a cache MISS is priced
 *                             0 NO_MISS_PENALTY  (reset state)
 *                             1 LOW_POWER        inserts a wait state on miss
 *                             2 DETERMINISTIC    hit and miss cost the same
 *                             3 reserved
 *   CTRLB.CACHEDIS [18]     cache off
 *
 * so the one opaque word ss_board.h asks for is PACKED here, which is the
 * escape hatch that contract documents:
 *
 *     bits 0..3   RWS, 0..15
 *     bits 8..9   READMODE, 0..2
 *     bit  16     CACHEDIS
 *
 * The packing is deliberately NOT CTRLB's own bit layout. CTRLB also carries
 * MANW and SLEEPPRM, which are a write-path and a sleep-path control and
 * have no business in a word that labels a timing capture; a word that looked
 * like CTRLB but was not CTRLB would be the worse of the two confusions.
 *
 * The host never interprets it. It round-trips through SS_V_SET_ACR and lands
 * in the capture's metadata, so a trace count is attributable to the
 * configuration that actually ran. ssb_acr_get reads CTRLB back and rebuilds
 * the word from it, never from what was requested, which is what makes a
 * refused configuration visible instead of recorded under the name it was
 * asked for.
 *
 * WHAT IS DELIBERATELY LEFT OUT, AND WHY.
 *
 *   SLEEPPRM. It selects NVM power reduction during sleep. The victim never
 *   sleeps, so it cannot reach the measured path. The rig preserves it across
 *   a read-modify-write of CTRLB and so does this file, but it is not in the
 *   word.
 *
 *   MANW. Manual write. Write path only, and this image never writes flash.
 *
 *   A prefetch bit. There is none. The SAM D21 NVMCTRL has no prefetch enable
 *   anywhere in its register map.
 *
 *   A separate instruction-cache enable. There is none either. The 64 bytes
 *   are unified and CACHEDIS is the only switch, which is a property of the
 *   part rather than an omission here.
 *
 * THE CLOCK IS NOT IN THE WORD, and that is a choice worth naming. RWS is
 * only meaningful against a CPU frequency, and this port fixes the frequency
 * at build time (see board.c), so the word says what the read path was doing
 * and SS_V_IDENT's separate clock field says at what rate.
 */
#ifndef SS_REGS_H
#define SS_REGS_H

#include <stdint.h>

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define REG16(a) (*(volatile uint16_t *)(uintptr_t)(a))
#define REG8(a)  (*(volatile uint8_t  *)(uintptr_t)(a))

/* ---- packing of the opaque accelerator word --------------------------- */
#define D21_ACR_RWS_Pos        0u
#define D21_ACR_RWS_Msk        0x0000000Fu
#define D21_ACR_READMODE_Pos   8u
#define D21_ACR_READMODE_Msk   0x00000300u
#define D21_ACR_CACHEDIS       0x00010000u

/* ---- Cortex-M0+ core (ARMv6-M) ----------------------------------------
 * SysTick at SCS_BASE + 0x10 and SCB at SCS_BASE + 0xD00, from
 * rigs/cmsis/core_cm0plus.h. There is NO DWT on this core: see board.c.
 */
#define SYST_CSR   REG32(0xE000E010u)   /* ENABLE | TICKINT | CLKSOURCE      */
#define SYST_RVR   REG32(0xE000E014u)   /* reload                            */
#define SYST_CVR   REG32(0xE000E018u)   /* current, 24-bit, counts DOWN      */
#define SCB_CPUID  REG32(0xE000ED00u)
#define SCB_VTOR   REG32(0xE000ED08u)

static inline void irq_off(void) { __asm volatile("cpsid i" ::: "memory"); }
static inline void irq_on(void)  { __asm volatile("cpsie i" ::: "memory"); }

/* ---- PM, 0x40000400 ---------------------------------------------------- */
#define PM_CPUSEL     REG8 (0x40000408u)   /* CPUDIV[2:0], 0 = /1            */
#define PM_APBCMASK   REG32(0x40000420u)
#define PM_APBCMASK_SERCOM3  (1u << 5)

/* ---- SYSCTRL, 0x40000800 ----------------------------------------------- */
#define SYSCTRL_PCLKSR   REG32(0x4000080Cu)
#define SYSCTRL_OSC8M    REG32(0x40000820u)   /* [1] ENABLE [9:8] PRESC      */
#define SYSCTRL_DFLLCTRL REG16(0x40000824u)   /* [1] ENABLE [2] MODE         */
#define SYSCTRL_DFLLVAL  REG32(0x40000828u)   /* [9:0] FINE [15:10] COARSE   */
#define PCLKSR_OSC8MRDY  (1u << 3)
#define PCLKSR_DFLLRDY   (1u << 4)
#define OSC8M_ENABLE     (1u << 1)
#define OSC8M_PRESC_Msk  (3u << 8)
#define DFLLCTRL_ENABLE  (1u << 1)

/* ---- GCLK, 0x40000C00 -------------------------------------------------- */
#define GCLK_STATUS   REG8 (0x40000C01u)
#define GCLK_CLKCTRL  REG16(0x40000C02u)
#define GCLK_GENCTRL  REG32(0x40000C04u)
#define GCLK_GENDIV   REG32(0x40000C08u)
#define GCLK_SYNCBUSY     (1u << 7)
#define GCLK_SRC_OSC8M    6u
#define GCLK_SRC_DFLL48M  7u
#define GENCTRL_SRC_Pos   8u
#define GENCTRL_GENEN     (1u << 16)
#define CLKCTRL_GEN_Pos   8u
#define CLKCTRL_CLKEN     (1u << 14)
#define CLKCTRL_ID_SERCOM3_CORE  0x17u

/* ---- NVMCTRL, 0x41004000: the accelerator ------------------------------ */
#define NVM_CTRLA    REG16(0x41004000u)
#define NVM_CTRLB    REG32(0x41004004u)
#define NVM_INTFLAG  REG8 (0x41004014u)
#define CTRLB_RWS_Pos        1u
#define CTRLB_RWS_Msk        (0xFu << 1)
#define CTRLB_SLEEPPRM_Msk   (3u << 8)
#define CTRLB_READMODE_Pos   16u
#define CTRLB_READMODE_Msk   (3u << 16)
#define CTRLB_CACHEDIS       (1u << 18)
#define NVM_CMD_INVALL       0x46u
#define NVM_CMDEX_KEY        0xA5u    /* CTRLA.CMDEX, bits 15:8             */
#define NVM_INTFLAG_READY    (1u << 0)

/* READMODE, by the SVD's own names. The descriptions are the vendor's claim
 * and research/targets/samd21j18a.md is what tested them. */
#define D21_READMODE_NO_MISS_PENALTY  0u
#define D21_READMODE_LOW_POWER        1u
#define D21_READMODE_DETERMINISTIC    2u

/* Factory DFLL48M coarse calibration: NVM software calibration area word 1,
 * bits 31:26. Read-only, at 0x00806024, outside the 256 KB main array. */
#define NVM_CAL_WORD1  REG32(0x00806024u)

/* ---- DSU, 0x41002000 --------------------------------------------------- */
#define DSU_DID  REG32(0x41002018u)

/* ---- PORT, 0x41004400, two groups 0x80 apart --------------------------- */
#define PORT_GROUP(g)      (0x41004400u + 0x80u * (g))
#define PORT_DIRSET(g)     REG32(PORT_GROUP(g) + 0x08u)
#define PORT_OUTCLR(g)     REG32(PORT_GROUP(g) + 0x14u)
#define PORT_OUTSET(g)     REG32(PORT_GROUP(g) + 0x18u)
#define PORT_PMUX(g, n)    REG8 (PORT_GROUP(g) + 0x30u + (n))
#define PORT_PINCFG(g, n)  REG8 (PORT_GROUP(g) + 0x40u + (n))
#define PINCFG_PMUXEN  (1u << 0)
#define PINCFG_INEN    (1u << 1)
#define PMUX_C         2u

/* ---- SERCOM3, 0x42001400, in USART mode -------------------------------- */
#define SERCOM3_CTRLA     REG32(0x42001400u)
#define SERCOM3_CTRLB     REG32(0x42001404u)
#define SERCOM3_BAUD      REG16(0x4200140Cu)
#define SERCOM3_INTFLAG   REG8 (0x42001418u)
#define SERCOM3_STATUS    REG16(0x4200141Au)
#define SERCOM3_SYNCBUSY  REG32(0x4200141Cu)
#define SERCOM3_DATA      REG16(0x42001428u)
#define USART_CTRLA_SWRST     (1u << 0)
#define USART_CTRLA_ENABLE    (1u << 1)
#define USART_CTRLA_MODE_INT_CLK (1u << 2)   /* MODE = 1, internal clock     */
#define USART_CTRLA_TXPO_Pos  16u
#define USART_CTRLA_RXPO_Pos  20u
#define USART_CTRLA_DORD      (1u << 30)     /* LSB first                    */
#define USART_CTRLB_TXEN      (1u << 16)
#define USART_CTRLB_RXEN      (1u << 17)
#define USART_INTFLAG_DRE     (1u << 0)
#define USART_INTFLAG_TXC     (1u << 1)
#define USART_INTFLAG_RXC     (1u << 2)
#define USART_SYNCBUSY_SWRST  (1u << 0)
#define USART_SYNCBUSY_ENABLE (1u << 1)
#define USART_SYNCBUSY_CTRLB  (1u << 2)
#define USART_STATUS_PERR     (1u << 0)
#define USART_STATUS_FERR     (1u << 1)
#define USART_STATUS_BUFOVF   (1u << 2)

#endif /* SS_REGS_H */
