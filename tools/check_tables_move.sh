#!/bin/bash
# The table slots must exist, and must MOVE for the RAM variant.
#
# Run by CI and runnable by hand. It is the cheapest check that the three
# linker fragments are still wired together, and it catches the failure
# that is otherwise invisible: a tab_*.ld that stopped matching collects
# nothing, the slot reports address 0 at run time, and address 0 is a
# LEGITIMATE answer meaning "this library keeps its tables in RAM". So a
# stale fragment reads as a library with no flash channel.
set -u
cd "$(dirname "${BASH_SOURCE[0]}")/.."
fail=0
for mcu in $(ls mcufw); do
    flash=$(find "build/$mcu" -name fw.elf -path '*refsbox*ram0*' 2>/dev/null | head -1)
    ram=$(find "build/$mcu" -name fw.elf -path '*refsbox*ram1*' 2>/dev/null | head -1)
    if [ -z "$flash" ] || [ -z "$ram" ]; then
        echo "$mcu: no refsbox flash/RAM pair built -- run `make matrix MCU=$mcu`"
        fail=1; continue
    fi
    fa=$(arm-none-eabi-nm "$flash" | awk '/ _tabS$/{print $1}')
    ra=$(arm-none-eabi-nm "$ram"   | awk '/ _tabS$/{print $1}')
    echo "$mcu: S-box flash=0x$fa ram=0x$ra"
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
[ "$fail" = 0 ] && echo "OK: every part's table slot exists and moves."
exit $fail
