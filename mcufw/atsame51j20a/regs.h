/* Microchip ATSAME51J20A registers, by hand, and the accelerator-word packing.
 *
 * WHERE EVERY NUMBER IN THIS FILE CAME FROM, because on this part the two
 * halves have different provenance and only one of them has been on silicon.
 *
 *   NVMCTRL, CMCC, MCLK, GCLK, OSCCTRL, DSU and the DWT: copied from
 *   rigs/fw_same51_aes/main.c in the flash-accelerator-tests tree, which
 *   took them from the component headers in Microchip.SAME51_DFP 3.9.267
 *   and has been run on this die (DSU.DID 0x61810604, ATSAME51J20A rev G).
 *   Those are the registers the measurement touches and they are the ones
 *   that have to be right.
 *
 *   SERCOM5 and PORT: NOT in that rig. It speaks to the host over an SWD
 *   mailbox and never brings a UART up, and the SAM D21 rig next to it
 *   declined to bring its SERCOM up for the same reason. So the console
 *   here is written from the ATSAME51J20A SVD and the SAM E51 Curiosity
 *   Nano kit user guide DS70005432 rev B, by way of the vendor-doc tree in
 *   the dulselang checkout, and NOTHING IN IT HAS BEEN RUN. Every base
 *   address that tree and the rig both carry agrees to the byte (NVMCTRL
 *   0x41004000, CMCC 0x41006000, MCLK 0x40000800, GCLK 0x40001C00, OSCCTRL
 *   0x40001000, and MCLK's own offsets), which is why it is trusted this
 *   far and no further. See the console note in board.c.
 *
 * ---- the opaque accelerator word ---------------------------------------
 *
 * There are two structures on the read path here and they are in different
 * peripherals, so the one word ss_board.h asks for is PACKED. The host
 * never interprets it; it round-trips through SS_V_SET_ACR and lands in the
 * capture's metadata, so a trace count is attributable to the configuration
 * that actually ran.
 *
 *     bits 0..3    NVMCTRL.CTRLA.RWS         wait states, 0..15
 *     bit  4       NVMCTRL.CTRLA.AUTOWS      1 = the hardware picks
 *     bit  5       NVMCTRL.CTRLA.CACHEDIS0   1 = AHB0 line cache OFF
 *     bit  6       NVMCTRL.CTRLA.CACHEDIS1   1 = AHB1 line cache OFF
 *     bit  8       CMCC.CTRL.CEN             1 = CMCC ON
 *     bit  9       CMCC.CFG.ICDIS            1 = its instruction side OFF
 *     bit  10      CMCC.CFG.DCDIS            1 = its data side OFF
 *     bits 12..14  CMCC.CFG.CSIZESW          0 = 1 KB, 1 = 2 KB, 2 = 4 KB
 *
 * THE DISABLE BITS KEEP THE HARDWARE'S POLARITY rather than being flipped
 * into enables, so a packed word maps onto the register fields with no
 * inversion anywhere. An inversion that exists in one direction and not the
 * other is a wrong configuration recorded under the right name, which is
 * the failure this whole layer is shaped to avoid.
 *
 * The reset word is therefore 0x2010: AUTOWS set, RWS 0, both line caches
 * live, CMCC off, CSIZESW reading 4 KB. The CMCC being off at reset means
 * reset is NOT what a product ships on this part.
 *
 * WHAT IS DELIBERATELY NOT IN THE WORD, and why.
 *
 *   CTRLA.AHBNS0/1 and CTRLA.PRM: the research tree measured both inert on
 *   this die. They are left out rather than swept so the word stays small,
 *   and the board refuses them rather than silently dropping them.
 *
 *   CMCC.LCKWAY: it is an axis and it is not inert, the research tree has
 *   it making the channel worse rather than better. It is out because it
 *   locks ways rather than configuring the read path, so it belongs to a
 *   mitigation study and not to the accelerator state a capture is filed
 *   under. Say so if it is ever wanted; adding it is four lines.
 *
 *   The three CMCC RAM apertures at 0x03000000 and above: an inspection
 *   route, not a configuration, and two of the three bus-fault on this die.
 */
#ifndef SS_REGS_H
#define SS_REGS_H

#include <stdint.h>

#define REG32(a)   (*(volatile uint32_t *)(a))
#define REG16(a)   (*(volatile uint16_t *)(a))
#define REG8(a)    (*(volatile uint8_t  *)(a))

/* ---- packing of the opaque accelerator word ---------------------------- */
#define E51_ACR_RWS_Msk       0x0000000Fu
#define E51_ACR_RWS_Pos       0u
#define E51_ACR_AUTOWS        (1u << 4)
#define E51_ACR_CACHEDIS0     (1u << 5)
#define E51_ACR_CACHEDIS1     (1u << 6)
#define E51_ACR_CEN           (1u << 8)
#define E51_ACR_ICDIS         (1u << 9)
#define E51_ACR_DCDIS         (1u << 10)
#define E51_ACR_CSIZESW_Msk   0x00007000u
#define E51_ACR_CSIZESW_Pos   12u
/* Everything the word is allowed to carry. A request with a bit outside
 * this is refused rather than masked, so a typo cannot be recorded as a
 * configuration that was never asked for. */
#define E51_ACR_KNOWN_Msk     (E51_ACR_RWS_Msk | E51_ACR_AUTOWS \
                             | E51_ACR_CACHEDIS0 | E51_ACR_CACHEDIS1 \
                             | E51_ACR_CEN | E51_ACR_ICDIS | E51_ACR_DCDIS \
                             | E51_ACR_CSIZESW_Msk)

/* ---- NVMCTRL: the level-two read path ---------------------------------- *
 * CTRLA AND CTRLB SWAP ROLES AGAINST THE SAM D21. Here CTRLA is the read
 * path and CTRLB is the write-only command register, and the CMDEX key
 * 0xA5 sits in the byte a wait-state write would reach, so D21 code copied
 * over would issue a command. Nothing in this port writes CTRLB at all. */
#define NVMCTRL           0x41004000u
#define NVM_CTRLA         REG16(NVMCTRL + 0x00u)
#define NVM_PARAM         REG32(NVMCTRL + 0x08u)
#define NVM_INTFLAG       REG16(NVMCTRL + 0x10u)
#define NVM_STATUS        REG16(NVMCTRL + 0x12u)

#define CTRLA_AUTOWS      (1u << 2)
#define CTRLA_SUSPEN      (1u << 3)
#define CTRLA_WMODE_Msk   (3u << 4)
#define CTRLA_PRM_Msk     (3u << 6)
#define CTRLA_RWS_Pos     8u
#define CTRLA_RWS_Msk     (0xFu << 8)
#define CTRLA_AHBNS0      (1u << 12)
#define CTRLA_AHBNS1      (1u << 13)
#define CTRLA_CACHEDIS0   (1u << 14)
#define CTRLA_CACHEDIS1   (1u << 15)

/* ---- CMCC: the level-one cache ----------------------------------------- *
 * CTRL, MAINT0, MAINT1 and MCTRL are WRITE-ONLY, which is why ssb_acr_get
 * reads the enable back out of SR.CSTS and not out of CTRL. */
#define CMCC              0x41006000u
#define CMCC_TYPE         REG32(CMCC + 0x00u)
#define CMCC_CFG          REG32(CMCC + 0x04u)
#define CMCC_CTRL         REG32(CMCC + 0x08u)
#define CMCC_SR           REG32(CMCC + 0x0Cu)
#define CMCC_LCKWAY       REG32(CMCC + 0x10u)
#define CMCC_MAINT0       REG32(CMCC + 0x20u)
#define CMCC_MCFG         REG32(CMCC + 0x28u)
#define CMCC_MEN          REG32(CMCC + 0x2Cu)
#define CMCC_MCTRL        REG32(CMCC + 0x30u)
#define CMCC_MSR          REG32(CMCC + 0x34u)

#define CFG_ICDIS         (1u << 1)
#define CFG_DCDIS         (1u << 2)
#define CFG_CSIZESW_Pos   4u
#define CFG_CSIZESW_Msk   (7u << 4)
#define CTRL_CEN          (1u << 0)
#define SR_CSTS           (1u << 0)
#define MAINT0_INVALL     (1u << 0)

/* ---- MCLK: the bus clock gates ----------------------------------------- *
 * APBDMASK resets to zero, so a peripheral on bridge D reads all zeros and
 * discards writes until its bit is set. SERCOM5 is bit 1 of APBDMASK; that
 * bit number is from the vendor-doc tree, not the rig. */
#define MCLK              0x40000800u
#define MCLK_HSDIV        REG8 (MCLK + 0x04u)
#define MCLK_CPUDIV       REG8 (MCLK + 0x05u)
#define MCLK_AHBMASK      REG32(MCLK + 0x10u)
#define MCLK_APBDMASK     REG32(MCLK + 0x20u)
#define APBDMASK_SERCOM5  (1u << 1)

/* ---- GCLK: the peripheral core clocks ---------------------------------- *
 * Generator 0 runs from DFLL48M at reset and nothing here reprograms it,
 * so the only GCLK write this port makes is the SERCOM's channel.
 * Peripheral channel 35 is SERCOM5_CORE; that index is from the vendor-doc
 * tree, not the rig. */
#define GCLK              0x40001C00u
#define GCLK_SYNCBUSY     REG32(GCLK + 0x04u)
#define GCLK_GENCTRL(n)   REG32(GCLK + 0x20u + 4u * (n))
#define GCLK_PCHCTRL(n)   REG32(GCLK + 0x80u + 4u * (n))
#define PCHCTRL_CHEN      (1u << 6)
#define GCLK_ID_SERCOM5   35u

/* ---- PORT: the trigger pin and the console pads ------------------------ *
 * Two groups on this package, GROUP0 = PA00-PA31 and GROUP1 = PB00-PB31,
 * 0x80 apart. PMUXn packs two pins per byte and PINCFGn is one byte per
 * pin. These offsets are from the SVD, not the rig. */
#define PORT_BASE         0x41008000u
#define PORT_GROUP(g)     (PORT_BASE + 0x80u * (g))
#define PORT_DIRSET(g)    REG32(PORT_GROUP(g) + 0x08u)
#define PORT_OUTCLR(g)    REG32(PORT_GROUP(g) + 0x14u)
#define PORT_OUTSET(g)    REG32(PORT_GROUP(g) + 0x18u)
#define PORT_PMUX(g, p)   REG8 (PORT_GROUP(g) + 0x30u + ((p) >> 1))
#define PORT_PINCFG(g, p) REG8 (PORT_GROUP(g) + 0x40u + (p))
#define PINCFG_PMUXEN     (1u << 0)
#define PINCFG_INEN       (1u << 1)
#define PINCFG_PULLEN     (1u << 2)
/* Peripheral function letters are a family convention: A = 0, B = 1,
 * C = 2, and so on. Confirmed in-tree against the SAM D21 CMSIS header
 * rigs/cmsis/samd21/pio/samd21j18a.h, where MUX_PA22C_SERCOM3_PAD0 is 2. */
#define PMUX_C            2u

/* ---- SERCOM5 in USART mode with an internal clock ----------------------- *
 * NOT IN THE RIG. Offsets and field positions from the ATSAME51J20A SVD;
 * the pad assignment from DS70005432 rev B. */
#define SERCOM5           0x43000400u
#define SERCOM5_CTRLA     REG32(SERCOM5 + 0x00u)
#define SERCOM5_CTRLB     REG32(SERCOM5 + 0x04u)
#define SERCOM5_BAUD      REG16(SERCOM5 + 0x0Cu)
#define SERCOM5_INTFLAG   REG8 (SERCOM5 + 0x18u)
#define SERCOM5_STATUS    REG16(SERCOM5 + 0x1Au)
#define SERCOM5_SYNCBUSY  REG32(SERCOM5 + 0x1Cu)
#define SERCOM5_DATA      REG32(SERCOM5 + 0x28u)

#define USART_CTRLA_SWRST     (1u << 0)
#define USART_CTRLA_ENABLE    (1u << 1)
#define USART_CTRLA_MODE_Pos  2u
#define USART_CTRLA_MODE_INT  1u          /* USART with an internal clock */
#define USART_CTRLA_SAMPR_Pos 13u
#define USART_CTRLA_TXPO_Pos  16u
#define USART_CTRLA_RXPO_Pos  20u
#define USART_CTRLA_FORM_Pos  24u
#define USART_CTRLA_DORD      (1u << 30)  /* 1 = LSB first */

#define USART_CTRLB_CHSIZE_Pos 0u         /* 0 = eight bits */
#define USART_CTRLB_SBMODE     (1u << 6)
#define USART_CTRLB_TXEN       (1u << 16)
#define USART_CTRLB_RXEN       (1u << 17)

#define USART_SYNCBUSY_SWRST   (1u << 0)
#define USART_SYNCBUSY_ENABLE  (1u << 1)
#define USART_SYNCBUSY_CTRLB   (1u << 2)

#define USART_INTFLAG_DRE      (1u << 0)
#define USART_INTFLAG_TXC      (1u << 1)
#define USART_INTFLAG_RXC      (1u << 2)

/* ---- the core ----------------------------------------------------------- *
 * ARMv7-M, so the same block as the other Cortex-M parts in this tree. The
 * DWT and DEMCR addresses are the ones the rig uses on this very die. */
#define SCB_VTOR          REG32(0xE000ED08u)

#define SYST_CSR          REG32(0xE000E010u)
#define SYST_RVR          REG32(0xE000E014u)
#define SYST_CVR          REG32(0xE000E018u)

#define DEMCR             REG32(0xE000EDFCu)
#define DEMCR_TRCENA      (1u << 24)
#define DWT_CTRL          REG32(0xE0001000u)
#define DWT_CTRL_CYCCNTENA (1u << 0)
#define DWT_CYCCNT        REG32(0xE0001004u)

#endif /* SS_REGS_H */
