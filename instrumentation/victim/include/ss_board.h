/* THE BOARD CONTRACT. Everything a new part has to supply, and nothing else.
 *
 * One file per part under mcufw/<part>/ implements this; the protocol loop
 * (src/ss_victim.c) and the cipher provider (eval/lib/<name>/provider.c) are
 * built unmodified for every part. A host that speaks only the protocol needs
 * no change for a new target, and a fix lands once.
 *
 * WHY THE CONTRACT IS THIS SHAPE. It is the intersection of two probes that
 * were written independently against the same bench -- a library victim and a
 * synthetic flash-arbitration kernel -- and then found to want nearly the
 * same services. Everything both needed is here; everything only one needed
 * is behind an `ssb_has_*` query that returns 0 by default.
 *
 * WHY THE HOOKS ARE THIS SHORT. Everything inside the measured window has to
 * be the library's own code plus at most two GPIO writes. So the board layer
 * owns the clock, the console, the trigger pin, the accelerator and the cycle
 * counter; the provider owns exactly "encrypt one block"; and nothing else is
 * allowed in between.
 *
 * THE `ss` PREFIX is historical -- this grew out of a serial-victim rig -- and
 * is kept because the code under it is the version that has been run on
 * silicon. Renaming it is cheap and has not been done, so that a diff against
 * the rig it came from still reads.
 *
 * THREE OF THESE ARE EASY TO GET SUBTLY WRONG, and each failure looks like a
 * measurement rather than a fault. They are called out at the declaration.
 */
#ifndef SS_BOARD_H
#define SS_BOARD_H

#include <stdint.h>

/* ---- identification ---------------------------------------------------- */
/* Short tag, e.g. "stm32l152re". Reported so a capture records the board it
 * ran on rather than the directory someone meant to build. */
const char *ssb_name(void);

/* ---- bring-up ---------------------------------------------------------- */
void     ssb_init(void);            /* clock, console, trigger, counter      */

/* ---- console ----------------------------------------------------------- */
void     ssb_put(uint8_t c);
int      ssb_get(void);             /* blocking                              */

/* ---- the trigger marker ------------------------------------------------- */
/* Two register writes, no more. These bracket the cipher in the arm that has
 * a marker; the arm that does not never calls them. */
void     ssb_trig_high(void);
void     ssb_trig_low(void);

/* ---- the flash accelerator, as ONE OPAQUE WORD --------------------------
 * The host never interprets it; it is echoed back by SS_V_IDENT and
 * SS_V_SET_ACR so a capture records exactly what was set. On an STM32 it is
 * FLASH_ACR. On a part whose accelerator is a separate peripheral the board
 * file packs the fields into one word and documents the packing in its own
 * header -- the protocol layer must not have to know which kind of part it
 * is talking to.
 *
 * TRAP 1: `ssb_acr_set` MUST REFUSE a value this clock cannot run, rather
 * than execute the next instruction fetch under it. Measured the hard way:
 * writing zero wait states at 32 MHz on an STM32L152, where the reference
 * manual makes one mandatory, stopped the part dead mid-script with four
 * checks already passed, and from the host it looked like an unrelated crash.
 * SS_V_SET_ACR replies with the READBACK, never the requested value, so a
 * caller learns it was refused.
 *
 * TRAP 2: on a part with caches, leaving the accelerator "in the state a
 * reset-time write would" means RESETTING the caches while they are disabled,
 * not just writing the enable bit. Omitting that has invalidated a whole
 * round of results before now. Do the dance in the board file. */
void     ssb_acr_set(uint32_t v);
uint32_t ssb_acr_get(void);

/* ---- the core clock ----------------------------------------------------- */
uint32_t ssb_clk_hz(void);
/* Optional. Return 0 if the part has one clock; the host then treats a clock
 * change as "not available here" rather than as an error. */
int      ssb_set_mhz(uint32_t mhz);

/* ---- the cycle counter --------------------------------------------------
 * TRAP 3: THE BOARD OWNS THE SUBTRACTION. A free-running counter at core rate
 * that can be read with a single load -- but it may count UP (Cortex-M DWT
 * CYCCNT) or DOWN over 24 bits (SysTick), so the caller must not assume
 * `second - first`. This hook exists because the first part ported counted
 * down, and a caller that assumed otherwise would have produced large
 * positive garbage rather than an error.
 *
 * ssb_has_cyc() == 0 is a legitimate build (the arm that carries no
 * instrumentation at all). SS_V_ENC_CYC then answers with the error response
 * rather than with zeros, so the host learns it from the device instead of
 * reading a column of zeros as a constant-time result. */
int      ssb_has_cyc(void);
uint32_t ssb_cyc_read(void);
uint32_t ssb_cyc_delta(uint32_t first, uint32_t second);

/* ---- interrupts ---------------------------------------------------------
 * Masked around the measured region even on a build that enables none: the
 * cost is two instructions OUTSIDE the trigger window and it removes the
 * question entirely, which is cheaper than answering it in a write-up. */
void     ssb_irq_off(void);
void     ssb_irq_on(void);

/* ---- what the provider's table layout is, for the record ----------------
 * 's' byte S-box, 't' word T-tables, 'c' one rotated table, '?' unknown. */
char     ssb_layout(void);

/* ---- optional: hardware cache hit/miss counters -------------------------
 * Where the part has them. Return 0 from ssb_has_monitors() when absent and
 * the host reports "none" instead of inventing numbers. */
int      ssb_has_monitors(void);
void     ssb_monitors_reset(void);
void     ssb_monitors_read(uint32_t *hit, uint32_t *miss);

#endif /* SS_BOARD_H */
