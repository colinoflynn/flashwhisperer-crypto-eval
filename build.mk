# The one build. Composes a PART with a LIBRARY and nothing else knows about
# either; see docs/DESIGN.md for why the pieces divide where they do.
#
#   make MCU=stm32l152re LIB=refsbox
#   make MCU=stm32l152re LIB=mbedtls ROMTAB=1 RAMTAB=1
#
# WHY THE VARIANT STRING IS SO LONG, AND WHY EACH VARIANT GETS ITS OWN
# OBJECT DIRECTORY. Several of these axes change the LINK-TIME LAYOUT, and
# the same firmware at three link-time accidents has needed 6 250, 12 500
# and 25 000 traces to recover the same key. Two builds that differ only in
# a define are not interchangeable images, and an object file reused across
# them is a silent wrong answer. The variant string is also what the capture
# is filed under, and the device reports its own configuration so the two
# can be checked against each other.

ROOT    := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

MCU     ?= stm32l152re
LIB     ?= refsbox

MCUDIR  := $(ROOT)/mcufw/$(MCU)
LIBDIR  := $(ROOT)/eval/lib/$(LIB)
EVALDIR := $(ROOT)/eval/mcu/$(MCU)
VICTIM  := $(ROOT)/instrumentation/victim
PROTO   := $(ROOT)/instrumentation/protocol

ifeq ($(wildcard $(MCUDIR)/board.mk),)
  $(error no such part: $(MCU).  `make list` shows what there is)
endif
ifeq ($(wildcard $(LIBDIR)/lib.mk),)
  $(error no such library: $(LIB).  `make list` shows what there is)
endif

# ---- the axes every part and library shares ------------------------------
# The table residence twin. THE MANDATORY CONTROL: same code, same
# instruction stream, same indices, tables moved to RAM by the linker.
RAMTAB  ?= 0
# Which physical link the victim speaks on. NO DEFAULT HERE: the parts do
# not all offer the same links, so the default is the part's own first
# choice and is set after board.mk has been read. Hardcoding `header` made
# `make matrix` fail on the second part ported, which offers only `vcp` --
# correctly refused rather than built wired to nothing, but refused for a
# default this file had no business choosing.
LINK    ?=
# Whether the victim carries a cycle counter at all. `none` is the arm with
# no instrumentation in it, for an externally timed measurement.
TIMING  ?= systick
# A fixed plaintext encrypted before the measured one, so trace k does not
# depend on trace k-1 through the table-line state.
WARMUP  ?= 1
# Shifts all code by N bytes without changing it: the cheap way to ask
# whether a result is about the code or about where the code landed.
CODEPAD ?= 0
BAUD    ?= 115200
FALIGN  ?= 4

include $(MCUDIR)/board.mk
include $(LIBDIR)/lib.mk
-include $(EVALDIR)/eval.mk

# ---- link ----------------------------------------------------------------
# The part's first declared link, unless the caller named one.
ifeq ($(strip $(LINK)),)
  LINK := $(firstword $(BOARD_LINKS))
endif

ifeq ($(LINK),vcp)
  COMMON_DEFS += -DSS_LINK_VCP
else ifeq ($(LINK),header)
  COMMON_DEFS += -DSS_LINK_HEADER
else
  $(error LINK must be one of: $(BOARD_LINKS))
endif
ifeq ($(filter $(LINK),$(BOARD_LINKS)),)
  $(error $(MCU) does not offer LINK=$(LINK); it has: $(BOARD_LINKS))
endif

# Which counter the victim times itself with, where it has a choice. `none`
# is not a degenerate case: it is the arm with NO instrumentation in the
# victim at all, for an externally timed measurement, and SS_V_ENC_CYC then
# answers with an error rather than with zeros so the host cannot mistake
# a missing counter for a constant-time result.
ifeq ($(TIMING),systick)
  COMMON_DEFS += -DSS_TIMING_SYSTICK
else ifeq ($(TIMING),dwt)
  COMMON_DEFS += -DSS_TIMING_DWT
else ifneq ($(TIMING),none)
  $(error TIMING must be systick, dwt or none)
endif

ifeq ($(RAMTAB),1)
  LDSCRIPT = link_ram.ld
else
  LDSCRIPT = link_flash.ld
endif

COMMON_DEFS += -DSS_BAUD=$(BAUD)u -DSS_WARMUP=$(WARMUP) -DSS_RAMTAB=$(RAMTAB)

VARIANT = $(LIB)$(LIB_VARIANT)-ram$(RAMTAB)-$(LINK)-b$(BAUD)-t$(TIMING)-w$(WARMUP)-pad$(CODEPAD)
BUILD   = $(ROOT)/build/$(MCU)/$(VARIANT)
TARGET  = $(BUILD)/fw

SRC = $(VICTIM)/src/ss_victim.c $(VICTIM)/src/ss_main.c \
      $(LIBDIR)/provider.c $(BOARD_SRC) $(LIB_SRC)

# -fdata-sections IS LOAD-BEARING, NOT A SIZE OPTIMISATION: it gives each
# table its own input section so the shared sections script can collect and
# relocate them, which is the only reason the residence axis exists for a
# library whose tables are file-static -- or shipped as a binary archive.
CFLAGS  = $(BOARD_CPU) -O2 -g -ffreestanding -fno-common \
          -fdata-sections -ffunction-sections -falign-functions=$(FALIGN) \
          -Wall -Wextra \
          -I$(VICTIM)/include -I$(PROTO) $(BOARD_INC) $(LIB_INC) \
          $(COMMON_DEFS) $(LIB_DEFS) -MMD -MP

# -L gives `INCLUDE` its search path, which is how the three linker fragments
# find each other: the part's memory map, the shared sections block, and the
# library's table section names.
LDFLAGS = $(BOARD_CPU) -T$(LDSCRIPT) \
          -L$(VICTIM)/ld -L$(MCUDIR) -L$(LIBDIR) \
          -nostartfiles --specs=nano.specs --specs=nosys.specs \
          -Wl,--gc-sections -Wl,-Map=$(TARGET).map \
          -Wl,--defsym=CODEPAD=$(CODEPAD)

CROSS  ?= arm-none-eabi-
CC      = $(CROSS)gcc
OBJ     = $(addprefix $(BUILD)/,$(notdir $(SRC:.c=.o)))
vpath %.c $(sort $(dir $(SRC)))

.PHONY: all addr clean variant
all: $(TARGET).bin

# The variant directory name, for a caller that needs to know where a build
# landed. Asking make is the only way to get it that cannot disagree with
# the rule above; recomputing the naming scheme in a script is a second copy
# of it, and the two drifting apart files a capture under the wrong name.
variant:
	@echo $(VARIANT)

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJ) | $(BUILD)
	$(CC) $(LDFLAGS) $(OBJ) $(LIB_ARCHIVE) -o $@
	@$(CROSS)size $@

$(TARGET).bin: $(TARGET).elf
	$(CROSS)objcopy -O binary $< $@
	@echo "built $@"
	@$(MAKE) --no-print-directory -f $(ROOT)/build.mk addr ELF=$(TARGET).elf \
	         MCU=$(MCU) LIB=$(LIB) 2>/dev/null || true
	@$(if $(LIB_BUILD_NOTE),printf '%s\n' $(LIB_BUILD_NOTE),)

$(BUILD):
	mkdir -p $@

# Addresses read OFF THE IMAGE rather than remembered. A trace count quoted
# without the table's address is under-specified, and the three slots are
# where the relocation either happened or did not.
ELF ?= $(TARGET).elf
addr:
	@echo "  addresses in $(notdir $(ELF)):"
	@$(CROSS)nm $(ELF) | grep -E " _tab(S|T|D)(_end)?$$" | sort | sed 's/^/    /'

clean:
	rm -rf $(ROOT)/build/$(MCU)/$(VARIANT)

-include $(OBJ:.o=.d)
