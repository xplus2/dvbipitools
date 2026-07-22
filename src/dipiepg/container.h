/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIEPG_CONTAINER_H
#define DIPIEPG_CONTAINER_H

#include <stddef.h>

/* TS 102 822-3-2 4.5.2.1 table 4 */
#define CONTAINER_STRUCT_DATA_REPOSITORY 0x02
/* table 5, structure_id for structure_type=0x02 */
#define CONTAINER_DATAREPO_STRINGS 0x00
#define CONTAINER_DATAREPO_BINARY 0x01

/* container with exactly 2 Data Repository structures (binary access unit + strings). *out is malloc'd, caller frees. 0 ok, -1 oom */
int container_build(const unsigned char *access_unit, size_t au_len, const unsigned char *string_repo, size_t sr_len, unsigned char **out, size_t *out_len);

/* au and sr point into buf, no copy. 0 ok, -1 malformed or structures not found */
int container_parse(const unsigned char *buf, size_t len, const unsigned char **au, size_t *au_len, const unsigned char **sr, size_t *sr_len);

#endif
