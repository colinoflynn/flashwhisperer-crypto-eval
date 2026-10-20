/* STM32F303RE registers, by hand.
 *
 * Every address and bit position below was taken from one of two places
 * and nowhere else: rigs/fw_f303_aes/main.c in the research tree, which
 * is the rig that has been run on this part, and rigs/cmsis/stm32f303xc.h
 * for the two things the rig does not touch. Where a value came from the
 * CMSIS header rather than the rig it says so at the line.
 *
 * The accelerator word here is FLASH_ACR and nothing else, so this part
 * needs no packing - but it is NOT the F4's FLASH_ACR. It is the
 * smallest accelerator interface in this tree: LATENCY, HLFCYA, PRFTBE
 * and a read-only PRFTBS, with no ICEN, no DCEN and no ICRST/DCRST,
 * because the part has no instruction cache and no data cache at all.
 * Saying which registers are absent is a finding rather than an
 * omission, so the missing bits are listed below instead of left out.
 *
 * Two further differences from the F4 ports beside it, both of which
 * would compile and then address nothing if carried over: FLASH sits at
 * 0x40022000 and not 0x40023C00, and GPIOA is on AHB2 at 0x48000000 and
 * not on AHB1 at 0x40020000.
 */
#ifndef SS_REGS_H
#define SS_REGS_H

#include <stdint.h>

/* ---- flash ------------------------------------------------------------
 * FLASH_ACR, rigs/fw_f303_aes/main.c:
 *   #define FLASH_ACR REG32(0x40022000u)
 *   ACR_LATENCY_MASK 7u, ACR_HLFCYA 1u<<3, ACR_PRFTBE 1u<<4,
 *   ACR_PRFTBS 1u<<5
 *
 * PRFTBS is the prefetch buffer STATUS and is read-only, which is why it
 * is outside FLASH_ACR_WRITABLE below. It is the one bit a readback can
 * carry that no write put there, and that is the intended behaviour: the
 * readback is what the host records.
 *
 * No ICEN/DCEN/ICRST/DCRST exist on this part, so the cache-reset dance
 * the F4 and G4 board files do has no equivalent here and board.c says
 * so where a reader would look for it.
 */
#define FLASH_BASE_REG        0x40022000u
#define FLASH_ACR             (*(volatile uint32_t *)(FLASH_BASE_REG + 0x00u))
#define FLASH_ACR_LATENCY_Msk 0x00000007u
#define FLASH_ACR_HLFCYA      (1u << 3)
#define FLASH_ACR_PRFTBE      (1u << 4)
#define FLASH_ACR_PRFTBS      (1u << 5)   /* read-only status */
/* Everything a write is allowed to carry. Anything else is refused by
 * ssb_acr_set rather than masked away, so a host that asks for a bit
 * this part does not have learns it from the readback. */
#define FLASH_ACR_WRITABLE    (FLASH_ACR_LATENCY_Msk | FLASH_ACR_HLFCYA \
                               | FLASH_ACR_PRFTBE)

/* ---- RCC --------------------------------------------------------------
 * Base and offsets from rigs/fw_f303_aes/main.c. Note AHBENR at 0x14 and
 * APB1ENR at 0x1C, which are not the F4's offsets either.
 */
#define RCC_BASE_REG       0x40021000u
#define RCC_CR             (*(volatile uint32_t *)(RCC_BASE_REG + 0x00u))
#define RCC_CFGR           (*(volatile uint32_t *)(RCC_BASE_REG + 0x04u))
#define RCC_AHBENR         (*(volatile uint32_t *)(RCC_BASE_REG + 0x14u))
#define RCC_APB1ENR        (*(volatile uint32_t *)(RCC_BASE_REG + 0x1Cu))

#define RCC_CR_HSION       (1u << 0)
#define RCC_CR_HSIRDY      (1u << 1)
#define RCC_CR_PLLON       (1u << 24)
#define RCC_CR_PLLRDY      (1u << 25)

#define RCC_CFGR_SW_Msk    (3u << 0)
#define RCC_CFGR_SW_HSI    (0u << 0)
#define RCC_CFGR_SWS_Msk   (3u << 2)
#define RCC_CFGR_SWS_HSI   (0u << 2)
#define RCC_CFGR_PPRE1_Msk (7u << 8)
#define RCC_CFGR_PPRE1_DIV1 (0u << 8)

#define RCC_AHBENR_IOPAEN   (1u << 17)
#define RCC_APB1ENR_USART2EN (1u << 17)

/* ---- GPIOA: AHB2, USART2 on PA2/PA3 AF7 -------------------------------
 * Base and the MODER/OSPEEDR/PUPDR/AFRL offsets from the rig.
 *
 * BSRR IS THE ONE OFFSET THE RIG DOES NOT USE, because the F303 rig
 * drives no trigger pin. It is taken from rigs/cmsis/stm32f303xc.h's
 * GPIO_TypeDef, where BSRR is the seventh uint32_t member - MODER 0x00,
 * OTYPER 0x04, OSPEEDR 0x08, PUPDR 0x0C, IDR 0x10, ODR 0x14, BSRR 0x18 -
 * and the member after it, LCKR, carries the comment "Address offset:
 * 0x1C". The comment ON the BSRR line in that header reads "0x1A", which
 * is a typo in ST's comment and not the structure: a uint32_t member
 * cannot start at an odd-halfword offset, and LCKR's own comment pins
 * the end of BSRR at 0x1C. The structure is what the compiler uses, so
 * 0x18 is the address, and this paragraph exists because 0x1A is exactly
 * the kind of value that would be copied out of a comment.
 */
#define GPIOA_BASE_REG    0x48000000u
#define GPIOA_MODER       (*(volatile uint32_t *)(GPIOA_BASE_REG + 0x00u))
#define GPIOA_OSPEEDR     (*(volatile uint32_t *)(GPIOA_BASE_REG + 0x08u))
#define GPIOA_PUPDR       (*(volatile uint32_t *)(GPIOA_BASE_REG + 0x0Cu))
#define GPIOA_BSRR        (*(volatile uint32_t *)(GPIOA_BASE_REG + 0x18u))
#define GPIOA_AFRL        (*(volatile uint32_t *)(GPIOA_BASE_REG + 0x20u))

/* ---- USART2 -----------------------------------------------------------
 * The F0/F3/F7-generation peripheral, NOT the F4's SR/DR one: status is
 * ISR, receive is RDR and transmit is TDR, and CR1 is at offset 0 rather
 * than 0x0C. Offsets and bits from rigs/fw_f303_aes/main.c.
 */
#define USART2_BASE_REG   0x40004400u
#define USART2_CR1        (*(volatile uint32_t *)(USART2_BASE_REG + 0x00u))
#define USART2_BRR        (*(volatile uint32_t *)(USART2_BASE_REG + 0x0Cu))
#define USART2_ISR        (*(volatile uint32_t *)(USART2_BASE_REG + 0x1Cu))
#define USART2_RDR        (*(volatile uint32_t *)(USART2_BASE_REG + 0x24u))
#define USART2_TDR        (*(volatile uint32_t *)(USART2_BASE_REG + 0x28u))
#define USART_ISR_RXNE    (1u << 5)
#define USART_ISR_TXE     (1u << 7)
#define USART_CR1_UE      (1u << 0)
#define USART_CR1_RE      (1u << 2)
#define USART_CR1_TE      (1u << 3)

/* ---- core -------------------------------------------------------------
 * Architectural Cortex-M addresses, the same ones the rig uses.
 */
#define SCB_VTOR          (*(volatile uint32_t *)0xE000ED08u)

#define SYST_CSR          (*(volatile uint32_t *)0xE000E010u)
#define SYST_RVR          (*(volatile uint32_t *)0xE000E014u)
#define SYST_CVR          (*(volatile uint32_t *)0xE000E018u)

#define DEMCR             (*(volatile uint32_t *)0xE000EDFCu)
#define DEMCR_TRCENA      (1u << 24)
#define DWT_CTRL          (*(volatile uint32_t *)0xE0001000u)
#define DWT_CTRL_CYCCNTENA (1u << 0)
#define DWT_CTRL_NOCYCCNT  (1u << 25)   /* set = this core has no CYCCNT */
#define DWT_CYCCNT        (*(volatile uint32_t *)0xE0001004u)

#endif /* SS_REGS_H */
