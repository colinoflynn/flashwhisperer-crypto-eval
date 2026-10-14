/* STM32G474RE registers, by hand.
 *
 * The accelerator word is plain FLASH_ACR, with the same LATENCY /
 * PRFTEN / ICEN / DCEN / ICRST / DCRST layout as both F4 parts here -
 * verified against ST's own CMSIS header for this part rather than
 * assumed from the family - so no packing is needed.
 *
 * Everything else moves. The G4 puts RCC at 0x40021000 and FLASH at
 * 0x40022000 rather than the F4's 0x40023800 / 0x40023C00, GPIO is on
 * AHB2 at 0x48000000, and the enable registers are APB1ENR1 and AHB2ENR
 * rather than APB1ENR and AHB1ENR. The USART is the modern ISR/TDR/RDR
 * block and has a KERNEL CLOCK SELECT, which the F4's does not.
 */
#ifndef SS_REGS_H
#define SS_REGS_H

#include <stdint.h>

/* ---- flash ------------------------------------------------------------ */
#define FLASH_BASE_REG    0x40022000u
#define FLASH_ACR         (*(volatile uint32_t *)(FLASH_BASE_REG + 0x00u))
#define FLASH_ACR_LATENCY_Msk 0x0000000Fu
#define FLASH_ACR_PRFTEN      (1u << 8)
#define FLASH_ACR_ICEN        (1u << 9)
#define FLASH_ACR_DCEN        (1u << 10)
#define FLASH_ACR_ICRST       (1u << 11)
#define FLASH_ACR_DCRST       (1u << 12)

/* ---- RCC -------------------------------------------------------------- */
#define RCC_BASE_REG      0x40021000u
#define RCC_CR            (*(volatile uint32_t *)(RCC_BASE_REG + 0x00u))
#define RCC_CFGR          (*(volatile uint32_t *)(RCC_BASE_REG + 0x08u))
#define RCC_AHB2ENR       (*(volatile uint32_t *)(RCC_BASE_REG + 0x4Cu))
#define RCC_APB1ENR1      (*(volatile uint32_t *)(RCC_BASE_REG + 0x58u))
#define RCC_CCIPR         (*(volatile uint32_t *)(RCC_BASE_REG + 0x88u))

#define RCC_CR_HSION      (1u << 8)        /* HSI16: bit 8, not bit 0 */
#define RCC_CR_HSIRDY     (1u << 10)       /* and ready is bit 10     */
#define RCC_AHB2ENR_GPIOA (1u << 0)
#define RCC_APB1ENR1_USART2 (1u << 17)
/* USART2SEL is CCIPR bits 3:2; 2 = HSI16. Pinning the UART to HSI16
 * keeps BRR independent of whatever the core clock is doing. */
#define RCC_CCIPR_USART2SEL_Msk (3u << 2)
#define RCC_CCIPR_USART2SEL_HSI (2u << 2)

/* ---- GPIOA on AHB2: USART2 on PA2/PA3 AF7, LD2 on PA5 ----------------- */
#define GPIOA_BASE_REG    0x48000000u
#define GPIOA_MODER       (*(volatile uint32_t *)(GPIOA_BASE_REG + 0x00u))
#define GPIOA_OSPEEDR     (*(volatile uint32_t *)(GPIOA_BASE_REG + 0x08u))
#define GPIOA_PUPDR       (*(volatile uint32_t *)(GPIOA_BASE_REG + 0x0Cu))
#define GPIOA_BSRR        (*(volatile uint32_t *)(GPIOA_BASE_REG + 0x18u))
#define GPIOA_AFRL        (*(volatile uint32_t *)(GPIOA_BASE_REG + 0x20u))

/* ---- USART2: the modern ISR/TDR/RDR interface ------------------------- */
#define USART2_BASE_REG   0x40004400u
#define USART2_CR1        (*(volatile uint32_t *)(USART2_BASE_REG + 0x00u))
#define USART2_CR2        (*(volatile uint32_t *)(USART2_BASE_REG + 0x04u))
#define USART2_CR3        (*(volatile uint32_t *)(USART2_BASE_REG + 0x08u))
#define USART2_BRR        (*(volatile uint32_t *)(USART2_BASE_REG + 0x0Cu))
#define USART2_ISR        (*(volatile uint32_t *)(USART2_BASE_REG + 0x1Cu))
#define USART2_ICR        (*(volatile uint32_t *)(USART2_BASE_REG + 0x20u))
#define USART2_RDR        (*(volatile uint32_t *)(USART2_BASE_REG + 0x24u))
#define USART2_TDR        (*(volatile uint32_t *)(USART2_BASE_REG + 0x28u))
#define USART_CR1_UE      (1u << 0)
#define USART_CR1_RE      (1u << 2)
#define USART_CR1_TE      (1u << 3)
#define USART_ISR_ORE     (1u << 3)
#define USART_ISR_RXNE    (1u << 5)
#define USART_ISR_TXE     (1u << 7)

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
