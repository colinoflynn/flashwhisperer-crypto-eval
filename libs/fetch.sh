#!/bin/bash
# Fetch every pinned library, SHALLOW AND SPARSE AND AT THE PIN.
#
#   ./libs/fetch.sh              # all of them
#   ./libs/fetch.sh mbedtls      # just one
#   ./libs/fetch.sh --size       # report what is on disk, fetch nothing
#
# These are submodules: libs/<name> is a gitlink and the parent records the
# exact commit. But they are NOT fetched the way submodules usually are, for
# two measured reasons.
#
# WHY NOT `git submodule update --init --depth 1`. That clones the default
# BRANCH at depth 1, and every pin here is an older release tag that is not
# in it. The result is a checkout of some other version under the right
# directory name - and for Mbed-TLS it lands on the 4.x line, which has no
# library/aes.c at all, so the build silently measures nothing. Not
# hypothetical: it is what the first attempt at this repository did.
#
# WHY NOT `git submodule add` EITHER. It clones the whole history. wolfSSL's
# is just over a gigabyte, and this tree lives in a synced folder. The first
# attempt put 1 GB of pack files in Dropbox before anyone looked.
#
# So each one is an empty repository with one remote, a sparse-checkout set
# to the directories the build actually compiles from, and a single
# depth-1 fetch of the pinned COMMIT - which GitHub allows. Exact, small,
# and it cannot drift.
#
# NOTHING IS VENDORED AND NOTHING IS PATCHED. The submodules are upstream's
# own bytes at a stated commit. Every configuration this repository builds
# is reached through the library's own documented mechanism, from
# eval/lib/<name>/lib.mk, or through the linker - never by editing library
# source. tools/verify.sh checks that rather than asserting it.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"

# Only the directories the build compiles from. Cone mode, so these are
# directory prefixes. Everything else stays in the object store and off the
# disk.
sparse_for() {
    case "$1" in
      mbedtls) echo "library include" ;;
      wolfssl) echo "wolfcrypt/src wolfssl" ;;
      *)       echo "" ;;
    esac
}

if [ "${1:-}" = "--size" ]; then
    while read -r name _ _ _; do
        case "$name" in ''|'#'*) continue ;; esac
        w=$(du -sh "$HERE/$name" 2>/dev/null | cut -f1 || echo -)
        g=$(du -sh "$ROOT/.git/modules/$name" 2>/dev/null | cut -f1 || echo -)
        printf '%-10s worktree %-7s objects %s\n' "$name" "$w" "$g"
    done < "$HERE/PINS"
    exit 0
fi

want=("$@")

while read -r name url pin version; do
    case "$name" in ''|'#'*) continue ;; esac
    if [ ${#want[@]} -gt 0 ] && [[ ! " ${want[*]} " =~ " ${name} " ]]; then
        continue
    fi

    dir="$HERE/$name"
    echo "== $name $version @ ${pin:0:9}"

    if [ ! -e "$dir/.git" ]; then
        mkdir -p "$dir"
        git -C "$dir" init -q
        git -C "$dir" remote add origin "$url"
    fi
    git -C "$dir" config core.longpaths true

    read -r -a sparse <<< "$(sparse_for "$name")"
    if [ ${#sparse[@]} -gt 0 ]; then
        git -C "$dir" config core.sparseCheckout true
        git -C "$dir" sparse-checkout init --cone
        git -C "$dir" sparse-checkout set "${sparse[@]}"
    fi

    if ! git -C "$dir" cat-file -e "${pin}^{commit}" 2>/dev/null; then
        git -C "$dir" fetch -q --depth 1 origin "$pin"
    fi
    git -C "$dir" checkout -q --detach "$pin"

    # The version string is ASSERTED, not trusted. A pin that resolves to a
    # different tree than the build was written against is silent otherwise.
    case "$name" in
      mbedtls)
        f="$dir/include/mbedtls/build_info.h"
        rx="define[[:space:]]+MBEDTLS_VERSION_STRING[[:space:]]+\"$version\"" ;;
      wolfssl)
        f="$dir/wolfssl/version.h"
        rx="define[[:space:]]+LIBWOLFSSL_VERSION_STRING[[:space:]]+\"$version\"" ;;
      *) f=""; rx="" ;;
    esac
    if [ -n "$f" ]; then
        if [ ! -f "$f" ]; then
            echo "FAIL: $f missing - this is not a $name tree, or the" >&2
            echo "      sparse set does not cover it." >&2
            exit 1
        fi
        if ! grep -Eq "$rx" "$f"; then
            echo "FAIL: $f does not declare $name $version." >&2
            echo "      The pin resolved to a different tree than this build" >&2
            echo "      was written against. Re-read it before trusting" >&2
            echo "      anything measured from it." >&2
            exit 1
        fi
        echo "   $name $version verified"
    fi

    # The one file per library whose CONTENT an axis depends on. If upstream
    # stops gating its tables on the macro eval/lib/<name>/lib.mk sets, that
    # axis is inert and every result on it is a null for the wrong reason.
    case "$name" in
      mbedtls) guard="MBEDTLS_AES_ROM_TABLES"; src="$dir/library/aes.c" ;;
      wolfssl) guard="WOLFSSL_AES_SMALL_TABLES"; src="$dir/wolfcrypt/src/aes.c" ;;
      *) guard=""; src="" ;;
    esac
    if [ -n "$guard" ] && [ -f "$src" ] && ! grep -q "$guard" "$src"; then
        echo "WARN: $(basename "$src") no longer mentions $guard;" >&2
        echo "      the corresponding axis in eval/lib/$name/lib.mk may now" >&2
        echo "      do nothing at all." >&2
    fi
done < "$HERE/PINS"

echo
echo "Record the pins in the parent:  git add libs/"
echo "Build with e.g.:  ./tools/build.sh MCU=stm32l152re LIB=mbedtls ROMTAB=1"
