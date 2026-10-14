/* wolfSSL's reduced table. NOTE that the DEFAULT build has no separate byte
   S-box at all: its Te carries one. So in the default configuration this
   slot is empty and slot 1 is the address that matters -- which is why
   SS_V_IDENT reports the first slot that is actually populated rather than
   slot 0 unconditionally. */
*(.rodata.Tsbox)
