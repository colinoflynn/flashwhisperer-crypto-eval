/* TI CC1352R1F3 registers, by hand, and the accelerator-word packing.
 *
 * EVERY ADDRESS AND BIT POSITION IN THIS FILE WAS COPIED FROM THE RIG that
 * measured this part, rigs/fw_cc1352_aes/main.c in the research tree, which
 * took them from TI's own target database shipped inside UniFlash
 * (ccs_base/common/targetdb/devices/cc1352r1f3.xml and
 * the Modules/CC26xx/CC2652 module files, VIMS.xml among them). Nothing
 * here is inferred from another TI
 * part or from a family manual, because a wrong peripheral address on this
 * part does not fault - it reads zero, and a zero accelerator word is a
 * plausible configuration rather than an error.
 *
 * ---- the one opaque accelerator word ------------------------------------
 *
 * The accelerator here is the VIMS (Versatile Instruction Memory System),
 * which is not one enable bit and not a wait-state field. It is an 8 KB
 * array that VIMS_CTL.MODE switches between three jobs, plus a separate
 * one-entry 16-byte flash line buffer with its own disable, plus a tag
 * prefetcher. The two that decide whether this part leaks sit in different
 * fields of the same register and mean different things, so the word is
 * PACKED, the same escape hatch docs/PORTING.md documents for the H723:
 *
 *     bits 0..1   VIMS_CTL.MODE          0 = GPRAM, 1 = CACHE, 3 = OFF
 *     bit  2      VIMS_CTL.PREF_EN       tag prefetcher
 *     bit  3      not part of the word   (VIMS_CTL.ARB_CFG, see below)
 *     bit  4      VIMS_CTL.SYSBUS_LB_DIS sysbus line buffer disable
 *     bit  5      VIMS_CTL.IDCODE_LB_DIS Icode/Dcode line buffer disable
 *
 * The positions are deliberately the ones VIMS_CTL itself uses, so a word
 * recorded in a capture can be read against a register dump of the running
 * part without a translation table in between. The word is still the
 * board's own: it is a SUBSET of VIMS_CTL, bits outside CC_ACR_VALID_Msk
 * are refused rather than written, and ssb_acr_get reads the register and
 * masks, so a refused request comes back as the configuration that is
 * actually running.
 *
 * MODE = 2 does not exist on this part and is refused with everything else
 * invalid. Bit 3 is a hole in the word rather than a knob: ARB_CFG picks
 * Icode/Dcode against sysbus arbitration, and with one master running the
 * cipher there is nothing for it to arbitrate, so it was held at 0 for
 * every measurement this part has. Leaving it out of the word means a
 * sweep cannot reach it by accident and then attribute a reading to a
 * setting nobody meant to change.
 *
 * DYN_CG_EN (bit 29) is likewise not in the word. The rig preserves
 * whatever the hardware has there across a reconfigure instead of choosing
 * a value, and so does this board file.
 *
 * ---- what is NOT in the word, and why -----------------------------------
 *
 * There is no wait-state axis here. FLASH_FRDCTL at 0x40032000 reads 0x200
 * and accepts writes of 0x000 to 0x500, and the research tree measured that
 * every one of them changes nothing - and TI marks the register internal,
 * so what the field means is not established either. A knob that cannot
 * reach the data path does not belong in the word; the clock divider is
 * the gain knob on this part instead, and it is reached through
 * ssb_set_mhz rather than through the accelerator word.
 *
 * The CRYPTO hardware AES engine at 0x40024000 is not touched anywhere in
 * this port. The question is what a software table lookup costs.
 */
#ifndef SS_REGS_H
#define SS_REGS_H

#include <stdint.h>

#define REG32(a) (*(volatile uint32_t *)(a))

/* ---- packing of the opaque accelerator word --------------------------- */
#define CC_ACR_MODE_Msk      0x00000003u
#define CC_ACR_MODE_GPRAM    0x00000000u
#define CC_ACR_MODE_CACHE    0x00000001u
#define CC_ACR_MODE_OFF      0x00000003u
#define CC_ACR_PREF_EN       0x00000004u
#define CC_ACR_SYSBUS_LB_DIS 0x00000010u
#define CC_ACR_IDCODE_LB_DIS 0x00000020u
/* Bit 3 is absent on purpose: see the header comment. */
#define CC_ACR_VALID_Msk     (CC_ACR_MODE_Msk | CC_ACR_PREF_EN \
                              | CC_ACR_SYSBUS_LB_DIS | CC_ACR_IDCODE_LB_DIS)

/* ---- VIMS ------------------------------------------------------------- */
#define VIMS_BASE            0x40034000u
#define VIMS_STAT            REG32(VIMS_BASE + 0x00u)
#define VIMS_CTL             REG32(VIMS_BASE + 0x04u)
#define VIMS_MODE_M          0x3u
#define VIMS_MODE_GPRAM      0x0u
#define VIMS_MODE_CACHE      0x1u
#define VIMS_MODE_OFF        0x3u
#define VIMS_PREF_EN         (1u << 2)
#define VIMS_ARB_CFG         (1u << 3)
#define VIMS_SYSBUS_LB_DIS   (1u << 4)
#define VIMS_IDCODE_LB_DIS   (1u << 5)
#define VIMS_DYN_CG_EN       (1u << 29)
/* Both of these have to be waited on across a reconfigure. MODE_CHANGING
 * covers the transition and INV covers the automatic cache invalidate, and
 * skipping the second is how stale lines survive a mode change. */
#define VIMS_STAT_MODE_CHANGING (1u << 3)
#define VIMS_STAT_INV           (1u << 2)

/* ---- PRCM: power domains, clock gates, the CPU clock divider ---------- */
#define PRCM_BASE            0x40082000u
#define PRCM_CLKLOADCTL      REG32(PRCM_BASE + 0x028u)  /* [0] LOAD [1] DONE */
#define PRCM_GPIOCLKGR       REG32(PRCM_BASE + 0x048u)  /* [0] CLK_EN        */
#define PRCM_UARTCLKGR       REG32(PRCM_BASE + 0x06Cu)  /* [1:0] CLK_EN      */
#define PRCM_CPUCLKDIV       REG32(PRCM_BASE + 0x0B8u)  /* [0] RATIO         */
#define PRCM_PDCTL0          REG32(PRCM_BASE + 0x12Cu)  /* [2] PERIPH [1] SER */
#define PRCM_PDSTAT0         REG32(PRCM_BASE + 0x140u)
#define PRCM_CLKLOADCTL_LOAD      (1u << 0)
#define PRCM_CLKLOADCTL_LOAD_DONE (1u << 1)
#define PRCM_PD_SERIAL       (1u << 1)
#define PRCM_PD_PERIPH       (1u << 2)
#define PRCM_CPUCLKDIV_RATIO (1u << 0)

/* ---- IOC: the pin mux. One word per DIO. ------------------------------ */
#define IOC_BASE             0x40081000u
#define IOCFG(n)             REG32(IOC_BASE + 4u * (n))
#define IOC_PORT_GPIO        0u
#define IOC_PORT_UART0_RX    15u
#define IOC_PORT_UART0_TX    16u
#define IOC_PULL_DIS         (3u << 13)
#define IOC_IE               (1u << 29)

/* ---- UART0 ------------------------------------------------------------ */
#define UART0_BASE           0x40001000u
#define UART_DR              REG32(UART0_BASE + 0x000u)
#define UART_FR              REG32(UART0_BASE + 0x018u)
#define UART_IBRD            REG32(UART0_BASE + 0x024u)
#define UART_FBRD            REG32(UART0_BASE + 0x028u)
#define UART_LCRH            REG32(UART0_BASE + 0x02Cu)
#define UART_CTL             REG32(UART0_BASE + 0x030u)
#define UART_FR_RXFE         (1u << 4)
#define UART_FR_TXFF         (1u << 5)
#define UART_LCRH_WLEN8      (3u << 5)
#define UART_LCRH_FEN        (1u << 4)
#define UART_CTL_UARTEN      (1u << 0)
#define UART_CTL_TXE         (1u << 8)
#define UART_CTL_RXE         (1u << 9)

/* ---- core ------------------------------------------------------------- */
/* Architectural Cortex-M addresses, not part-specific, and the same ones
 * every other board file here uses. The rig confirms DWT_CYCCNT runs on
 * this die (DWT_CTRL.NOCYCCNT reads 0); it does not exercise SysTick, so
 * the SysTick arm of board.c proves itself at run time instead of being
 * assumed. */
#define SYST_CSR             REG32(0xE000E010u)
#define SYST_RVR             REG32(0xE000E014u)
#define SYST_CVR             REG32(0xE000E018u)

#define DEMCR                REG32(0xE000EDFCu)
#define DEMCR_TRCENA         (1u << 24)
#define DWT_CTRL             REG32(0xE0001000u)
#define DWT_CTRL_CYCCNTENA   (1u << 0)
#define DWT_CTRL_NOCYCCNT    (1u << 25)
#define DWT_CYCCNT           REG32(0xE0001004u)

#endif /* SS_REGS_H */
