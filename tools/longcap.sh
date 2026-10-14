#!/bin/bash
# Push the trace count on the configurations that have something to find.
#
# The RAM twins and stock Mbed-TLS are bit-exactly constant, so more data
# buys nothing on them; they stay at 20 000 as controls. This takes the
# four library arms and the reference cipher to 100 000.
set -u
cd "$(dirname "${BASH_SOURCE[0]}")/.."
PROBE=066DFF565271525067103647
N=${N:-100000}
for v in refsbox-sbox-ram0 mbedtls-rom1-few0-ram0 mbedtls-rom1-few1-ram0 \
         wolfssl-few0-ram0 wolfssl-few1-ram0; do
    echo "######## $v  n=$N"
    python tools/capture.py --mcu stm32l152re \
        --variant "$v-vcp-b115200-tsystick-w1-pad0" \
        --probe "$PROBE" -n "$N" \
        --out "results/stm32l152re/$v-vcp-b115200-tsystick-w1-pad0-n$N.npz" \
        || echo "FAILED $v"
done
