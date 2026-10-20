/* STM32L552ZE registers, by hand, and the accelerator-word packing.
 *
 * Every address and bit position below was read out of the in-tree CMSIS
 * header of the research repository,
 * flash-accelerator-tests/rigs/cmsis/stm32l552xx.h (and core_cm33.h for
 * the core block), and every peripheral used here is one the L5 rigs in
 * that repository already drive on this die - rigs/fw_l5_arb/board.c,
 * rigs/fw_l5_cache/main.c and rigs/fw_map_l5/main.c. Nothing here is from
 * a datasheet row that was not also seen in one of those two places.
 *
 * THE ACCELERATOR ON THIS PART IS NOT FLASH_ACR. That register carries
 * LATENCY and LVEN and nothing else: no PRFTEN, no ICEN, no DCEN. All
 * flash acceleration is the separate ICACHE peripheral, and that cache is
 * UNIFIED - on Cortex-M33 the whole code region is reached over C-AHB, so
 * instruction fetch and flash data loads both cross it. So the one opaque
 * word ss_board.h asks for is PACKED here, which is the escape hatch that
 * contract documents for exactly this case.
 *
 * THE PACKING, WHICH IS DELIBERATELY THE RIG'S:
 *
 *     bits 3:0   FLASH_ACR.LATENCY, 0..15
 *     bit  8     ICACHE_CR.EN
 *     bit  9     ICACHE_CR.WAYSEL   (0 = 1-way / direct, 1 = 2-way)
 *
 * It is byte-for-byte the packing rigs/fw_l5_arb/board.c uses, so a
 * configuration label here means the same thing as the same label in the
 * research tree's L552 captures. Changing it would make two sets of
 * numbers about one part incomparable for no gain.
 *
 * Bits 4..7 and 10..31 are NOT part of the word. ssb_acr_set refuses a
 * value that sets any of them rather than discarding them quietly, so a
 * host typo comes back as a refusal through the SS_V_SET_ACR readback
 * instead of being recorded under a name the device never ran.
 *
 * WAYSEL IS IN THE WORD BECAUSE IT REACHES THE DATA PATH. It is not a
 * size knob: the research tree measured that the cache keeps all 8 KB in
 * both settings and WAYSEL re-INDEXES it, 512 sets at 1-way against 256
 * sets at 2-way (flash-accelerator-tests/research/targets/stm32l552.md
 * section 3b, counted with the hardware miss counter rather than inferred
 * from timing). Which lines a table and the code around it collide in is
 * therefore a function of WAYSEL, which makes it an axis of the
 * measurement and not a detail of bring-up.
 *
 * CACHEINV, HITMRST and MISSMRST are not in the word either, and that is
 * a different reason: they are pulses, not states. They are part of the
 * sequence ssb_acr_set performs, and a host cannot ask for one.
 */
#ifndef SS_REGS_H
#define SS_REGS_H

#include <stdint.h>

/* ---- packing of the opaque accelerator word --------------------------- */
#define L5_ACR_LATENCY_Msk   0x0000000Fu
#define L5_ACR_ICEN          0x00000100u
#define L5_ACR_WAYSEL        0x00000200u
#define L5_ACR_KNOWN_Msk     (L5_ACR_LATENCY_Msk | L5_ACR_ICEN | L5_ACR_WAYSEL)

/* ---- flash ------------------------------------------------------------
 * FLASH_R_BASE = AHB1PERIPH (0x40020000) + 0x2000. ACR is at offset 0.
 * LVEN is defined for completeness; nothing here writes it. */
#define FLASH_BASE_REG    0x40022000u
#define FLASH_ACR         (*(volatile uint32_t *)(FLASH_BASE_REG + 0x00u))
#define FLASH_ACR_LATENCY_Msk 0x0000000Fu
#define FLASH_ACR_LVEN        (1u << 15)

/* ---- ICACHE: the whole accelerator -----------------------------------
 * ICACHE_BASE = AHB1PERIPH (0x40020000) + 0x10400. */
#define ICACHE_BASE_REG   0x40030400u
#define ICACHE_CR         (*(volatile uint32_t *)(ICACHE_BASE_REG + 0x00u))
#define ICACHE_SR         (*(volatile uint32_t *)(ICACHE_BASE_REG + 0x04u))
#define ICACHE_FCR        (*(volatile uint32_t *)(ICACHE_BASE_REG + 0x0Cu))
#define ICACHE_HMONR      (*(volatile uint32_t *)(ICACHE_BASE_REG + 0x10u))
#define ICACHE_MMONR      (*(volatile uint32_t *)(ICACHE_BASE_REG + 0x14u))

#define ICACHE_CR_EN       (1u << 0)
#define ICACHE_CR_CACHEINV (1u << 1)
#define ICACHE_CR_WAYSEL   (1u << 2)
#define ICACHE_CR_HITMEN   (1u << 16)
#define ICACHE_CR_MISSMEN  (1u << 17)
#define ICACHE_CR_HITMRST  (1u << 18)
#define ICACHE_CR_MISSMRST (1u << 19)
#define ICACHE_SR_BUSYF    (1u << 0)
#define ICACHE_SR_BSYENDF  (1u << 1)
#define ICACHE_FCR_CBSYENDF (1u << 1)

/* ---- RCC -------------------------------------------------------------- */
#define RCC_BASE_REG      0x40021000u
#define RCC_CR            (*(volatile uint32_t *)(RCC_BASE_REG + 0x00u))
#define RCC_CFGR          (*(volatile uint32_t *)(RCC_BASE_REG + 0x08u))
#define RCC_AHB2ENR       (*(volatile uint32_t *)(RCC_BASE_REG + 0x4Cu))
#define RCC_APB1ENR1      (*(volatile uint32_t *)(RCC_BASE_REG + 0x58u))
#define RCC_APB1ENR2      (*(volatile uint32_t *)(RCC_BASE_REG + 0x5Cu))
#define RCC_CCIPR1        (*(volatile uint32_t *)(RCC_BASE_REG + 0x88u))

#define RCC_CR_HSION      (1u << 8)
#define RCC_CR_HSIRDY     (1u << 10)
#define RCC_CFGR_SW_Pos   0u
#define RCC_CFGR_SW_Msk   (3u << RCC_CFGR_SW_Pos)
#define RCC_CFGR_SWS_Pos  2u
#define RCC_CFGR_SWS_Msk  (3u << RCC_CFGR_SWS_Pos)
#define RCC_CFGR_SW_HSI16 1u                      /* SW = 01 selects HSI16 */
#define RCC_AHB2ENR_GPIOBEN (1u << 1)
#define RCC_AHB2ENR_GPIOGEN (1u << 6)
#define RCC_APB1ENR1_PWREN  (1u << 28)
#define RCC_APB1ENR2_LPUART1EN (1u << 0)
#define RCC_CCIPR1_LPUART1SEL_Pos 10u
#define RCC_CCIPR1_LPUART1SEL_Msk (3u << RCC_CCIPR1_LPUART1SEL_Pos)
#define RCC_CCIPR1_LPUART1SEL_HSI16 2u

/* ---- PWR: port G is dead without PWR_CR2.IOSV ------------------------- */
#define PWR_BASE_REG      0x40007000u
#define PWR_CR2           (*(volatile uint32_t *)(PWR_BASE_REG + 0x04u))
#define PWR_CR2_IOSV      (1u << 9)

/* ---- GPIO: AHB2PERIPH is 0x42020000 on this part, not 0x48000000 ------
 * Port G carries the virtual COM port (PG7/PG8, AF8). Port B carries the
 * trigger; see board.c for what is and is not established about that pin. */
#define GPIOB_BASE_REG    0x42020400u
#define GPIOB_MODER       (*(volatile uint32_t *)(GPIOB_BASE_REG + 0x00u))
#define GPIOB_BSRR        (*(volatile uint32_t *)(GPIOB_BASE_REG + 0x18u))

#define GPIOG_BASE_REG    0x42021800u
#define GPIOG_MODER       (*(volatile uint32_t *)(GPIOG_BASE_REG + 0x00u))
#define GPIOG_OSPEEDR     (*(volatile uint32_t *)(GPIOG_BASE_REG + 0x08u))
#define GPIOG_PUPDR       (*(volatile uint32_t *)(GPIOG_BASE_REG + 0x0Cu))
#define GPIOG_AFRL        (*(volatile uint32_t *)(GPIOG_BASE_REG + 0x20u))
#define GPIOG_AFRH        (*(volatile uint32_t *)(GPIOG_BASE_REG + 0x24u))

/* ---- LPUART1: the Nucleo-144 VCP on this board ------------------------ */
#define LPUART1_BASE_REG  0x40008000u
#define LPUART1_CR1       (*(volatile uint32_t *)(LPUART1_BASE_REG + 0x00u))
#define LPUART1_CR2       (*(volatile uint32_t *)(LPUART1_BASE_REG + 0x04u))
#define LPUART1_CR3       (*(volatile uint32_t *)(LPUART1_BASE_REG + 0x08u))
#define LPUART1_BRR       (*(volatile uint32_t *)(LPUART1_BASE_REG + 0x0Cu))
#define LPUART1_ISR       (*(volatile uint32_t *)(LPUART1_BASE_REG + 0x1Cu))
#define LPUART1_RDR       (*(volatile uint32_t *)(LPUART1_BASE_REG + 0x24u))
#define LPUART1_TDR       (*(volatile uint32_t *)(LPUART1_BASE_REG + 0x28u))
#define USART_CR1_UE      (1u << 0)
#define USART_CR1_RE      (1u << 2)
#define USART_CR1_TE      (1u << 3)
#define USART_ISR_RXNE    (1u << 5)
#define USART_ISR_TXE     (1u << 7)

/* ---- core -------------------------------------------------------------
 * ARMv8-M main: SCB at SCS + 0xD00, DCB (what ARMv7-M called CoreDebug) at
 * 0xE000EDF0 with DEMCR four bytes in. The DWT here has NO lock access
 * register - core_cm33.h's DWT_Type has a read-only LSR at 0xFB4 and no
 * LAR - so the M7 ports' DWT_LAR unlock has no counterpart on this part
 * and writing one would be inventing a register. */
#define SCB_VTOR          (*(volatile uint32_t *)0xE000ED08u)
#define SCB_CPACR         (*(volatile uint32_t *)0xE000ED88u)
#define SCB_CPACR_CP10_CP11_FULL (0xFu << 20)

#define SYST_CSR          (*(volatile uint32_t *)0xE000E010u)
#define SYST_RVR          (*(volatile uint32_t *)0xE000E014u)
#define SYST_CVR          (*(volatile uint32_t *)0xE000E018u)

#define DEMCR             (*(volatile uint32_t *)0xE000EDFCu)
#define DEMCR_TRCENA      (1u << 24)
#define DWT_CTRL          (*(volatile uint32_t *)0xE0001000u)
#define DWT_CTRL_CYCCNTENA (1u << 0)
#define DWT_CYCCNT        (*(volatile uint32_t *)0xE0001004u)

#endif /* SS_REGS_H */
