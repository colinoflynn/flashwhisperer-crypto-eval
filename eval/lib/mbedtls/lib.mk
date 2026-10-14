# DESC: Mbed-TLS 3.6.7, axes ROMTAB and FEWER (both default off)
# Mbed-TLS. Pinned at 3.6.7; see libs/PINS for why the 3.6 LTS line and not
# 4.x (4.x has no library/aes.c at all).
#
# TWO AXES, BOTH MBED-TLS'S OWN OPTIONS, BOTH DEFAULTING OFF:
#
#   ROMTAB=0|1   MBEDTLS_AES_ROM_TABLES.
#       DEFAULT 0, WHICH IS STOCK MBED-TLS, AND STOCK IS EXPECTED TO BE
#       QUIET: with it off the tables are plain `static` arrays in .bss,
#       generated at run time, so the flash read path never sees them. The
#       build reports every table slot as address 0 and `make` says so out
#       loud rather than leaving it to be discovered. Set it to 1 for the
#       configuration an integrator picks to get ~9 KB of RAM back, which is
#       what puts the tables in flash.
#
#   FEWER=0|1    MBEDTLS_AES_FEWER_TABLES.
#       Off: four 1 KB forward T-tables plus the S-box. On: one T-table,
#       rotated inline.
#
# Both are reached through MBEDTLS_CONFIG_FILE -- Mbed-TLS's own documented
# mechanism. No library source is modified.

MBEDTLS_DIR = $(ROOT)/libs/mbedtls

ROMTAB ?= 0
FEWER  ?= 0

LIB_SRC  += $(MBEDTLS_DIR)/library/aes.c $(MBEDTLS_DIR)/library/platform_util.c
LIB_INC  += -I$(MBEDTLS_DIR)/include -I$(MBEDTLS_DIR)/library -I$(LIBDIR)
LIB_DEFS += -DLIB_MBEDTLS -DMBEDTLS_CONFIG_FILE='"my_mbedtls_config.h"'

ifeq ($(ROMTAB),1)
  LIB_DEFS += -DSS_MBEDTLS_ROM_TABLES
endif
ifeq ($(FEWER),1)
  LIB_DEFS += -DSS_MBEDTLS_FEWER_TABLES
endif

LIB_VARIANT = -rom$(ROMTAB)-few$(FEWER)

ifeq ($(ROMTAB),0)
LIB_BUILD_NOTE = "  NOTE: MBEDTLS_AES_ROM_TABLES is OFF -- this is STOCK Mbed-TLS." \
                 "        Its tables are generated into .bss at run time, so they are" \
                 "        in RAM and this build is EXPECTED to be quiet. The table" \
                 "        slots will report address 0. Build ROMTAB=1 for the" \
                 "        flash-table configuration."
endif

LIB_HAS_VENDOR = 1
