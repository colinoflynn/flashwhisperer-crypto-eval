/* Microchip ATSAMG55J19 registers, by hand.
 *
 * WHERE THESE NUMBERS COME FROM, because on this part they come from two
 * places and only one of them has been on silicon.
 *
 *   * EEFC, CMCC, PMC, CHIPID, WDT and the core blocks are transcribed from
 *     rigs/fw_samg55_aes/ in the research tree, which has been run on a SAM
 *     G55 Xplained Pro. Every address and bit position below that appears in
 *     that rig is one the rig has read back from the die.
 *   * FLEXCOM7, USART7 and PIOA are NOT in that rig and have never been
 *     exercised here. The rig drives the part over an SWD mailbox and has no
 *     serial path at all, so the console half of this port is transcribed
 *     from the vendor headers instead: ASF3 sam/utils/cmsis/samg/samg55/
 *     include/{samg55j19.h,instance/usart7.h,instance/pioa.h,component/
 *     {usart,flexcom,pio}.h} and sam/boards/samg55_xplained_pro/
 *     samg55_xplained_pro.h. Those headers agree with the rig on every base
 *     address the two have in common (CMCC 0x4003C000, PMC 0x400E0400, EFC
 *     0x400E0A00, CHIPID 0x400E0740, WDT 0x400E1450), which is the only
 *     cross-check available without a board.
 *
 * ---- THE ACCELERATOR WORD, AND WHY IT IS PACKED ------------------------
 *
 * The contract wants one opaque word. This part does not have one: the read
 * path is two structures in two peripherals, stacked, and the research tree
 * measured them as separable. The EEFC at 0x400E0A00 holds a 128-bit read
 * latch that no register anywhere reaches, with EEFC_FMR carrying the wait
 * states and three mode bits in front of it; the CMCC at 0x4003C000 is a
 * 4-way cache behind that, with its own enable, its own per-path disables
 * and a programmable capacity. Leaving the CMCC out would leave the board
 * unable to do the cache dance the contract's TRAP 2 asks for, so both are
 * packed into the word and the packing lives here, in the board's own
 * header, where the protocol layer and the host cannot see it.
 *
 *   bit     field      written to             read back from
 *   3:0     FWS        EEFC_FMR[11:8]         EEFC_FMR[11:8]
 *   4       SCOD       EEFC_FMR[16]           EEFC_FMR[16]
 *   5       FAM        EEFC_FMR[24]           EEFC_FMR[24]
 *   6       CLOE       EEFC_FMR[26]           EEFC_FMR[26]
 *   7       -          reserved, must be 0
 *   8       CEN        CMCC_CTRL[0]           CMCC_SR[0] (CSTS)
 *   9       ICDIS      CMCC_CFG[1]            CMCC_CFG[1]
 *   10      DCDIS      CMCC_CFG[2]            CMCC_CFG[2]
 *   11      -          reserved, must be 0
 *   14:12   PRGCSIZE   CMCC_CFG[6:4]          CMCC_CFG[6:4]
 *
 * CEN is the one asymmetric row and it is asymmetric in the hardware:
 * CMCC_CTRL is write-only, so the enable cannot be read back from the
 * register it was written to and the status bit is what the board reports.
 * The rig says so and does the same thing.
 *
 * WHAT IS NOT IN THE WORD, and why.
 *
 *   * The EEFC's read latch has no enable, no mode and no register of its
 *     own. The research tree's roster records that no register anywhere in
 *     this map reaches it, which is exactly why FWS matters so much here:
 *     the only way to change what a latch miss costs is to change the
 *     insertion count in front of it.
 *   * FRDY, EEFC_FMR[0], is an interrupt enable for flash programming. This
 *     firmware never programs flash and an EFC interrupt inside a measured
 *     region is contamination, so it is held at 0 and is not an axis.
 *   * CMCC_MCFG / CMCC_MEN / CMCC_MSR are the cache's hit counters. They are
 *     a separate contract hook (ssb_monitors_*), not part of the
 *     configuration word.
 *
 * ---- WRITING EEFC_FMR: NO KEY, AND THAT IS CHECKED, NOT ASSUMED --------
 *
 * SAM parts key-protect several configuration registers and the question is
 * a fair one to ask of the flash controller. On this part the rig writes
 * EEFC_FMR as one plain 32-bit store with no key, no unlock sequence and no
 * command register, and it reads the value straight back in its IDENT
 * reply. The key-protected register on this part is CKGR_MOR, which needs
 * 0x37 in bits 23:16 or the write is discarded silently and reads back as
 * the old value. That one is in the clock path, not the accelerator path,
 * and this port does not write it (see board.c on the clock).
 *
 * EEFC_FMR is written whole rather than read-modify-written, also from the
 * rig: it has no other live fields, and a read-modify-write would carry
 * whatever the previous configuration set into the next one.
 */
#ifndef SS_REGS_H
#define SS_REGS_H

#include <stdint.h>

#define SS_REG32(a) (*(volatile uint32_t *)(a))

/* ---- EEFC, the flash controller --------------------------------------- */
#define EFC_BASE_REG      0x400E0A00u
#define EEFC_FMR          SS_REG32(EFC_BASE_REG + 0x00u)
#define FMR_FWS_Pos       8u
#define FMR_FWS_Msk       (0xFu << 8)
#define FMR_SCOD          (1u << 16)
#define FMR_FAM           (1u << 24)
#define FMR_CLOE          (1u << 26)

/* ---- CMCC, the cache in front of the whole flash read path ------------- */
#define CMCC_BASE_REG     0x4003C000u
#define CMCC_TYPE         SS_REG32(CMCC_BASE_REG + 0x00u)
#define CMCC_CFG          SS_REG32(CMCC_BASE_REG + 0x04u)
#define CMCC_CTRL         SS_REG32(CMCC_BASE_REG + 0x08u)
#define CMCC_SR           SS_REG32(CMCC_BASE_REG + 0x0Cu)
#define CMCC_MAINT0       SS_REG32(CMCC_BASE_REG + 0x20u)
#define CMCC_MCFG         SS_REG32(CMCC_BASE_REG + 0x28u)
#define CMCC_MEN          SS_REG32(CMCC_BASE_REG + 0x2Cu)
#define CMCC_MCTRL        SS_REG32(CMCC_BASE_REG + 0x30u)
#define CMCC_MSR          SS_REG32(CMCC_BASE_REG + 0x34u)
#define CFG_ICDIS         (1u << 1)
#define CFG_DCDIS         (1u << 2)
#define CFG_PRGCSIZE_Pos  4u
#define CFG_PRGCSIZE_Msk  (0x7u << 4)
#define CTRL_CEN          (1u << 0)
#define SR_CSTS           (1u << 0)
#define MAINT0_INVALL     (1u << 0)
#define MEN_MENABLE       (1u << 0)
#define MCTRL_SWRST       (1u << 0)
#define MCFG_MODE_Msk     0x3u

/* ---- the packed accelerator word --------------------------------------- */
#define SSB_ACR_FWS_Pos       0u
#define SSB_ACR_FWS_Msk       0x0000000Fu
#define SSB_ACR_SCOD          (1u << 4)
#define SSB_ACR_FAM           (1u << 5)
#define SSB_ACR_CLOE          (1u << 6)
#define SSB_ACR_CEN           (1u << 8)
#define SSB_ACR_ICDIS         (1u << 9)
#define SSB_ACR_DCDIS         (1u << 10)
#define SSB_ACR_PRGCSIZE_Pos  12u
#define SSB_ACR_PRGCSIZE_Msk  (0x7u << 12)
/* Everything the word is allowed to carry. A bit outside this is refused
 * rather than masked away, so a host that sends an STM32 FLASH_ACR value to
 * the wrong part learns about it from the readback. */
#define SSB_ACR_VALID_Msk     (SSB_ACR_FWS_Msk | SSB_ACR_SCOD | SSB_ACR_FAM \
                             | SSB_ACR_CLOE | SSB_ACR_CEN | SSB_ACR_ICDIS \
                             | SSB_ACR_DCDIS | SSB_ACR_PRGCSIZE_Msk)

/* ---- PMC, read only in this port --------------------------------------- */
#define PMC_BASE_REG      0x400E0400u
#define PMC_PCER0         SS_REG32(PMC_BASE_REG + 0x10u)
#define CKGR_MOR          SS_REG32(PMC_BASE_REG + 0x20u)
#define PMC_MCKR          SS_REG32(PMC_BASE_REG + 0x30u)
#define PMC_SR            SS_REG32(PMC_BASE_REG + 0x68u)

/* Peripheral identifiers, which are also the PMC_PCER0 bit numbers for
 * everything below 32. From samg55j19.h; the rig's vector table carries the
 * same numbering (FLEXCOM7 is interrupt 7, PIOA is 11). */
#define ID_FLEXCOM7       7u
#define ID_PIOA           11u

/* ---- FLEXCOM7 and USART7: the EDBG virtual COM port -------------------- */
/* SAM G55 has no standalone UART peripheral. Every serial port on it is a
 * FLEXCOM, a wrapper that selects one of USART / SPI / TWI, and the USART
 * registers only answer once FLEXCOM_MR.OPMODE says USART. USART7 is the
 * one the Xplained Pro board support calls CONSOLE_UART. */
#define FLEXCOM7_BASE_REG 0x40034000u
#define FLEXCOM7_MR       SS_REG32(FLEXCOM7_BASE_REG + 0x00u)
#define FLEXCOM_MR_OPMODE_USART 0x1u

#define USART7_BASE_REG   0x40034200u
#define US_CR             SS_REG32(USART7_BASE_REG + 0x00u)
#define US_MR             SS_REG32(USART7_BASE_REG + 0x04u)
#define US_IDR            SS_REG32(USART7_BASE_REG + 0x0Cu)
#define US_CSR            SS_REG32(USART7_BASE_REG + 0x14u)
#define US_RHR            SS_REG32(USART7_BASE_REG + 0x18u)
#define US_THR            SS_REG32(USART7_BASE_REG + 0x1Cu)
#define US_BRGR           SS_REG32(USART7_BASE_REG + 0x20u)

#define US_CR_RSTRX       (1u << 2)
#define US_CR_RSTTX       (1u << 3)
#define US_CR_RXEN        (1u << 4)
#define US_CR_RXDIS       (1u << 5)
#define US_CR_TXEN        (1u << 6)
#define US_CR_TXDIS       (1u << 7)
#define US_MR_MODE_NORMAL (0x0u << 0)
#define US_MR_USCLKS_MCK  (0x0u << 4)
#define US_MR_CHRL_8_BIT  (0x3u << 6)
#define US_MR_PAR_NO      (0x4u << 9)
#define US_MR_NBSTOP_1    (0x0u << 12)
#define US_MR_CHMODE_NORM (0x0u << 14)
#define US_CSR_RXRDY      (1u << 0)
#define US_CSR_TXRDY      (1u << 1)
#define US_BRGR_CD_Pos    0u
#define US_BRGR_FP_Pos    16u

/* ---- PIOA -------------------------------------------------------------- */
/* PA27 is RXD7 and PA28 is TXD7, both on peripheral B; PA6 is LED0. All
 * three from samg55_xplained_pro.h, and see board.c on what that header
 * contradicts itself about. */
#define PIOA_BASE_REG     0x400E0E00u
#define PIOA_PER          SS_REG32(PIOA_BASE_REG + 0x00u)
#define PIOA_PDR          SS_REG32(PIOA_BASE_REG + 0x04u)
#define PIOA_OER          SS_REG32(PIOA_BASE_REG + 0x10u)
#define PIOA_SODR         SS_REG32(PIOA_BASE_REG + 0x30u)
#define PIOA_CODR         SS_REG32(PIOA_BASE_REG + 0x34u)
#define PIOA_PUDR         SS_REG32(PIOA_BASE_REG + 0x60u)
#define PIOA_PUER         SS_REG32(PIOA_BASE_REG + 0x64u)
#define PIOA_ABCDSR0      SS_REG32(PIOA_BASE_REG + 0x70u)
#define PIOA_ABCDSR1      SS_REG32(PIOA_BASE_REG + 0x74u)

#define PIO_PA6           (1u << 6)
#define PIO_PA27_RXD7     (1u << 27)
#define PIO_PA28_TXD7     (1u << 28)

/* ---- memory bases ------------------------------------------------------ */
/* Both as in memory.ld. The flash base is 0x00400000 rather than 0 or
 * 0x08000000; startup.c uses the SRAM base to tell the two table-residence
 * variants apart. */
#define SS_FLASH_BASE     0x00400000u
#define SS_SRAM_BASE      0x20000000u

/* ---- core -------------------------------------------------------------- */
#define SCB_VTOR          SS_REG32(0xE000ED08u)

#define SYST_CSR          SS_REG32(0xE000E010u)
#define SYST_RVR          SS_REG32(0xE000E014u)
#define SYST_CVR          SS_REG32(0xE000E018u)
#define SYST_MASK         0x00FFFFFFu

#define DEMCR             SS_REG32(0xE000EDFCu)
#define DEMCR_TRCENA      (1u << 24)
#define DWT_CTRL          SS_REG32(0xE0001000u)
#define DWT_CTRL_CYCCNTENA (1u << 0)
#define DWT_CYCCNT        SS_REG32(0xE0001004u)

/* ---- watchdog ---------------------------------------------------------- */
/* Live at reset on this part, at roughly 16 s, and WDT_MR is write-once per
 * reset. It is turned off in startup.c because that is the only place it
 * can be turned off. */
#define WDT_MR            SS_REG32(0x400E1454u)
#define WDT_MR_WDDIS      (1u << 15)

#endif /* SS_REGS_H */
