/* STM32F746ZG registers, by hand, and the accelerator-word packing.
 *
 * This part is the hybrid of the other two and that is why it is worth
 * having. Unlike the H723 its FLASH_ACR DOES carry accelerator bits -
 * PRFTEN and ARTEN, in the F4 shape - and unlike the F429 its caches are
 * not in that register at all, because they are the Cortex-M7 L1 over in
 * the core's SCB. So the one opaque word has to span both:
 *
 *     bits 0..11  FLASH_ACR verbatim: LATENCY 0..15, PRFTEN bit 8,
 *                 ARTEN bit 9. ARTRST (bit 11) is REFUSED - see below.
 *     bit  16     SCB_CCR.IC   L1 instruction cache
 *     bit  17     SCB_CCR.DC   L1 data cache
 *
 * The low half is the register's own layout rather than a re-encoding, so
 * `0x307` here means exactly what it means in ST's and wolfSSL's own
 * board files: LATENCY 7 | PRFTEN | ARTEN. The vendor's configuration for
 * this family is that word with both L1 caches on, which is `0x30307`.
 *
 * ARTRST IS A PULSE, NOT A STATE, and it is writable only while ARTEN is
 * 0 (RM0385 3.4.2) - the same shape as the F4's ICRST/DCRST, where
 * clearing the enable bit alone left stale lines alive across a
 * reconfiguration. A caller asking for it to be LEFT set is asking for
 * something the register does not mean, so ssb_acr_set refuses the word
 * and pulses the bit itself where it is needed.
 *
 * EXPECT THE ART TO DO NOTHING HERE, and do not read that as a bug. It
 * accelerates the ITCM flash alias and this victim runs from the AXI
 * alias, so it cannot reach the data path. The research tree measured
 * that twice on this part - ACR 0x307 against 0x007 byte-identical in a
 * cipher scan, and no effect in 8 matched pairs of a structural knob map.
 * It is still in the word because it is a real bit of a real accelerator
 * and a capture should record what was set, not what someone expected to
 * matter. Unlike the H723's ART, this one can at least be read.
 */
#ifndef SS_REGS_H
#define SS_REGS_H

#include <stdint.h>

/* ---- packing of the opaque accelerator word --------------------------- */
#define F7_ACR_FLASH_Msk     0x00000FFFu   /* FLASH_ACR's own bits         */
#define F7_ACR_IC            0x00010000u
#define F7_ACR_DC            0x00020000u

/* ---- flash ------------------------------------------------------------ */
#define FLASH_BASE_REG    0x40023C00u
#define FLASH_ACR         (*(volatile uint32_t *)(FLASH_BASE_REG + 0x00u))
#define FLASH_ACR_LATENCY_Msk    0x0000000Fu
#define FLASH_ACR_PRFTEN         0x00000100u
#define FLASH_ACR_ARTEN          0x00000200u
#define FLASH_ACR_ARTRST         0x00000800u

/* ---- RCC -------------------------------------------------------------- */
#define RCC_BASE_REG      0x40023800u
#define RCC_CR            (*(volatile uint32_t *)(RCC_BASE_REG + 0x00u))
#define RCC_CFGR          (*(volatile uint32_t *)(RCC_BASE_REG + 0x08u))
#define RCC_AHB1ENR       (*(volatile uint32_t *)(RCC_BASE_REG + 0x30u))
#define RCC_APB1ENR       (*(volatile uint32_t *)(RCC_BASE_REG + 0x40u))

#define RCC_CR_HSION      (1u << 0)
/* HSIRDY is bit 1 on this family and bit 2 on the H7. Spinning on the
 * wrong one waits forever on a board that is already running. */
#define RCC_CR_HSIRDY     (1u << 1)
#define RCC_AHB1ENR_GPIOBEN  (1u << 1)
#define RCC_AHB1ENR_GPIODEN  (1u << 3)
#define RCC_APB1ENR_USART3EN (1u << 18)

/* ---- GPIO: USART3 on PD8/PD9 AF7, LD1 on PB0, as every Nucleo-144 ------ */
#define GPIOB_BASE_REG    0x40020400u
#define GPIOB_MODER       (*(volatile uint32_t *)(GPIOB_BASE_REG + 0x00u))
#define GPIOB_BSRR        (*(volatile uint32_t *)(GPIOB_BASE_REG + 0x18u))
#define GPIOD_BASE_REG    0x40020C00u
#define GPIOD_MODER       (*(volatile uint32_t *)(GPIOD_BASE_REG + 0x00u))
#define GPIOD_OSPEEDR     (*(volatile uint32_t *)(GPIOD_BASE_REG + 0x08u))
#define GPIOD_PUPDR       (*(volatile uint32_t *)(GPIOD_BASE_REG + 0x0Cu))
#define GPIOD_AFRH        (*(volatile uint32_t *)(GPIOD_BASE_REG + 0x24u))

/* ---- USART3: the modern ISR/TDR/RDR interface, as on the H7 ----------- */
#define USART3_BASE_REG   0x40004800u
#define USART3_CR1        (*(volatile uint32_t *)(USART3_BASE_REG + 0x00u))
#define USART3_CR2        (*(volatile uint32_t *)(USART3_BASE_REG + 0x04u))
#define USART3_CR3        (*(volatile uint32_t *)(USART3_BASE_REG + 0x08u))
#define USART3_BRR        (*(volatile uint32_t *)(USART3_BASE_REG + 0x0Cu))
#define USART3_ISR        (*(volatile uint32_t *)(USART3_BASE_REG + 0x1Cu))
#define USART3_ICR        (*(volatile uint32_t *)(USART3_BASE_REG + 0x20u))
#define USART3_RDR        (*(volatile uint32_t *)(USART3_BASE_REG + 0x24u))
#define USART3_TDR        (*(volatile uint32_t *)(USART3_BASE_REG + 0x28u))
#define USART_CR1_UE      (1u << 0)
#define USART_CR1_RE      (1u << 2)
#define USART_CR1_TE      (1u << 3)
#define USART_ISR_RXNE    (1u << 5)
#define USART_ISR_TXE     (1u << 7)
#define USART_ISR_ORE     (1u << 3)

/* ---- core: SCB, the L1 caches, SysTick, DWT --------------------------- */
#define SCB_VTOR          (*(volatile uint32_t *)0xE000ED08u)
#define SCB_CCR           (*(volatile uint32_t *)0xE000ED14u)
#define SCB_CCR_IC        (1u << 17)
#define SCB_CCR_DC        (1u << 16)
#define SCB_CCSIDR        (*(volatile uint32_t *)0xE000ED80u)
#define SCB_CSSELR        (*(volatile uint32_t *)0xE000ED84u)
#define SCB_ICIALLU       (*(volatile uint32_t *)0xE000EF50u)
#define SCB_DCISW         (*(volatile uint32_t *)0xE000EF60u)
#define SCB_DCCISW        (*(volatile uint32_t *)0xE000EF74u)

#define SYST_CSR          (*(volatile uint32_t *)0xE000E010u)
#define SYST_RVR          (*(volatile uint32_t *)0xE000E014u)
#define SYST_CVR          (*(volatile uint32_t *)0xE000E018u)

#define CORE_DEMCR        (*(volatile uint32_t *)0xE000EDFCu)
#define CORE_DEMCR_TRCENA (1u << 24)
#define DWT_LAR           (*(volatile uint32_t *)0xE0001FB0u)
#define DWT_LAR_KEY       0xC5ACCE55u
#define DWT_CTRL          (*(volatile uint32_t *)0xE0001000u)
#define DWT_CTRL_CYCCNTENA (1u << 0)
#define DWT_CYCCNT        (*(volatile uint32_t *)0xE0001004u)

#endif /* SS_REGS_H */
