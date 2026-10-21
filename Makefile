# flashwhisperer-crypto-eval - top level.
#
#   make list                       what parts and libraries there are
#   make fetch                      the pinned library checkouts
#   make verify                     nothing upstream was modified
#   make matrix MCU=stm32l152re     every configuration worth having
#   make one MCU=... LIB=... ...    a single build (same as tools/build.sh)
#
# One build is build.mk; this file is the things you do to many of them.

ROOT  := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
MCU   ?= stm32l152re

MCUS  := $(notdir $(patsubst %/board.mk,%,$(wildcard $(ROOT)/mcufw/*/board.mk)))
LIBS  := $(notdir $(patsubst %/lib.mk,%,$(wildcard $(ROOT)/eval/lib/*/lib.mk)))

# Cells the part says it cannot hold. The declaration and the measured
# reason live in the part's own board.mk; only the list is needed here, and
# build.mk refuses a cell on the list if something asks for it directly.
NO_CELLS := $(strip $(shell sed -n 's/^ *BOARD_NO_CELLS *:*= *//p'                       $(ROOT)/mcufw/$(MCU)/board.mk 2>/dev/null))

.PHONY: list fetch verify matrix one clean help
help: list

list:
	@echo "parts:"
	@for m in $(MCUS); do \
	  printf '  %-14s %s\n' "$$m" "$$(sed -n 's/^# DESC: //p' $(ROOT)/mcufw/$$m/board.mk)"; \
	done
	@echo "libraries:"
	@for l in $(LIBS); do \
	  printf '  %-14s %s\n' "$$l" "$$(sed -n 's/^# DESC: //p' $(ROOT)/eval/lib/$$l/lib.mk)"; \
	done
	@echo
	@echo "  make matrix MCU=<part>      build every configuration for a part"

fetch:
	$(ROOT)/libs/fetch.sh

verify:
	$(ROOT)/tools/verify.sh

one:
	@$(ROOT)/tools/build.sh MCU=$(MCU) $(filter-out one,$(MAKECMDGOALS))

# EVERY CONFIGURATION WORTH HAVING, AND DELIBERATELY NOT A CROSS PRODUCT.
# ROMTAB exists only for Mbed-TLS and FEWER only for the libraries that have it, so
# a generated cross product would silently build meaningless combinations and
# report them as results. Written out instead, which also makes the list
# reviewable.
#
# Each line is a configuration someone could actually ship, plus its RAM
# twin where the twin is the control that makes the flash arm readable.
#
# ONE LINE IS NOT THE SAME CELL ON EVERY PART. LibTomCrypt's default twin
# needs 12 KB of SRAM for its tables and the two parts with 16 KB cannot
# hold it, so those build the LTC_SMALL_CODE twin instead - three 1 KB
# tables - and keep a residence control rather than losing one. Which parts
# and the measured shortfall are in their own board.mk under
# BOARD_NO_CELLS. Nothing is silently skipped: the substitution is here, in
# the list, and `built for <part>` at the end names what landed.
ifeq ($(filter libtomcrypt-few0-ram1,$(NO_CELLS)),)
  LTC_TWIN = FEWER=0   RAMTAB=1
else
  LTC_TWIN = FEWER=1   RAMTAB=1
endif

matrix:
	$(MAKE) -s one MCU=$(MCU) LIB=refsbox LAYOUT=sbox   RAMTAB=0
	$(MAKE) -s one MCU=$(MCU) LIB=refsbox LAYOUT=sbox   RAMTAB=1
	$(MAKE) -s one MCU=$(MCU) LIB=refsbox LAYOUT=ttable RAMTAB=0
	$(MAKE) -s one MCU=$(MCU) LIB=mbedtls ROMTAB=0      RAMTAB=0
	$(MAKE) -s one MCU=$(MCU) LIB=mbedtls ROMTAB=1      RAMTAB=0
	$(MAKE) -s one MCU=$(MCU) LIB=mbedtls ROMTAB=1      RAMTAB=1
	$(MAKE) -s one MCU=$(MCU) LIB=mbedtls ROMTAB=1 FEWER=1 RAMTAB=0
	$(MAKE) -s one MCU=$(MCU) LIB=wolfssl FEWER=0       RAMTAB=0
	$(MAKE) -s one MCU=$(MCU) LIB=wolfssl FEWER=0       RAMTAB=1
	$(MAKE) -s one MCU=$(MCU) LIB=wolfssl FEWER=1       RAMTAB=0
	$(MAKE) -s one MCU=$(MCU) LIB=libtomcrypt FEWER=0   RAMTAB=0
	$(MAKE) -s one MCU=$(MCU) LIB=libtomcrypt $(LTC_TWIN)
	$(MAKE) -s one MCU=$(MCU) LIB=libtomcrypt FEWER=1   RAMTAB=0
	@echo
	@echo "built for $(MCU):"
	@ls -1 $(ROOT)/build/$(MCU)/

clean:
	rm -rf $(ROOT)/build

# Swallow the LIB=/RAMTAB=/... goals `make one` forwards.
%:
	@:
