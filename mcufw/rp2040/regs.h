/* RP2040 registers, by hand, and the accelerator-word packing.
 *
 * Every address and bit position below came from one of two places and
 * nowhere else: the research tree's RP2040 rig (rigs/fw_rp2040_aes/main.c,
 * which has been run on this silicon), and the pico-sdk's own register
 * headers under src/rp2040/hardware_regs/include/hardware/regs/ read for
 * the numbers only. Where the two overlap they agree. Nothing here needs
 * the SDK at build time - the SDK is 300 MB for twenty registers and the
 * toolkit has to stay buildable without it.
 *
 * THIS PART HAS NO INTERNAL FLASH. Code and constants are executed and read
 * in place out of an external QSPI device, over a serial link, through a
 * unified cache. "Flash" in this port means the XIP window at 0x10000000,
 * and a table at 0x2000xxxx is in SRAM exactly as on every other part here.
 *
 * THERE IS NO FLASH_ACR AND NO WAIT-STATE FIELD. The cost of a miss is set
 * by the serial link - the SSI's clock divider BAUDR.SCKDV, the dummy
 * cycles in SPI_CTRLR0.WAIT_CYCLES and RX_SAMPLE_DLY.RSD - and all three
 * are programmed by boot2 before any of this code runs. See the next
 * section for why that puts them outside the accelerator word and inside
 * it at the same time.
 *
 * ---- packing of the opaque accelerator word ----------------------------
 *
 * ss_board.h asks for one opaque word. This part does not have one, so the
 * word is packed, the same escape hatch the H723 uses (see
 * mcufw/stm32h723zg/regs.h). It carries ONE WRITABLE BIT and a READ-ONLY
 * WITNESS of the knob that is not writable at all:
 *
 *     bit  0        XIP_CTRL.EN     the unified cache, on or off.
 *                                   WRITABLE, and the only accelerator bit
 *                                   on this part that is.
 *     bits 8..31    the low 24 bits of the boot2 checksum READ OUT OF
 *                                   FLASH at 0x100000FC. READ-ONLY: ignored
 *                                   on a write, always reassembled from the
 *                                   live read.
 *
 * WHY THE WITNESS IS THERE, which is the whole awkwardness of this part in
 * one field. The miss cost is set by boot2, boot2 is selected at LINK time,
 * and build.mk's variant string - which is shared by every part and is not
 * this port's to extend - cannot carry it. So two images that differ only
 * in their flash read configuration would be filed under the same variant
 * name, and a trace count from one would be quoted against the other. The
 * device therefore reports which boot2 is PHYSICALLY IN FLASH: the last
 * word of the 256-byte boot2 is its CRC-32/MPEG-2 over the preceding 252
 * bytes, so it is a different number for every boot2 ever built, and unlike
 * a name carried in a -D it cannot go stale. The two the port ships:
 *
 *     0x4eb274   boot2_w25q080     quad continuous read - the fast arm
 *     0x4c22ea   boot2_generic_03h plain 03h serial read - the slow arm
 *
 * A caller that replays a word recorded from another build therefore gets a
 * DIFFERENT readback when a different boot2 is in flash, which is the point:
 * a refusal and a changed configuration both show up as "the readback is
 * not what I asked for".
 *
 * WHAT IS DELIBERATELY NOT IN THE WORD, and why, for each one:
 *
 *   XIP_CTRL.POWER_DOWN (bit 3). Powers down the cache SRAM. Setting it
 *   from code that is executing out of the window the cache serves is a way
 *   to stop the part, not a configuration. Refused.
 *
 *   SSI_BAUDR / SPI_CTRLR0.WAIT_CYCLES / RX_SAMPLE_DLY, at 0x18000014,
 *   0x180000F4 and 0x180000F0. THESE ARE THE GAIN KNOB and they are not
 *   reachable from here: the SSI must be disabled (SSIENR = 0) before any
 *   of them can be changed, and disabling the SSI while the core is
 *   fetching instructions through it removes the instruction source. The
 *   SDK changes them only from code it has moved into SRAM first. This port
 *   does not do that - it would mean a second RAM-resident path whose
 *   failure mode is a part that stops with no output - so the knob stays
 *   where the silicon puts it, in boot2, and the word carries the witness
 *   above instead of a field it cannot honestly write. The research tree
 *   measured 2.77x between two boot2 variants, so this is a real axis that
 *   is simply not a register one.
 *
 *   The four per-access aliases, 0x10 (cached, allocating), 0x11 (cached,
 *   non-allocating), 0x12 (non-cached, allocating) and 0x13 (full bypass).
 *   They are addresses, not a register, so they cannot be set - they are
 *   chosen per load by whoever forms the pointer. The table's residence on
 *   this bench is a LINKER property (RAMTAB), and routing the victim's
 *   table reads through a different alias would change the cipher's
 *   instruction stream, which the residence axis exists not to do. They are
 *   a probe tool and they belong in a probe; the research tree's rig uses
 *   all four.
 *
 *   XIP_CTR_HIT and XIP_CTR_ACC. Real hardware hit/miss counters, which
 *   this part has and most parts here do not, so they are exposed - but
 *   through ssb_has_monitors/ssb_monitors_read, which is where the contract
 *   puts counters, not folded into the configuration word.
 *
 * ONE MORE THING ABOUT THIS REGISTER FILE, and it cost the rig a confusing
 * bring-up: DO NOT BLOCK-READ IT. STREAM_FIFO at +0x1C is destructive -
 * reading it pops the FIFO. A debugger `read32 0x14000000 32` during the
 * rig's bring-up returned a word out of a FIFO that STAT said was empty.
 * Nothing here reads +0x1C and nothing here should.
 */
#ifndef SS_REGS_H
#define SS_REGS_H

#include <stdint.h>

/* ---- packing of the opaque accelerator word --------------------------- */
#define RP_ACR_CACHE_EN    0x00000001u   /* XIP_CTRL.EN, writable          */
#define RP_ACR_B2CRC_Msk   0xFFFFFF00u   /* boot2 checksum, read-only      */
#define RP_ACR_B2CRC_Pos   8u
/* The bits a write is allowed to carry at all. Anything else comes back
 * refused, with the old value as the readback. */
#define RP_ACR_MASK        (RP_ACR_CACHE_EN | RP_ACR_B2CRC_Msk)

/* ---- the XIP window and its aliases -----------------------------------
 * Only the first is used by anything in this port; the other three are here
 * so that a reader of board.c does not have to go looking for what 0x11 is,
 * and so that memory.ld's FLASH origin can be checked against a name. */
#define XIP_BASE_ADDR      0x10000000u   /* cached, allocating - normal     */
#define XIP_NOALLOC_ADDR   0x11000000u   /* cached, non-allocating          */
#define XIP_NOCACHE_ADDR   0x12000000u   /* non-cached, allocating          */
#define XIP_BYPASS_ADDR    0x13000000u   /* full bypass                     */

/* The last word of boot2, which is its CRC-32/MPEG-2 over bytes 0..251.
 * Read as data through the normal cached alias; this is a plain flash read
 * and it is only ever done outside a measured region. */
#define BOOT2_CRC_ADDR     (XIP_BASE_ADDR + 0xFCu)
#define BOOT2_CRC          (*(volatile const uint32_t *)BOOT2_CRC_ADDR)

/* ---- XIP cache control (DS 2.6.3.6; SDK regs/xip.h) ------------------- */
#define XIP_CTRL_BASE_REG  0x14000000u
#define XIP_CTRL_REG       (*(volatile uint32_t *)(XIP_CTRL_BASE_REG + 0x00u))
#define XIP_FLUSH_REG      (*(volatile uint32_t *)(XIP_CTRL_BASE_REG + 0x04u))
#define XIP_STAT_REG       (*(volatile uint32_t *)(XIP_CTRL_BASE_REG + 0x08u))
#define XIP_CTR_HIT_REG    (*(volatile uint32_t *)(XIP_CTRL_BASE_REG + 0x0Cu))
#define XIP_CTR_ACC_REG    (*(volatile uint32_t *)(XIP_CTRL_BASE_REG + 0x10u))
/* +0x14 STREAM_ADDR, +0x18 STREAM_CTR, +0x1C STREAM_FIFO. Not defined on
 * purpose: see the note at the top about reading +0x1C. */

#define XIP_CTRL_EN            (1u << 0)
#define XIP_CTRL_ERR_BADWRITE  (1u << 1)
#define XIP_CTRL_POWER_DOWN    (1u << 3)
#define XIP_STAT_FLUSH_READY   (1u << 0)

/* ---- RESETS ------------------------------------------------------------
 * Every peripheral comes out of chip reset held in reset, and a register
 * read of a peripheral still in reset returns zero rather than faulting -
 * which is the "a register that is not there does not always hang, it
 * sometimes lies" shape of the trap in docs/PORTING.md. RESET_DONE is
 * polled for each block this port touches. */
#define RESETS_BASE_REG    0x4000C000u
#define RESETS_RESET       (*(volatile uint32_t *)(RESETS_BASE_REG + 0x00u))
#define RESETS_RESET_DONE  (*(volatile uint32_t *)(RESETS_BASE_REG + 0x08u))
#define RESETS_IO_BANK0    (1u << 5)
#define RESETS_PADS_BANK0  (1u << 8)
#define RESETS_UART0       (1u << 22)

/* ---- XOSC: the 12 MHz crystal ----------------------------------------- */
#define XOSC_BASE_REG      0x40024000u
#define XOSC_CTRL          (*(volatile uint32_t *)(XOSC_BASE_REG + 0x00u))
#define XOSC_STATUS        (*(volatile uint32_t *)(XOSC_BASE_REG + 0x04u))
#define XOSC_STARTUP       (*(volatile uint32_t *)(XOSC_BASE_REG + 0x0Cu))
#define XOSC_CTRL_FREQ_1_15MHZ  0xAA0u
#define XOSC_CTRL_ENABLE        (0xFABu << 12)
#define XOSC_STATUS_STABLE      (1u << 31)

/* ---- CLOCKS ------------------------------------------------------------
 * NOTE CLK_PERI: it has a CTRL and a SELECTED and NO DIVIDER on this part,
 * so SELECTED is at +0x50 and +0x4C is reserved. The RP2350 put a divider
 * in that hole, which is one of the reasons the two ports do not share a
 * header. */
#define CLOCKS_BASE_REG    0x40008000u
#define CLK_REF_CTRL       (*(volatile uint32_t *)(CLOCKS_BASE_REG + 0x30u))
#define CLK_REF_DIV        (*(volatile uint32_t *)(CLOCKS_BASE_REG + 0x34u))
#define CLK_REF_SELECTED   (*(volatile uint32_t *)(CLOCKS_BASE_REG + 0x38u))
#define CLK_SYS_CTRL       (*(volatile uint32_t *)(CLOCKS_BASE_REG + 0x3Cu))
#define CLK_SYS_DIV        (*(volatile uint32_t *)(CLOCKS_BASE_REG + 0x40u))
#define CLK_SYS_SELECTED   (*(volatile uint32_t *)(CLOCKS_BASE_REG + 0x44u))
#define CLK_PERI_CTRL      (*(volatile uint32_t *)(CLOCKS_BASE_REG + 0x48u))
#define CLK_PERI_SELECTED  (*(volatile uint32_t *)(CLOCKS_BASE_REG + 0x50u))

#define CLK_REF_SRC_XOSC       2u          /* CLK_REF_CTRL.SRC             */
#define CLK_SYS_SRC_CLK_REF    0u          /* CLK_SYS_CTRL.SRC             */
#define CLK_PERI_AUXSRC_CLK_SYS 0u         /* CLK_PERI_CTRL.AUXSRC         */
#define CLK_PERI_CTRL_ENABLE   (1u << 11)
#define CLK_DIV_1              (1u << 8)   /* integer 1, fraction 0        */

/* ---- IO_BANK0 and PADS_BANK0 ------------------------------------------
 * One CTRL per pin, 8 bytes apart, FUNCSEL in bits 4..0. UART0 TX/RX are
 * function 2 on GPIO0/GPIO1; SIO (plain software GPIO) is function 5. */
#define IO_BANK0_BASE_REG  0x40014000u
#define IO_CTRL(pin)       (*(volatile uint32_t *)(IO_BANK0_BASE_REG + 0x04u + 8u*(pin)))
#define IO_FUNC_UART       2u
#define IO_FUNC_SIO        5u

#define PADS_BANK0_BASE_REG 0x4001C000u
#define PAD_CTRL(pin)      (*(volatile uint32_t *)(PADS_BANK0_BASE_REG + 0x04u + 4u*(pin)))
#define PAD_IE             (1u << 6)
#define PAD_OD             (1u << 7)

/* ---- SIO: the GPIO the core drives directly --------------------------
 * These offsets are NOT the same on the RP2350, which has 48 pins and
 * therefore two words per register. Do not copy them across. */
#define SIO_BASE_REG       0xD0000000u
#define SIO_GPIO_OUT_SET   (*(volatile uint32_t *)(SIO_BASE_REG + 0x14u))
#define SIO_GPIO_OUT_CLR   (*(volatile uint32_t *)(SIO_BASE_REG + 0x18u))
#define SIO_GPIO_OE_SET    (*(volatile uint32_t *)(SIO_BASE_REG + 0x24u))

/* ---- UART0: an ARM PL011 ---------------------------------------------- */
#define UART0_BASE_REG     0x40034000u
#define UART_DR            (*(volatile uint32_t *)(UART0_BASE_REG + 0x00u))
#define UART_FR            (*(volatile uint32_t *)(UART0_BASE_REG + 0x18u))
#define UART_IBRD          (*(volatile uint32_t *)(UART0_BASE_REG + 0x24u))
#define UART_FBRD          (*(volatile uint32_t *)(UART0_BASE_REG + 0x28u))
#define UART_LCR_H         (*(volatile uint32_t *)(UART0_BASE_REG + 0x2Cu))
#define UART_CR            (*(volatile uint32_t *)(UART0_BASE_REG + 0x30u))
#define UART_FR_RXFE       (1u << 4)
#define UART_FR_TXFF       (1u << 5)
#define UART_LCR_H_FEN     (1u << 4)
#define UART_LCR_H_WLEN8   (3u << 5)
#define UART_CR_UARTEN     (1u << 0)
#define UART_CR_TXE        (1u << 8)
#define UART_CR_RXE        (1u << 9)

/* ---- core --------------------------------------------------------------
 * ARMv6-M. SysTick is here and is the time base; the DWT is advertised by
 * the ROM table and HAS NO CYCLE COUNTER, which is a v7-M feature. The DWT
 * addresses are defined anyway because board.c's TIMING=dwt arm WRITES the
 * enables and checks whether the counter moves - the honest way to report
 * a counter that reads as a plausible zero. */
#define SCB_VTOR           (*(volatile uint32_t *)0xE000ED08u)

#define SYST_CSR           (*(volatile uint32_t *)0xE000E010u)
#define SYST_RVR           (*(volatile uint32_t *)0xE000E014u)
#define SYST_CVR           (*(volatile uint32_t *)0xE000E018u)
#define SYST_CSR_ENABLE    (1u << 0)
#define SYST_CSR_CLKSOURCE (1u << 2)

#define DEMCR              (*(volatile uint32_t *)0xE000EDFCu)
#define DEMCR_TRCENA       (1u << 24)
#define DWT_CTRL           (*(volatile uint32_t *)0xE0001000u)
#define DWT_CTRL_CYCCNTENA (1u << 0)
#define DWT_CYCCNT         (*(volatile uint32_t *)0xE0001004u)

#endif /* SS_REGS_H */
