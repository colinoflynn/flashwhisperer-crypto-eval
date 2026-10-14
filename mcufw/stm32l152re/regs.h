/* Register definitions for STM32L152RE (Cortex-M3), written by hand.
 *
 * NO VENDOR HEADERS, deliberately. This part needs about twenty registers,
 * and writing them out with their reference-manual addresses is smaller than
 * vendoring a CMSIS tree and a device header, and leaves nothing to keep in
 * sync with a datasheet. Everything here is from RM0038 rev 16 and the
 * ARMv7-M architecture reference, and every definition is used.
 *
 * A part that genuinely needs a vendor HAL may of course bring one - the
 * board contract says nothing about how the board file gets its registers.
 * See docs/PORTING.md.
 */
#ifndef STM32L152RE_REGS_H
#define STM32L152RE_REGS_H

#include <stdint.h>

#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))

/* ---- Cortex-M3 core (ARMv7-M) ---- */
#define SYST_CSR   REG(0xE000E010u)      /* SysTick control/status            */
#define SYST_RVR   REG(0xE000E014u)      /* reload value                      */
#define SYST_CVR   REG(0xE000E018u)      /* current value (24-bit, counts DOWN)*/
#define SCB_VTOR   REG(0xE000ED08u)
#define SCB_AIRCR  REG(0xE000ED0Cu)
#define SCB_CFSR   REG(0xE000ED28u)
#define SCB_HFSR   REG(0xE000ED2Cu)

#define SYSRESETREQ 0x05FA0004u          /* AIRCR key | SYSRESETREQ           */

static inline void irq_off(void) { __asm volatile("cpsid i" ::: "memory"); }
static inline void irq_on(void)  { __asm volatile("cpsie i" ::: "memory"); }
static inline void sysreset(void) {
    __asm volatile("dsb");
    SCB_AIRCR = SYSRESETREQ;
    __asm volatile("dsb");
    for (;;) {}
}

/* ---- RCC (0x40023800) ---- */
#define RCC_CR       REG(0x40023800u)
#define RCC_ICSCR    REG(0x40023804u)
#define RCC_CFGR     REG(0x40023808u)
#define RCC_AHBENR   REG(0x4002381Cu)
#define RCC_APB2ENR  REG(0x40023820u)
#define RCC_APB1ENR  REG(0x40023824u)

#define RCC_CR_HSION    (1u << 0)
#define RCC_CR_HSIRDY   (1u << 1)
#define RCC_CR_PLLON    (1u << 24)
#define RCC_CR_PLLRDY   (1u << 25)
#define RCC_AHBENR_GPIOA (1u << 0)
#define RCC_APB1ENR_USART2 (1u << 17)
#define RCC_APB1ENR_PWR    (1u << 28)

/* ---- PWR (0x40007000) ---- */
#define PWR_CR   REG(0x40007000u)
#define PWR_CSR  REG(0x40007004u)
#define PWR_CR_VOS_Msk   (3u << 11)
#define PWR_CR_VOS_RANGE1 (1u << 11)     /* 1.8 V core: 0 WS to 16 MHz, 1 WS to 32 */
#define PWR_CSR_VOSF     (1u << 4)       /* voltage scaling in progress            */

/* ---- FLASH (0x40023C00) ---- */
#define FLASH_ACR REG(0x40023C00u)
#define FLASH_ACR_LATENCY (1u << 0)      /* 0 or 1 wait states - that is the range */
#define FLASH_ACR_PRFTEN  (1u << 1)      /* prefetch; requires ACC64                */
#define FLASH_ACR_ACC64   (1u << 2)      /* 64-bit flash read access                */
#define FLASH_ACR_SLEEP_PD (1u << 3)
#define FLASH_ACR_RUN_PD  (1u << 4)

/* ---- GPIOA (0x40020000) ---- */
#define GPIOA_MODER   REG(0x40020000u)
#define GPIOA_OSPEEDR REG(0x40020008u)
#define GPIOA_PUPDR   REG(0x4002000Cu)
#define GPIOA_ODR     REG(0x40020014u)
#define GPIOA_AFRL    REG(0x40020020u)

/* ---- USART2 (0x40004400), classic SR/DR/BRR/CR1 layout ---- */
#define USART2_SR  REG(0x40004400u)
#define USART2_DR  REG(0x40004404u)
#define USART2_BRR REG(0x40004408u)
#define USART2_CR1 REG(0x4000440Cu)
#define USART_SR_TXE  (1u << 7)
#define USART_SR_TC   (1u << 6)      /* transmission complete: the last stop bit is out */
#define USART_SR_RXNE (1u << 5)
#define USART_CR1_RE  (1u << 2)
#define USART_CR1_TE  (1u << 3)
#define USART_CR1_UE  (1u << 13)

/* ---- GPIOA, the parts the trigger and the header UART need ---- */
#define GPIOA_BSRR  REG(0x40020018u)    /* atomic: set 1<<n, clear 1<<(n+16) */
#define GPIOA_AFRH  REG(0x40020024u)    /* alternate function, pins 8..15    */

/* ---- USART1 (0x40013800, APB2). Same SR/DR/BRR/CR1 layout as USART2. ---- */
#define USART1_SR   REG(0x40013800u)
#define USART1_DR   REG(0x40013804u)
#define USART1_BRR  REG(0x40013808u)
#define USART1_CR1  REG(0x4001380Cu)
#define RCC_APB2ENR_USART1 (1u << 14)

/* CRCEN is AHB bit 12. Needed by ST's STM32 Cryptographic Library - its
 * release notes say "CRC peripheral is used by STM32 cryptographic library",
 * and without the clock it runs, returns success, and computes the wrong
 * answer. That one line is the entire documentation of it. */
#define RCC_AHBENR_CRC     (1u << 12)

#define DBGMCU_IDCODE REG(0xE0042000u)

#endif /* STM32L152RE_REGS_H */
