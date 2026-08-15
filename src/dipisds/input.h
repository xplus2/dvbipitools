/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPISDS_INPUT_H
#define DIPISDS_INPUT_H

#include "lib/sds_xml.h"

typedef enum { INPUT_SERVICES, INPUT_RAW_XML } input_kind_t;

typedef struct {
  input_kind_t kind;
  sds_service_t services[SDS_MAX_SERVICES];
  int service_count;
  unsigned char *raw_xml; /* kind==INPUT_RAW_XML, malloc'd */
  size_t raw_xml_len;
  unsigned raw_payload_id;
} input_t;

/* format from path suffix: .csv/.m3u/.xspf -> services, .xml -> raw passthrough. 0 ok, -1 error on stderr */
int input_load(const char *path, input_t *in);
void input_free(input_t *in);

/* --packages file, one line per package: id,name,lang,visible,svc1|svc2|... 0 ok, -1 error on stderr */
int input_load_packages(const char *path, sds_package_t *out, int max, int *count);

/* --cells file, one line per cell: id,country,type:value,type:value,... 0 ok, -1 error on stderr */
int input_load_cells(const char *path, sds_cell_t *out, int max, int *count);

#endif
