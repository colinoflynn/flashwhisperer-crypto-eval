#!/bin/bash
# The claim this repository rests on, CHECKED rather than asserted:
#
#   not one line of library source is modified, and every library checkout
#   is the exact commit libs/PINS names.
#
# That claim is what makes a result hard to argue with, so it is worth a
# script rather than a sentence in a README. It is cheap: run it before any
# capture and before believing any number.
#
# WHAT THIS DOES NOT CHECK, because a green checker that is not checking is
# worse than no checker:
#
#   * That the pinned commit is a commit upstream still publishes. A force
#     push or a deleted tag would leave a checkout here that nothing can
#     reproduce; only a fresh clone on another machine finds that.
#   * That a library was COMPILED the way an axis needs. -fdata-sections is
#     what makes the table-residence axis exist, and a prebuilt archive
#     built without it cannot be given one. `make addr` reading address 0
#     for every slot is the symptom; this script will not see it.
#   * The configuration headers in eval/lib/*/. They are OURS and are meant
#     to differ from upstream - that is the whole mechanism.
#   * Anything about eval/lib/refsbox/, whose source is in this repository
#     on purpose. `git status` is its check.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fail=0

echo "== library checkouts must be pristine at their pinned commits =="
while read -r name url pin version; do
    case "$name" in ''|'#'*) continue ;; esac
    dir="$ROOT/libs/$name"

    if [ ! -e "$dir/.git" ]; then
        echo "  MISSING  $name - run ./libs/fetch.sh"
        fail=1
        continue
    fi

    head=$(git -C "$dir" rev-parse HEAD)
    dirty=$(git -C "$dir" status --porcelain | wc -l | tr -d ' ')

    printf '  %-10s %s  %s modified/untracked\n' \
        "$name" "$(echo "$head" | cut -c1-9)" "$dirty"

    if [ "$head" != "$pin" ]; then
        echo "    FAIL: HEAD is not the pin $pin from libs/PINS."
        echo "          A checkout that has moved is a different measurement"
        echo "          under the same name."
        fail=1
    fi
    if [ "$dirty" != "0" ]; then
        echo "    FAIL: not pristine. The claim that no library source is"
        echo "          modified is the thing this repository rests on."
        git -C "$dir" status --porcelain | sed 's/^/            /'
        fail=1
    fi

    # The gitlink the PARENT records has to agree with PINS too, or a clone
    # of the parent gets a different library than this file advertises.
    gl=$(git -C "$ROOT" ls-tree HEAD "libs/$name" 2>/dev/null | awk '{print $3}')
    if [ -n "$gl" ] && [ "$gl" != "$pin" ]; then
        echo "    FAIL: the parent records $gl for libs/$name, PINS says $pin."
        fail=1
    fi
done < "$ROOT/libs/PINS"

echo
if [ "$fail" = 0 ]; then
    echo "  OK: no library source is modified, and every pin matches."
else
    echo "  NOT OK - see above." >&2
fi
exit $fail
