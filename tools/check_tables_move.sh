#!/bin/bash
# The table slots must exist, and must MOVE for the RAM variant.
#
# Run by CI and runnable by hand. It is the cheapest check that the three
# linker fragments are still wired together, and it catches the failure
# that is otherwise invisible: a tab_*.ld that stopped matching collects
# nothing, the slot reports address 0 at run time, and address 0 is a
# LEGITIMATE answer meaning "this library keeps its tables in RAM". So a
# stale fragment reads as a library with no flash channel.
#
# THE PAIR IS DERIVED, NOT SEARCHED FOR TWICE. It takes each refsbox sbox
# RAMTAB=0 build and renames -ram0- to -ram1- to get its twin, so the two
# ELFs it compares are guaranteed to differ in RAMTAB and nothing else.
# Searching for the arms separately did not guarantee that: '*refsbox*ram0*'
# also matches refsbox-ttable-ram0, and since build.mk grew CLK and
# BOARD_VARIANT it matches -clk8 and -b2generic_03h builds too, so `head -1`
# could pair one build's flash arm with another build's RAM arm. Both arms
# would still have moved and the check would still have passed, which is the
# worst shape for a check to fail in.
#
# WHAT THIS DOES NOT CHECK, because a green checker that is not checking is
# worse than no checker:
#
#   * Any library but refsbox. The pairs that matter to a RESULT are
#     Mbed-TLS at ROMTAB=1, wolfSSL and LibTomCrypt, and none of them is
#     looked at here. refsbox is the control, so a fragment that broke for
#     one library only passes this.
#   * Any slot but the one it names. _tabT and _tabD are not compared, so a
#     tab_t.ld or tab_d.ld that stopped matching passes.
#   * That _tabload stays in flash in both arms. If it did not, the RAM arm
#     would be copying from RAM and the control would be measuring nothing.
#   * That a slot holds the table it is supposed to. Sizes are not read,
#     and an empty slot at a plausible address is indistinguishable here
#     from a full one.
#   * Anything about the clock, the boot2 or any other axis. It builds
#     nothing: it reads what `make matrix` left behind, so a part whose
#     matrix was built before a change still reports the old addresses.
set -u
cd "$(dirname "${BASH_SOURCE[0]}")/.."
fail=0
for mcu in $(ls mcufw); do
    [ -f "mcufw/$mcu/board.mk" ] || continue

    # One line per refsbox sbox RAMTAB=0 build. More than one is normal now:
    # a part with an axis of its own (the RP2040's boot2) or a build at a
    # second clock has one per value, and each has its own twin.
    flashes=$(find "build/$mcu" -name fw.elf -path '*refsbox-sbox-*ram0-*' \
              2>/dev/null | sort)
    if [ -z "$flashes" ]; then
        echo "$mcu: no refsbox sbox RAMTAB=0 build - run 'make matrix MCU=$mcu'"
        fail=1; continue
    fi

    for flash in $flashes; do
        ram=${flash/-ram0-/-ram1-}
        v=$(basename "$(dirname "$flash")")
        if [ ! -f "$ram" ]; then
            echo "$mcu: $v has no RAMTAB=1 twin - run 'make matrix MCU=$mcu'"
            fail=1; continue
        fi
        fa=$(arm-none-eabi-nm "$flash" | awk '/ _tabS$/{print $1}')
        ra=$(arm-none-eabi-nm "$ram"   | awk '/ _tabS$/{print $1}')
        echo "$mcu: S-box flash=0x$fa ram=0x$ra  ($v)"
        if [ -z "$fa" ] || [ -z "$ra" ]; then
            echo "  FAIL: the sections script collected no S-box at all."
            fail=1
        elif [ "$fa" = "$ra" ]; then
            echo "  FAIL: the RAM variant did not move the table. The residence"
            echo "        control is what separates a flash channel from an"
            echo "        instrument artefact; if it does not move, both arms"
            echo "        measure the same thing."
            fail=1
        fi
    done
done
[ "$fail" = 0 ] && echo "OK: every part's table slot exists and moves."
exit $fail
