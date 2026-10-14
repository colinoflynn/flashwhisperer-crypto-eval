/* STM32F401RE registers, by hand.
 *
 * The accelerator word here is FLASH_ACR and nothing else, exactly as on
 * the F429 - LATENCY, PRFTEN, ICEN, DCEN, and the two reset pulses - so
 * this part needs no packing and the word the host sets IS the register.
 * The two M7 parts in this tree are the ones that had to pack.
 *
 * What differs from the F429 is the board around it rather than the
 * accelerator: a NUCLEO-64, so the virtual COM port is USART2 on PA2/PA3
 * instead of USART3 on PD8/PD9, and the user LED is PA5 rather than PB0.
 */
#ifndef SS_REGS_H
#define SS_REGS_H

#include <stdint.h>

/* ---- flash ------------------------------------------------------------ */
#define FLASH_BASE_REG    0x40023C00u
#define FLASH_ACR         (*(volatile uint32_t *)(FLASH_BASE_REG + 0x00u))
#define FLASH_ACR_LATENCY_Msk 0x0000000Fu
#define FLASH_ACR_PRFTEN      (1u << 8)
#define FLASH_ACR_ICEN        (1u << 9)
#define FLASH_ACR_DCEN        (1u << 10)
#define FLASH_ACR_ICRST       (1u << 11)
#define FLASH_ACR_DCRST       (1u << 12)

/* ---- RCC -------------------------------------------------------------- */
#define RCC_BASE_REG      0x40023800u
#define RCC_CR            (*(volatile uint32_t *)(RCC_BASE_REG + 0x00u))
#define RCC_CFGR          (*(volatile uint32_t *)(RCC_BASE_REG + 0x08u))
#define RCC_AHB1ENR       (*(volatile uint32_t *)(RCC_BASE_REG + 0x30u))
#define RCC_APB1ENR       (*(volatile uint32_t *)(RCC_BASE_REG + 0x40u))

#define RCC_CR_HSION      (1u << 0)
#define RCC_CR_HSIRDY     (1u << 1)
#define RCC_AHB1ENR_GPIOA (1u << 0)
#define RCC_APB1ENR_USART2 (1u << 17)

/* ---- GPIOA: USART2 on PA2/PA3 AF7, LD2 on PA5 ------------------------- */
#define GPIOA_BASE_REG    0x40020000u
#define GPIOA_MODER       (*(volatile uint32_t *)(GPIOA_BASE_REG + 0x00u))
#define GPIOA_OSPEEDR     (*(volatile uint32_t *)(GPIOA_BASE_REG + 0x08u))
#define GPIOA_PUPDR       (*(volatile uint32_t *)(GPIOA_BASE_REG + 0x0Cu))
#define GPIOA_BSRR        (*(volatile uint32_t *)(GPIOA_BASE_REG + 0x18u))
#define GPIOA_AFRL        (*(volatile uint32_t *)(GPIOA_BASE_REG + 0x20u))

/* ---- USART2: the older SR/DR interface, as on the F429 ---------------- */
#define USART2_BASE_REG   0x40004400u
#define USART2_SR         (*(volatile uint32_t *)(USART2_BASE_REG + 0x00u))
#define USART2_DR         (*(volatile uint32_t *)(USART2_BASE_REG + 0x04u))
#define USART2_BRR        (*(volatile uint32_t *)(USART2_BASE_REG + 0x08u))
#define USART2_CR1        (*(volatile uint32_t *)(USART2_BASE_REG + 0x0Cu))
#define USART_SR_RXNE     (1u << 5)
#define USART_SR_TXE      (1u << 7)
#define USART_CR1_RE      (1u << 2)
#define USART_CR1_TE      (1u << 3)
#define USART_CR1_UE      (1u << 13)

/* ---- core ------------------------------------------------------------- */
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
