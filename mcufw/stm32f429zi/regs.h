/* Register definitions for STM32F429ZI (Cortex-M4F), written by hand.
 *
 * Same reasoning as the other parts here: this board file touches about
 * twenty-five registers, and writing them out with their reference-manual
 * addresses is smaller than vendoring a CMSIS tree and leaves nothing to
 * keep in sync. Everything is from RM0090 rev 21 and the ARMv7-M
 * architecture reference. A part that wants a vendor HAL may bring one -
 * the board contract says nothing about how a board file gets its
 * registers. See docs/PORTING.md.
 */
#ifndef STM32F429ZI_REGS_H
#define STM32F429ZI_REGS_H

#include <stdint.h>

#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))

/* ---- Cortex-M4 core (ARMv7-M) ---- */
#define SYST_CSR   REG(0xE000E010u)      /* SysTick control/status            */
#define SYST_RVR   REG(0xE000E014u)      /* reload value                      */
#define SYST_CVR   REG(0xE000E018u)      /* current value (24-bit, counts DOWN)*/
#define SCB_VTOR   REG(0xE000ED08u)
#define SCB_AIRCR  REG(0xE000ED0Cu)
#define SCB_CCR    REG(0xE000ED14u)

/* The DWT cycle counter. Counts UP over a full 32 bits, unlike SysTick -
 * which is the reason ssb_cyc_delta is a board hook and not an assumption.
 *
 * TWO SILENT FAILURES, both of which look like a constant-time result:
 *   * DEMCR.TRCENA is cleared by some debug probes when they DISCONNECT
 *     after flashing, so arming it once at boot can yield zero for every
 *     window. It is armed where it is used.
 *   * CYCCNT can stop while CYCCNTENA still reads set. ssb_init checks
 *     that it actually moves before reporting that it has one.
 */
#define DEMCR      REG(0xE000EDFCu)
#define DEMCR_TRCENA   (1u << 24)
#define DWT_CTRL   REG(0xE0001000u)
#define DWT_CYCCNT REG(0xE0001004u)
#define DWT_CTRL_CYCCNTENA (1u << 0)

#define SYSRESETREQ 0x05FA0004u

static inline void irq_off(void) { __asm volatile("cpsid i" ::: "memory"); }
static inline void irq_on(void)  { __asm volatile("cpsie i" ::: "memory"); }

/* ---- RCC (0x40023800) ---- */
#define RCC_CR       REG(0x40023800u)
#define RCC_PLLCFGR  REG(0x40023804u)
#define RCC_CFGR     REG(0x40023808u)
#define RCC_AHB1ENR  REG(0x40023830u)
#define RCC_APB1ENR  REG(0x40023840u)
#define RCC_APB2ENR  REG(0x40023844u)

#define RCC_CR_HSION   (1u << 0)
#define RCC_CR_HSIRDY  (1u << 1)
#define RCC_AHB1ENR_GPIOB (1u << 1)
#define RCC_AHB1ENR_GPIOD (1u << 3)
#define RCC_APB1ENR_USART3 (1u << 18)

/* ---- FLASH (0x40023C00). THE ART ACCELERATOR. ----
 * RM0090 3.5.1. Unlike a part with a single LATENCY bit, this one has a
 * four-bit field AND two caches with explicit reset bits - and the reset
 * bits may only be written while the corresponding cache is DISABLED.
 * Writing the enable bit without the reset dance leaves stale lines, which
 * is a measurement rather than an error. */
#define FLASH_ACR REG(0x40023C00u)
#define FLASH_ACR_LATENCY_Msk 0xFu
#define FLASH_ACR_PRFTEN  (1u << 8)
#define FLASH_ACR_ICEN    (1u << 9)
#define FLASH_ACR_DCEN    (1u << 10)
#define FLASH_ACR_ICRST   (1u << 11)
#define FLASH_ACR_DCRST   (1u << 12)

/* ---- GPIOB (0x40020400) and GPIOD (0x40020C00) ---- */
#define GPIOB_MODER   REG(0x40020400u)
#define GPIOB_OSPEEDR REG(0x40020408u)
#define GPIOB_BSRR    REG(0x40020418u)

#define GPIOD_MODER   REG(0x40020C00u)
#define GPIOD_OSPEEDR REG(0x40020C08u)
#define GPIOD_PUPDR   REG(0x40020C0Cu)
#define GPIOD_AFRH    REG(0x40020C24u)   /* alternate function, pins 8..15 */

/* ---- USART3 (0x40004800), classic SR/DR/BRR/CR1 layout ---- */
#define USART3_SR  REG(0x40004800u)
#define USART3_DR  REG(0x40004804u)
#define USART3_BRR REG(0x40004808u)
#define USART3_CR1 REG(0x4000480Cu)
#define USART_SR_TXE  (1u << 7)
#define USART_SR_RXNE (1u << 5)
#define USART_CR1_RE  (1u << 2)
#define USART_CR1_TE  (1u << 3)
#define USART_CR1_UE  (1u << 13)

#endif /* STM32F429ZI_REGS_H */
