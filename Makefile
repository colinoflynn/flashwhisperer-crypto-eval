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
# ROMTAB exists only for Mbed-TLS and FEWER only for the two that have it, so
# a generated cross product would silently build meaningless combinations and
# report them as results. Written out instead, which also makes the list
# reviewable.
#
# Each line is a configuration someone could actually ship, plus its RAM
# twin where the twin is the control that makes the flash arm readable.
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
	@echo
	@echo "built for $(MCU):"
	@ls -1 $(ROOT)/build/$(MCU)/

clean:
	rm -rf $(ROOT)/build

# Swallow the LIB=/RAMTAB=/... goals `make one` forwards.
%:
	@:
