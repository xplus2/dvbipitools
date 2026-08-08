/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_MUX_CADESCBUILD_H
#define DVBIPITOOLS_LIB_MUX_CADESCBUILD_H

#include <stddef.h>

#define CADESC_SCRAMBLING_MODE_CSA2 0x02
#define CADESC_SCRAMBLING_MODE_CISSA 0x10

/* CA_descriptor, tag 0x09, no private_data_bytes. 0 on overflow */
size_t cadescbuild_ca_descriptor(unsigned ca_system_id, unsigned ca_pid, unsigned char *out, size_t cap);

/* scrambling_descriptor, tag 0x65 (TS 103 127 clause 7). 0 on overflow */
size_t cadescbuild_scrambling_descriptor(unsigned char scrambling_mode, unsigned char *out, size_t cap);

#endif
