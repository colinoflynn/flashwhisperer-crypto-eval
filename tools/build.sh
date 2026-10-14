#!/bin/bash
# Build one variant.
#
#   ./tools/build.sh MCU=stm32l152re LIB=mbedtls ROMTAB=1
#   ./tools/build.sh MCU=stm32l152re LIB=refsbox V=1     # show the commands
#
# A .sh file rather than a bare `wsl -- bash -lc` one-liner because variables
# inside that construct expand to empty, which has cost a debugging round
# before now.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
quiet=-s
for a in "$@"; do [ "$a" = "V=1" ] && quiet=; done
exec make $quiet -f build.mk "$@"
