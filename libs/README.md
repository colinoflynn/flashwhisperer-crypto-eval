# The libraries under test

*🤖WARNING🤖: This file LLM generated and may read oddly. Will eventually be human-edited for that real-life touch and typos.*

Each one is upstream's own bytes at a pinned commit, as a git submodule,
fetched shallow and sparse by [`fetch.sh`](fetch.sh). The pins and the
reason for each are in [`PINS`](PINS).

```bash
./libs/fetch.sh            # all of them, at the pin
./libs/fetch.sh wolfssl    # one
./libs/fetch.sh --size     # what is on disk
../tools/verify.sh         # nothing has been modified, every pin matches
```

## Nothing here is patched

There's no `patches/` directory and nothing to apply. Every configuration
this repository builds is reached through the library's own documented
mechanism - Mbed-TLS's `MBEDTLS_CONFIG_FILE`, wolfSSL's
`WOLFSSL_USER_SETTINGS` - or through the linker, which moves tables
without touching code. The per-library configuration headers live in
`eval/lib/<name>/`, which is ours; the submodule is theirs.

That claim is what makes a result hard to argue with, so it's checked
rather than asserted: `tools/verify.sh` fails if any checkout has a
modified file or has moved off its pin.

## Why the fetch is not `git submodule update --init`

Two measured reasons, both of which bit during setup:

* `--depth 1` clones the default BRANCH, and every pin here is an older
  release tag that isn't in it. For Mbed-TLS that lands on the 4.x line,
  which has no `library/aes.c` at all (AES moved to the separate
  `tf-psa-crypto` repository), so the build silently measures nothing.
* `git submodule add` clones the whole history. wolfSSL's is just over a
  gigabyte, and this tree lives in a synced folder.

So `fetch.sh` makes an empty repository with one remote, sets a
sparse-checkout to the directories the build actually compiles from, and
does a single depth-1 fetch of the pinned *commit*, which GitHub allows.
That's exact, small, and can't drift. It also asserts each library's own
version string afterwards, since a pin that resolves to a different tree
than the build was written against is otherwise silent.

It warns too if the one upstream file an axis depends on has stopped
mentioning the macro that axis sets - `MBEDTLS_AES_ROM_TABLES`,
`WOLFSSL_AES_SMALL_TABLES`. An inert axis produces a null for the wrong
reason, and a null is exactly what a working countermeasure looks like.

## What a submodule cannot cover

ST's STM32 Cryptographic Library is not on GitHub. There's no
`STMicroelectronics/x-cube-cryptolib` repository and STM32CubeL1 carries
no crypto middleware; it ships inside X-CUBE-SBSFU. So there's no upstream
to point a submodule at, and when it gets added here it needs its own
fetch path and a SHA-256 manifest rather than a gitlink. ST's library is
also ST's to distribute and not ours, so it's fetched and verified rather
than vendored.

`eval/lib/stcrypt/` is reserved for it and isn't wired up yet.

## Size

`./libs/fetch.sh --size` reports it. wolfSSL's working tree is large even
sparse, because `wolfcrypt/src` is large. Narrowing further to single
files would break the moment an axis needs a second translation unit, so
it's left whole deliberately.
