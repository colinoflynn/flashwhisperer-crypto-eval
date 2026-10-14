/* STM32H723ZG registers, by hand, and the accelerator-word packing.
 *
 * Twenty-odd registers rather than a vendored CMSIS tree, same as the other
 * parts here. A part that genuinely wants a vendor HAL may bring one - the
 * contract does not care.
 *
 * THE ACCELERATOR ON THIS PART IS NOT FLASH_ACR. That register holds
 * LATENCY and WRHIGHFREQ and nothing else: no PRFTEN, no ICEN, no DCEN.
 * Every cache on the data path is the Cortex-M7 L1, which lives in the
 * core's own SCB, and the ITCM-side ART is a third register again. So the
 * one opaque word ss_board.h asks for is PACKED here, which is the escape
 * hatch that contract documents for exactly this case:
 *
 *     bits 0..3   FLASH_ACR LATENCY, 0..7  (reads 7 at reset, measured)
 *     bit  8      SCB_CCR.IC   L1 instruction cache
 *     bit  9      SCB_CCR.DC   L1 data cache
 *
 * The host never interprets it; it round-trips through SS_V_SET_ACR and
 * lands in the capture's metadata, so a trace count is attributable to the
 * configuration that actually ran. Read the readback, never the request.
 *
 * THE ART IS DELIBERATELY NOT IN THERE, for a reason and then for a
 * second one. The reason: on this part the ART accelerates the ITCM flash
 * ALIAS, which is instruction-side, and this victim executes from the AXI
 * alias at 0x08000000. It cannot reach the data path being measured -
 * `research/reference/DATASHEETS.md` records "no ART on the data path" for
 * the H723 as a measurement, not an inference.
 *
 * The second one: it was tried anyway, and merely READING ART_CTR at
 * 0x40024400 stalls the device, with RCC_AHB1ENR bit 1 set and read back
 * first. Bisected by replacing the body of ssb_acr_get one register at a
 * time: a constant answers, FLASH_ACR answers 0x7, adding SCB_CCR still
 * answers, adding ART_CTR hangs. The symptom is an SS_V_IDENT that sends
 * eight of its sixteen bytes and stops, which the host reports as a short
 * response and which reads like a baud problem. Cause not established -
 * it is recorded as a reproducible fact about this die, and the register
 * is left out because nothing here needs it.
 */
#ifndef SS_REGS_H
#define SS_REGS_H

#include <stdint.h>

/* ---- packing of the opaque accelerator word --------------------------- */
#define H7_ACR_LATENCY_Msk   0x0000000Fu
#define H7_ACR_IC            0x00000100u
#define H7_ACR_DC            0x00000200u

/* ---- flash ------------------------------------------------------------ */
#define FLASH_BASE_REG    0x52002000u
#define FLASH_ACR         (*(volatile uint32_t *)(FLASH_BASE_REG + 0x00u))
#define FLASH_ACR_LATENCY_Msk    0x0000000Fu
#define FLASH_ACR_WRHIGHFREQ_Msk 0x00000030u

/* ---- RCC -------------------------------------------------------------- */
#define RCC_BASE_REG      0x58024400u
#define RCC_CR            (*(volatile uint32_t *)(RCC_BASE_REG + 0x000u))
#define RCC_AHB4ENR       (*(volatile uint32_t *)(RCC_BASE_REG + 0x0E0u))
#define RCC_APB1LENR      (*(volatile uint32_t *)(RCC_BASE_REG + 0x0E8u))

#define RCC_CR_HSION      (1u << 0)
#define RCC_CR_HSIRDY     (1u << 2)
#define RCC_CR_HSIDIV_Msk (3u << 3)
#define RCC_CR_HSIDIVF    (1u << 5)
#define RCC_AHB4ENR_GPIOBEN  (1u << 1)
#define RCC_AHB4ENR_GPIODEN  (1u << 3)
#define RCC_APB1LENR_USART3EN (1u << 18)

/* ---- GPIO: USART3 on PD8/PD9 AF7, LD1 on PB0, as every Nucleo-144 ------ */
#define GPIOB_BASE_REG    0x58020400u
#define GPIOB_MODER       (*(volatile uint32_t *)(GPIOB_BASE_REG + 0x00u))
#define GPIOB_BSRR        (*(volatile uint32_t *)(GPIOB_BASE_REG + 0x18u))
#define GPIOD_BASE_REG    0x58020C00u
#define GPIOD_MODER       (*(volatile uint32_t *)(GPIOD_BASE_REG + 0x00u))
#define GPIOD_OSPEEDR     (*(volatile uint32_t *)(GPIOD_BASE_REG + 0x08u))
#define GPIOD_PUPDR       (*(volatile uint32_t *)(GPIOD_BASE_REG + 0x0Cu))
#define GPIOD_AFRH        (*(volatile uint32_t *)(GPIOD_BASE_REG + 0x24u))

/* ---- USART3 ----------------------------------------------------------- */
#define USART3_BASE_REG   0x40004800u
#define USART3_CR1        (*(volatile uint32_t *)(USART3_BASE_REG + 0x00u))
#define USART3_CR2        (*(volatile uint32_t *)(USART3_BASE_REG + 0x04u))
#define USART3_CR3        (*(volatile uint32_t *)(USART3_BASE_REG + 0x08u))
#define USART3_BRR        (*(volatile uint32_t *)(USART3_BASE_REG + 0x0Cu))
#define USART3_ISR        (*(volatile uint32_t *)(USART3_BASE_REG + 0x1Cu))
#define USART3_RDR        (*(volatile uint32_t *)(USART3_BASE_REG + 0x24u))
#define USART3_TDR        (*(volatile uint32_t *)(USART3_BASE_REG + 0x28u))
#define USART_CR1_UE      (1u << 0)
#define USART_CR1_RE      (1u << 2)
#define USART_CR1_TE      (1u << 3)
#define USART_ISR_RXNE    (1u << 5)
#define USART_ISR_TXE     (1u << 7)

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
