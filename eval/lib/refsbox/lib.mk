# DESC: this project's own byte S-box AES -- the control, source in-tree
# refsbox.
#
# The only entry under eval/lib/ with no counterpart in libs/, because it is
# not a library under test. Everything else here fetches its source at a
# pinned commit and modifies none of it; this one IS ours.

LIB_SRC   += $(LIBDIR)/aes.c
LIB_INC   += -I$(LIBDIR)
LIB_DEFS  += -DLIB_REFSBOX

# refsbox is the one provider with a choice of table layout, which is what
# makes it the porting checklist: 's' byte S-box, 't' four word T-tables,
# 'c' one rotated table.
LAYOUT ?= sbox
ifeq ($(LAYOUT),sbox)
  LIB_DEFS += -DSS_LAYOUT="'s'"
else ifeq ($(LAYOUT),ttable)
  LIB_DEFS += -DSS_LAYOUT="'t'"
else ifeq ($(LAYOUT),compact)
  LIB_DEFS += -DSS_LAYOUT="'c'"
else
  $(error LAYOUT must be sbox, ttable or compact)
endif
LIB_VARIANT = -$(LAYOUT)

# Nothing to fetch and nothing to verify: the source is in this repository
# and `git status` is the check.
LIB_HAS_VENDOR = 0
