/* RP2350 registers, by hand, and the accelerator-word packing.
 *
 * Every address and bit position below came from one of two places and
 * nowhere else: the research tree's RP2350 rig (rigs/fw_rp2350_aes/main.c,
 * which has been run on this silicon), and the pico-sdk 2.x register headers
 * under src/rp2350/hardware_regs/include/hardware/regs/ read for the numbers
 * only. Where the two overlap they agree. Nothing here needs the SDK at
 * build time.
 *
 * ================= THIS IS NOT AN RP2040 WITH A BIGGER NUMBER =============
 *
 * Do not read this header as the RP2040's with the constants bumped. The two
 * parts share a vendor, a package idea and an XIP cache of the same nominal
 * size, and almost nothing else about the flash path or the peripheral map.
 * Four of the differences below produce code that assembles, runs, and
 * measures something else entirely.
 *
 *   1. 0x18000000 WAS THE SSI REGISTER BLOCK ON THE RP2040. It is the CACHE
 *      MAINTENANCE WINDOW here. The RP2040 rig reads SSI_BAUDR at 0x18000014
 *      in its ident block; the same read on this part is a maintenance
 *      operation against flash offset 0x14. This is the single most
 *      dangerous line to copy across, which is why the serial link is
 *      spelled out below as the QMI and the maintenance window is named.
 *
 *   2. XIP_CTRL MOVED, from 0x14000000 to 0x400C8000. 0x14000000 is now the
 *      UNCACHED FLASH WINDOW, so the RP2040's base used here would read
 *      flash contents as register values - and erased flash reads 0xFF,
 *      which would report every control bit set.
 *
 *   3. THE SIO REGISTER OFFSETS MOVED, because this part has 48 GPIOs and
 *      therefore two words per register. OUT_SET is +0x18 not +0x14, OUT_CLR
 *      is +0x20 not +0x18, OE is +0x30 not +0x20. RP2040 GPIO code carried
 *      across clears the pin it means to set.
 *
 *   4. THE PADS COME OUT OF RESET ISOLATED. PADS_BANK0.GPIOn.ISO resets to 1
 *      and the pad does nothing until it is cleared. The RP2040 has no such
 *      bit, so an RP2040 UART bring-up copied here configures everything
 *      correctly and sends nothing.
 *
 *   5. THE CLOCK DIVIDERS CHANGED SHAPE. An RP2040 divisor is INT in bits
 *      31:8 and FRAC in 7:0; here it is INT in 31:16 and FRAC in 15:0. So
 *      the RP2040's "divide by one", 1 << 8, means INT = 0 with a fractional
 *      part here - a divisor that is not one, a core clock that is not what
 *      ssb_clk_hz reports, and a UART that frames.
 *
 * And the reset bit positions are different again (UART0 is bit 26 here and
 * bit 22 there), and CLK_PERI gained a divider, which moves nothing but
 * fills the RP2040's reserved hole at +0x4C.
 *
 * ---- packing of the opaque accelerator word ----------------------------
 *
 * ss_board.h asks for one opaque word. This part does not have one, so the
 * word is packed, the same escape hatch the H723 uses. UNLIKE THE RP2040,
 * EVERY FIELD IN IT IS WRITABLE AT RUNTIME, which is the single most useful
 * thing about this part:
 *
 *     bits 0..7    QMI_M0_TIMING.CLKDIV   flash SCLK divider for memory
 *                                         window 0. Reset value 4.
 *     bits 8..31   XIP_CTRL << 8, masked to the three bits this port allows:
 *       bit 8        XIP_CTRL.EN_SECURE     (XIP_CTRL bit 0)
 *       bit 9        XIP_CTRL.EN_NONSECURE  (XIP_CTRL bit 1)
 *       bit 17       XIP_CTRL.SPLIT_WAYS    (XIP_CTRL bit 9)
 *
 * The high half is XIP_CTRL shifted left by eight and nothing else, which is
 * the only packing worth having here: there is one rule to remember rather
 * than three bit positions to get wrong, and the shift is what keeps CLKDIV
 * in the byte the register already gives it. NOTE THE CONSEQUENCE: SPLIT_WAYS
 * is bit 9 of XIP_CTRL and bit 17 OF THE WORD. A word written with 0x200
 * meaning "split" sets nothing.
 *
 * CLKDIV IS THE CLOSEST THING ON EITHER RP PART TO AN ST WAIT STATE, and it
 * is in the word for exactly that reason. On the RP2040 the equivalent lives
 * in boot2 and can only be varied by relinking; here it is one register
 * write, and the ledger's claim that wait states are a gain factor rather
 * than information is directly testable on it. The SDK's own register
 * documentation says CLKDIV may be changed while the QMI is accessing that
 * window - it is the one field in M0_TIMING that may - so this is a live
 * knob, not a reset-time one.
 *
 * SPLIT_WAYS IS THE OTHER REASON THIS WORD IS INTERESTING. It routes Secure
 * accesses to way 0 and Non-secure accesses to way 1, turning one 2-way
 * cache into two direct-mapped half-caches. A direct-mapped cache is the
 * structure that makes collisions deterministic, so this is a configuration
 * that could plausibly CREATE a channel on a part that does not otherwise
 * have one, which is the interesting direction and is not something any
 * other part on this bench can do to itself.
 *
 * WHAT IS DELIBERATELY NOT IN THE WORD, and why, for each one:
 *
 *   XIP_CTRL.POWER_DOWN (bit 3, word bit 11). Powers down the cache SRAM.
 *   From code executing out of the window the cache serves, that is a way to
 *   stop the part, not a configuration. Refused.
 *
 *   XIP_CTRL.MAINT_NONSEC, NO_UNCACHED_*, NO_UNTRANS_*, WRITABLE_M0/M1.
 *   Access-control and write-through policy for a Non-secure or a writable
 *   XIP. This port builds one ARM Secure image that never issues a
 *   Non-secure access and never writes flash, so none of them can reach the
 *   path being measured. Leaving them writable would let a sweep record a
 *   configuration that changed nothing under a name that suggests it did.
 *
 *   THE REST OF QMI_M0_TIMING - RXDELAY, MIN_DESELECT, MAX_SELECT,
 *   SELECT_HOLD, PAGEBREAK, COOLDOWN - and M0_RFMT/M0_RCMD. These are the
 *   rest of the flash read configuration and they are NOT in the word
 *   because the SDK's own documentation for the register says every
 *   parameter except CLKDIV must only be changed WHILE THE QMI IS IDLE, and
 *   the QMI is never idle while the core is fetching instructions through
 *   it. Reaching them means a RAM-resident path that stops the instruction
 *   source and restarts it, which this port does not have and which, on a
 *   part nothing has run yet, would be a failure mode with no output. CLKDIV
 *   alone is the field the silicon says may be written live, so CLKDIV alone
 *   is in the word.
 *
 *   The address windows - 0x10000000 cached, 0x14000000 uncached,
 *   0x18000000 maintenance, 0x1C000000 uncached-untranslated. They are
 *   addresses, not a register. Note that the RP2040's 0x11/0x12/0x13 are
 *   NOT unmapped here: they are inside the 64 MB cached window and alias
 *   FLASH AT A DIFFERENT OFFSET, so RP2040 probe code carried across reads
 *   valid data from the wrong address and reports a clean null.
 *
 *   0x13FFC000, which the SDK's address map calls XIP_SRAM_BASE. THERE IS NO
 *   CACHE-AS-SRAM WINDOW ON THIS PART. The RP2040 could be told to stop
 *   being a cache and answer as a 16 KB SRAM bank at 0x15000000, which gave
 *   the table a third residence class; nothing in the SDK references this
 *   part's define and the substitute here is line PINNING, a maintenance
 *   write with operation 7. The define is noted so that the next reader does
 *   not take it for the RP2040's window.
 *
 *   XIP_CTR_HIT and XIP_CTR_ACC. Real hardware hit/miss counters, so they
 *   are exposed - but through ssb_has_monitors/ssb_monitors_read, which is
 *   where the contract puts counters, not folded into a configuration word.
 *
 * THERE IS NO XIP_CTRL.FLUSH ON THIS PART. Invalidating is a sweep of byte
 * writes into the maintenance window, 2048 of them for the whole cache, with
 * the operation encoded in the low address bits. See board.c.
 *
 * DO NOT BLOCK-READ THIS REGISTER FILE. STREAM_FIFO at +0x1C is destructive -
 * reading it pops the FIFO. It is not defined below for that reason.
 */
#ifndef SS_REGS_H
#define SS_REGS_H

#include <stdint.h>

/* ---- packing of the opaque accelerator word --------------------------- */
#define RP_ACR_CLKDIV_Msk  0x000000FFu
#define RP_ACR_XIP_Pos     8u
/* The XIP_CTRL bits this port allows, in XIP_CTRL's own positions. */
#define RP_XIP_ALLOWED     (XIP_CTRL_EN_SECURE | XIP_CTRL_EN_NONSECURE \
                            | XIP_CTRL_SPLIT_WAYS)
#define RP_ACR_MASK        (RP_ACR_CLKDIV_Msk | (RP_XIP_ALLOWED << RP_ACR_XIP_Pos))

/* ---- the XIP address windows (DS 4.4.1) -------------------------------
 * 64 MB apart, so the offset within a window is 26 bits - NOT the RP2040's
 * 24. A 24-bit mask here would silently fold a 16 MB+ offset. */
#define XIP_BASE_ADDR      0x10000000u   /* cached, allocating - normal     */
#define XIP_UNCACHED_ADDR  0x14000000u   /* uncached                        */
#define XIP_MAINT_ADDR     0x18000000u   /* CACHE MAINTENANCE, not the SSI  */
#define XIP_UNTRANS_ADDR   0x1C000000u   /* uncached, untranslated          */

/* Cache geometry, as the SDK states it and as the research tree measured it -
 * and here, unlike on the RP2040, the two agree: 16 KB, 8-byte lines, 2-way,
 * 1024 sets. Used by the invalidate sweep in board.c. */
#define XIP_CACHE_BYTES    (16u * 1024u)
#define XIP_CACHE_LINE     8u
#define XIP_WINDOW_BYTES   0x04000000u   /* 64 MB per window                */
/* The maintenance operation encodings, in the low bits of the maintenance
 * address: 0 invalidate by set/way, 1 clean by set/way, 2 invalidate by
 * address, 3 clean by address, 7 pin at address. Only the first is used. */
#define XIP_MAINT_INVAL_SETWAY 0u

/* ---- XIP cache control (DS 4.4; SDK regs/xip.h) -----------------------
 * NOTE THE BASE: 0x400C8000, in the peripheral region. See difference 2. */
#define XIP_CTRL_BASE_REG  0x400C8000u
#define XIP_CTRL_REG       (*(volatile uint32_t *)(XIP_CTRL_BASE_REG + 0x00u))
#define XIP_STAT_REG       (*(volatile uint32_t *)(XIP_CTRL_BASE_REG + 0x08u))
#define XIP_CTR_HIT_REG    (*(volatile uint32_t *)(XIP_CTRL_BASE_REG + 0x0Cu))
#define XIP_CTR_ACC_REG    (*(volatile uint32_t *)(XIP_CTRL_BASE_REG + 0x10u))
/* +0x14 STREAM_ADDR, +0x18 STREAM_CTR, +0x1C STREAM_FIFO - see the note at
 * the top about reading +0x1C. */

#define XIP_CTRL_EN_SECURE     (1u << 0)
#define XIP_CTRL_EN_NONSECURE  (1u << 1)
#define XIP_CTRL_POWER_DOWN    (1u << 3)
#define XIP_CTRL_MAINT_NONSEC  (1u << 8)
#define XIP_CTRL_SPLIT_WAYS    (1u << 9)

/* ---- the serial link: QMI, not SSI (DS 12.14) -------------------------
 * Only M0_TIMING is touched, and only its CLKDIV field. The other windows
 * and the direct-mode CSR are not defined: nothing here may use them, for
 * the reason in the packing note above. */
#define QMI_BASE_REG       0x400D0000u
#define QMI_M0_TIMING      (*(volatile uint32_t *)(QMI_BASE_REG + 0x0Cu))
#define QMI_M0_TIMING_CLKDIV_Msk 0x000000FFu
/* The reset value, which is what the bootrom's XIP setup leaves behind
 * unless a boot2 changed it - and this port links no boot2. */
#define QMI_CLKDIV_RESET   4u
/* THE FLOOR IS 2, NOT 1. Divisor 1 is legal in the register and is the one
 * setting that can stop the flash answering at all, at which point the next
 * instruction fetch has nowhere to come from and the part needs a BOOTSEL
 * cycle. The research tree's rig CLAMPS to 2; this port REFUSES below 2
 * instead, because the contract makes a refusal visible - SS_V_SET_ACR
 * replies with the readback, so the host learns the word did not take rather
 * than recording a configuration that was quietly changed under it. */
#define QMI_CLKDIV_MIN     2u

/* ---- RESETS ------------------------------------------------------------
 * The bit positions are NOT the RP2040's: this part has more peripherals and
 * they are in alphabetical slots, so everything above `dma` has moved. */
#define RESETS_BASE_REG    0x40020000u
#define RESETS_RESET       (*(volatile uint32_t *)(RESETS_BASE_REG + 0x00u))
#define RESETS_RESET_DONE  (*(volatile uint32_t *)(RESETS_BASE_REG + 0x08u))
#define RESETS_IO_BANK0    (1u << 6)
#define RESETS_PADS_BANK0  (1u << 9)
#define RESETS_UART0       (1u << 26)

/* ---- XOSC: the 12 MHz crystal ----------------------------------------- */
#define XOSC_BASE_REG      0x40048000u
#define XOSC_CTRL          (*(volatile uint32_t *)(XOSC_BASE_REG + 0x00u))
#define XOSC_STATUS        (*(volatile uint32_t *)(XOSC_BASE_REG + 0x04u))
#define XOSC_STARTUP       (*(volatile uint32_t *)(XOSC_BASE_REG + 0x0Cu))
#define XOSC_CTRL_FREQ_1_15MHZ  0xAA0u
#define XOSC_CTRL_ENABLE        (0xFABu << 12)
#define XOSC_STATUS_STABLE      (1u << 31)

/* ---- CLOCKS ------------------------------------------------------------
 * CLK_PERI HAS A DIVIDER ON THIS PART, at +0x4C, which is the RP2040's
 * reserved hole. SELECTED is at +0x50 on both, so the two maps agree by
 * accident on that one and disagree on the register between them. */
#define CLOCKS_BASE_REG    0x40010000u
#define CLK_REF_CTRL       (*(volatile uint32_t *)(CLOCKS_BASE_REG + 0x30u))
#define CLK_REF_DIV        (*(volatile uint32_t *)(CLOCKS_BASE_REG + 0x34u))
#define CLK_REF_SELECTED   (*(volatile uint32_t *)(CLOCKS_BASE_REG + 0x38u))
#define CLK_SYS_CTRL       (*(volatile uint32_t *)(CLOCKS_BASE_REG + 0x3Cu))
#define CLK_SYS_DIV        (*(volatile uint32_t *)(CLOCKS_BASE_REG + 0x40u))
#define CLK_SYS_SELECTED   (*(volatile uint32_t *)(CLOCKS_BASE_REG + 0x44u))
#define CLK_PERI_CTRL      (*(volatile uint32_t *)(CLOCKS_BASE_REG + 0x48u))
#define CLK_PERI_DIV       (*(volatile uint32_t *)(CLOCKS_BASE_REG + 0x4Cu))
#define CLK_PERI_SELECTED  (*(volatile uint32_t *)(CLOCKS_BASE_REG + 0x50u))

#define CLK_REF_SRC_XOSC        2u
#define CLK_SYS_SRC_CLK_REF     0u
#define CLK_PERI_AUXSRC_CLK_SYS 0u
#define CLK_PERI_CTRL_ENABLE   (1u << 11)
#define CLK_DIV_1              (1u << 16)  /* RP2350 divisors are INT<<16  */

/* ---- IO_BANK0 and PADS_BANK0 ------------------------------------------
 * One CTRL per pin, 8 bytes apart, FUNCSEL in bits 4..0, as on the RP2040 -
 * but at a different base, and with the ISO bit on the pad side. */
#define IO_BANK0_BASE_REG  0x40028000u
#define IO_CTRL(pin)       (*(volatile uint32_t *)(IO_BANK0_BASE_REG + 0x04u + 8u*(pin)))
#define IO_FUNC_UART       2u
#define IO_FUNC_SIO        5u

#define PADS_BANK0_BASE_REG 0x40038000u
#define PAD_CTRL(pin)      (*(volatile uint32_t *)(PADS_BANK0_BASE_REG + 0x04u + 4u*(pin)))
#define PAD_IE             (1u << 6)
#define PAD_OD             (1u << 7)
/* RESETS TO 1 AND THE PAD IS DEAD UNTIL IT IS CLEARED. See difference 4. */
#define PAD_ISO            (1u << 8)

/* ---- SIO: the GPIO the core drives directly --------------------------
 * 48 pins, two words per register, so these offsets are NOT the RP2040's.
 * Only the low word is used; the trigger and console pins are all below 32. */
#define SIO_BASE_REG       0xD0000000u
#define SIO_GPIO_OUT_SET   (*(volatile uint32_t *)(SIO_BASE_REG + 0x18u))
#define SIO_GPIO_OUT_CLR   (*(volatile uint32_t *)(SIO_BASE_REG + 0x20u))
#define SIO_GPIO_OE_SET    (*(volatile uint32_t *)(SIO_BASE_REG + 0x38u))

/* ---- UART0: an ARM PL011, at a different base ------------------------- */
#define UART0_BASE_REG     0x40070000u
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

/* ---- the picobin IMAGE_DEF block -------------------------------------
 * WITHOUT THESE FIVE WORDS SOMEWHERE IN THE FIRST 4 KB OF THE IMAGE, THIS
 * PART DOES NOT BOOT. The RP2350 bootrom does not look for an RP2040-style
 * checksummed boot2 at offset 0; it searches the start of flash for a block
 * loop and boots the IMAGE_DEF it finds. No block, no image, and from the
 * host that is a board that never says hello.
 *
 * Four of the five are byte-for-byte what the research tree's RP2350 rig
 * carries - read out of its built image at 0x10000138 - and the fifth is
 * the link, which is 0 here because this is a single-block loop pointing at
 * itself. The SDK's own embedded_start_block.inc.S says so in those words.
 *
 *   MARKER_START   0xFFFFDED3
 *   item           IMAGE_TYPE (0x42), one word, flags 0x1021 =
 *                  EXE | SECURITY_S | CPU_ARM | CHIP_RP2350
 *   item           LAST (0xFF), total item size one word
 *   link           relative offset to the next block, 0 = this one
 *   MARKER_END     0xAB123579
 *
 * THERE IS NO ENTRY_POINT ITEM, which is deliberate and is why the vector
 * table has to be at the very start of the image: with no entry point the
 * bootrom takes the initial SP and PC from the first two words. memory.ld
 * pins the table at 0x10000000 for that reason. A RISC-V image DOES need an
 * entry point - the SDK adds one - which is one more reason this port is
 * ARM-only. */
#define PICOBIN_MARKER_START 0xFFFFDED3u
#define PICOBIN_IMAGE_TYPE   0x10210142u
#define PICOBIN_ITEM_LAST    0x000001FFu
#define PICOBIN_SELF_LOOP    0x00000000u
#define PICOBIN_MARKER_END   0xAB123579u

/* ---- core --------------------------------------------------------------
 * Cortex-M33, so there IS a real 32-bit DWT cycle counter counting up -
 * which is the opposite of the RP2040's M0+. Both time bases are wired up
 * and both are checked rather than assumed. */
#define SCB_VTOR           (*(volatile uint32_t *)0xE000ED08u)

#define SYST_CSR           (*(volatile uint32_t *)0xE000E010u)
#define SYST_RVR           (*(volatile uint32_t *)0xE000E014u)
#define SYST_CVR           (*(volatile uint32_t *)0xE000E018u)
#define SYST_CSR_ENABLE    (1u << 0)
#define SYST_CSR_CLKSOURCE (1u << 2)

#define DEMCR              (*(volatile uint32_t *)0xE000EDFCu)
#define DEMCR_TRCENA       (1u << 24)
#define DWT_LAR            (*(volatile uint32_t *)0xE0001FB0u)
#define DWT_LAR_KEY        0xC5ACCE55u
#define DWT_CTRL           (*(volatile uint32_t *)0xE0001000u)
#define DWT_CTRL_CYCCNTENA (1u << 0)
#define DWT_CYCCNT         (*(volatile uint32_t *)0xE0001004u)

#endif /* SS_REGS_H */
