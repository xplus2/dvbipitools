/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPISDS_INPUT_INPUT_INT_H
#define DIPISDS_INPUT_INPUT_INT_H

#include <arpa/inet.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/net/dvbstp.h"
#include "lib/helper/xml_util.h"
#include "dipisds/input.h"

const char *suffix(const char *path);
int read_whole_file(const char *path, unsigned char **out, size_t *out_len);
int parse_mcast_uri(const char *uri, sds_service_t *s);
int dvb_id(const char *name, const char *v, unsigned *out);
int load_csv(FILE *f, input_t *in);
int load_m3u(FILE *f, input_t *in);
int load_xspf(input_t *in, const unsigned char *buf);
int count_xml_elems(const char *buf, size_t len, const char *name);

#endif
