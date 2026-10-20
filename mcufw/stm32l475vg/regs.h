/* STM32L475VG registers, by hand.
 *
 * Every address and bit position below came from one of two places and
 * nowhere else: the in-tree CMSIS header of the research repository,
 * flash-accelerator-tests/rigs/cmsis/stm32l475xx.h, and
 * rigs/fw_l4_aes/main.c, which is the rig that has been run on this die.
 * Where a peripheral is driven differently here than in that rig it says
 * so at the line.
 *
 * THE ACCELERATOR HERE IS PLAIN FLASH_ACR AND NEEDS NO PACKING. LATENCY,
 * PRFTEN, ICEN, DCEN and the two reset pulses are the F4/G4 layout
 * bit-for-bit - the research tree's dossier says so in terms, which is
 * why the G474's whole ICRST/DCRST dance ports over unchanged and why an
 * ACR value here means what the same value means on that part. There is
 * no ICACHE peripheral as on the L552, and no Cortex-M7 L1 as on the
 * F746/H723: this core has no L1 cache at all, so FLASH_ACR is the whole
 * data-side accelerator surface.
 *
 * TWO THINGS THAT ARE NOT THE G474'S, EITHER OF WHICH WOULD COMPILE AND
 * THEN HANG:
 *
 *   * LATENCY IS THREE BITS WIDE, not four. The mask is 0x7 and only 0..4
 *     are meaningful, so a host asking for LATENCY 8 does not get 8 and
 *     does not get an error either - the field TRUNCATES and the device
 *     quietly runs at 0 while the capture records 8. ssb_acr_set refuses
 *     the whole word instead; see board.c.
 *   * HSI16 IS OFF AT RESET on this family and the core comes up on MSI
 *     at 4 MHz, where the F4/G4/L5 parts all reset onto a 16 MHz HSI. So
 *     clock_init has to start HSI16 AND switch SW, and switching SW
 *     without starting it first hangs on the SWS poll forever.
 *
 * WHY PWR_CR1.VOS IS DEFINED HERE BUT IS NOT IN THE ACCELERATOR WORD.
 * On this part the core voltage range is a flash-path knob rather than a
 * power setting - Range 2 changes the wait states the array needs - and
 * it is the one axis of this kind no sibling part here has. It is left
 * out of the word for two reasons, both recorded rather than assumed:
 *
 *   1. IT IS A MEASURED NULL. The research tree swept it and every
 *      Range 2 cell reproduces its Range 1 twin to 14 significant
 *      figures, at all five wait states and all eight accelerator
 *      settings, with the register readback confirming the field
 *      actually changed (research/targets/stm32l475.md section 4). A
 *      knob that cannot reach what is being measured is left out with
 *      the reason recorded, per docs/PORTING.md - it is not carried as a
 *      sweep axis that costs cells and returns the same number.
 *   2. PUTTING IT IN THE WORD WOULD BREAK TRAP 1. The minimum legal
 *      LATENCY is a function of BOTH the clock and the range, so a
 *      refusal rule over a word containing VOS is two-dimensional, and
 *      the first thing a host would reach for - Range 2 at zero wait
 *      states, 16 MHz - is out of spec by RM0351. That same dossier
 *      section flags its own ws0/Range 2 cells as an out-of-spec
 *      combination that happens to work on this die, and says their
 *      agreement with Range 1 is not a specification claim. The board
 *      therefore PINS Range 1 at boot and enforces a one-dimensional
 *      rule it can actually guarantee.
 *
 * The definitions stay because board.c writes Range 1 explicitly rather
 * than inheriting it: the reset default is Range 1, and writing it
 * anyway is what makes the min-latency rule in ssb_acr_set true by
 * construction instead of true by assumption.
 */
#ifndef SS_REGS_H
#define SS_REGS_H

#include <stdint.h>

/* ---- flash ------------------------------------------------------------
 * FLASH_R_BASE = AHB1PERIPH (0x40020000) + 0x2000. ACR is at offset 0.
 *
 * LATENCY_Msk IS 0x7. The four-bit mask the F4/G4 ports use would let a
 * word through that this field cannot hold.
 *
 * FLASH_ACR RESETS TO 0x600 on this part - ICEN and DCEN set, prefetch
 * off, LATENCY 0 - which is measured, not read off a datasheet: the
 * research tree read it out of the live factory image's own registers,
 * and ST's own Cloud demo on this board goes further and runs 0x604.
 * The reset default is a configuration that leaks here, so it is in the
 * sweep list in board.mk rather than treated as a baseline.
 */
#define FLASH_BASE_REG    0x40022000u
#define FLASH_ACR         (*(volatile uint32_t *)(FLASH_BASE_REG + 0x00u))
#define FLASH_ACR_LATENCY_Msk 0x00000007u   /* THREE bits, 0..4 meaningful */
#define FLASH_ACR_PRFTEN      (1u << 8)
#define FLASH_ACR_ICEN        (1u << 9)
#define FLASH_ACR_DCEN        (1u << 10)
#define FLASH_ACR_ICRST       (1u << 11)
#define FLASH_ACR_DCRST       (1u << 12)
#define FLASH_ACR_RUN_PD      (1u << 13)
#define FLASH_ACR_SLEEP_PD    (1u << 14)

/* Everything a requested word is allowed to carry. The two reset pulses
 * are deliberately outside it: they are pulses rather than states, they
 * are part of the sequence ssb_acr_set performs, and a host cannot ask
 * for one. RUN_PD and SLEEP_PD are outside it too - the research tree
 * sources RUN_PD as reachable and explicitly does not sweep it, and the
 * cipher runs awake so SLEEP_PD cannot reach the measurement. */
#define FLASH_ACR_WRITABLE    (FLASH_ACR_LATENCY_Msk | FLASH_ACR_PRFTEN \
                               | FLASH_ACR_ICEN | FLASH_ACR_DCEN)
#define FLASH_ACR_LATENCY_MAX 4u

/* ---- RCC --------------------------------------------------------------
 * RCC_BASE = AHB1PERIPH (0x40020000) + 0x1000. Offsets read off the
 * RCC_TypeDef in the CMSIS header, which carries them in comments:
 * AHB2ENR 0x4C, APB1ENR1 0x58, APB2ENR 0x60, CCIPR 0x88.
 */
#define RCC_BASE_REG      0x40021000u
#define RCC_CR            (*(volatile uint32_t *)(RCC_BASE_REG + 0x00u))
#define RCC_CFGR          (*(volatile uint32_t *)(RCC_BASE_REG + 0x08u))
#define RCC_AHB2ENR       (*(volatile uint32_t *)(RCC_BASE_REG + 0x4Cu))
#define RCC_APB1ENR1      (*(volatile uint32_t *)(RCC_BASE_REG + 0x58u))
#define RCC_APB2ENR       (*(volatile uint32_t *)(RCC_BASE_REG + 0x60u))
#define RCC_CCIPR         (*(volatile uint32_t *)(RCC_BASE_REG + 0x88u))

/* MSION is bit 0 and is the source the part BOOTS on, at 4 MHz. HSI16 is
 * bit 8 and its ready flag is bit 10 - not bit 9, and not the L152's bit
 * 1. */
#define RCC_CR_MSION      (1u << 0)
#define RCC_CR_HSION      (1u << 8)
#define RCC_CR_HSIRDY     (1u << 10)

/* CFGR SW/SWS: 0 = MSI, 1 = HSI16, 2 = HSE, 3 = PLL. */
#define RCC_CFGR_SW_Msk   (3u << 0)
#define RCC_CFGR_SW_HSI16 (1u << 0)
#define RCC_CFGR_SWS_Msk  (3u << 2)
#define RCC_CFGR_SWS_HSI16 (1u << 2)

#define RCC_AHB2ENR_GPIOB   (1u << 1)     /* GPIOB, not GPIOA: see board.c */
#define RCC_APB2ENR_USART1  (1u << 14)
#define RCC_APB1ENR1_PWR    (1u << 28)

/* USART1SEL is CCIPR bits 1:0; 2 = HSI16. Pinning the UART's kernel
 * clock to HSI16 keeps BRR independent of the core clock, the same
 * reason the G474 port does it. */
#define RCC_CCIPR_USART1SEL_Msk (3u << 0)
#define RCC_CCIPR_USART1SEL_HSI (2u << 0)

/* ---- PWR --------------------------------------------------------------
 * PWR_BASE = APB1PERIPH (0x40000000) + 0x7000. CR1 at 0x00, SR2 at 0x14.
 * VOS is CR1 bits 10:9 (1 = Range 1, 2 = Range 2) and VOSF is SR2 bit
 * 10. Not an accelerator knob here - see the file header for why.
 */
#define PWR_BASE_REG      0x40007000u
#define PWR_CR1           (*(volatile uint32_t *)(PWR_BASE_REG + 0x00u))
#define PWR_SR2           (*(volatile uint32_t *)(PWR_BASE_REG + 0x14u))
#define PWR_CR1_VOS_Msk   (3u << 9)
#define PWR_CR1_VOS_RANGE1 (1u << 9)
#define PWR_SR2_VOSF      (1u << 10)      /* regulator still switching */

/* ---- GPIOB on AHB2: USART1 on PB6/PB7 AF7, LD2 on PB14 ----------------
 * GPIOB_BASE = AHB2PERIPH (0x48000000) + 0x400, so GPIOB and NOT the
 * GPIOA every Nucleo-64 port here uses. That is measured rather than
 * assumed: the research tree found GPIOA is not even clocked by this
 * board's factory image, so the PA5 that is the LED on a Nucleo is not
 * the LED here.
 */
#define GPIOB_BASE_REG    0x48000400u
#define GPIOB_MODER       (*(volatile uint32_t *)(GPIOB_BASE_REG + 0x00u))
#define GPIOB_OSPEEDR     (*(volatile uint32_t *)(GPIOB_BASE_REG + 0x08u))
#define GPIOB_PUPDR       (*(volatile uint32_t *)(GPIOB_BASE_REG + 0x0Cu))
#define GPIOB_BSRR        (*(volatile uint32_t *)(GPIOB_BASE_REG + 0x18u))
#define GPIOB_AFRL        (*(volatile uint32_t *)(GPIOB_BASE_REG + 0x20u))

/* ---- USART1: the modern ISR/TDR/RDR interface -------------------------
 * USART1_BASE = APB2PERIPH (0x40010000) + 0x3800. Same block as the G4's
 * USART2, on APB2 rather than APB1.
 */
#define USART1_BASE_REG   0x40013800u
#define USART1_CR1        (*(volatile uint32_t *)(USART1_BASE_REG + 0x00u))
#define USART1_CR2        (*(volatile uint32_t *)(USART1_BASE_REG + 0x04u))
#define USART1_CR3        (*(volatile uint32_t *)(USART1_BASE_REG + 0x08u))
#define USART1_BRR        (*(volatile uint32_t *)(USART1_BASE_REG + 0x0Cu))
#define USART1_ISR        (*(volatile uint32_t *)(USART1_BASE_REG + 0x1Cu))
#define USART1_ICR        (*(volatile uint32_t *)(USART1_BASE_REG + 0x20u))
#define USART1_RDR        (*(volatile uint32_t *)(USART1_BASE_REG + 0x24u))
#define USART1_TDR        (*(volatile uint32_t *)(USART1_BASE_REG + 0x28u))
#define USART_CR1_UE      (1u << 0)
#define USART_CR1_RE      (1u << 2)
#define USART_CR1_TE      (1u << 3)
#define USART_ISR_ORE     (1u << 3)
#define USART_ISR_RXNE    (1u << 5)
#define USART_ISR_TXE     (1u << 7)

/* ---- core -------------------------------------------------------------
 * Architectural Cortex-M4 addresses. This core has DWT, so TIMING=dwt is
 * a legitimate build; SysTick is the default because it is the
 * instrument the research tree's G474, L552 and L475 rigs all use and
 * this part's whole purpose is a comparison against those.
 */
#define SCB_VTOR          (*(volatile uint32_t *)0xE000ED08u)

/* CPACR, and full access for the two coprocessors that are the FPU. This
 * is a hard-float part and startup.c enables it before its copy loops;
 * see the comment there for the failure it prevents. */
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
