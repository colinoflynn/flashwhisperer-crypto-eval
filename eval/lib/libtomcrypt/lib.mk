# DESC: LibTomCrypt AES at 6c6d510; default 4T and LTC_SMALL_CODE 1T
# Upstream's unmodified software Rijndael. The pinned source is a develop
# snapshot, so the commit in libs/PINS, rather than a release number, is the
# version identity. FEWER=1 is LibTomCrypt's own LTC_SMALL_CODE option.
# The RAMTAB twin relocates only the const tables through the linker.

LIBTOMCRYPT_DIR = $(ROOT)/libs/libtomcrypt
FEWER ?= 0

LIB_SRC  += $(LIBTOMCRYPT_DIR)/src/ciphers/aes/aes.c
LIB_INC  += -I$(LIBTOMCRYPT_DIR)/src/headers
# ARGTYPE=4 keeps LibTomCrypt's argument checks and returns an error instead
# of calling its host-side crypt_argchk() on a bare-metal target.
LIB_DEFS += -DLTC_NO_TEST -DARGTYPE=4

ifeq ($(FEWER),1)
  LIB_DEFS += -DLTC_SMALL_CODE
endif

LIB_VARIANT = -few$(FEWER)
LIB_HAS_VENDOR = 1
