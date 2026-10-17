/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "lib/bim/fragment/fragment_int.h"

const bim_type_t t_xs_LOCATOR = {NULL, 0, BIM_CONTENT_SIMPLE, BIM_LOCATOR, NULL};
const bim_type_t t_xs_UNSUPPORTED = {NULL, 0, BIM_CONTENT_SIMPLE, BIM_UNSUPPORTED, NULL};
const bim_type_t t_xs_UVLU8 = {NULL, 0, BIM_CONTENT_SIMPLE, BIM_UVLU8, NULL};
const bim_type_t t_xs_STRING = {NULL, 0, BIM_CONTENT_SIMPLE, BIM_STRING, NULL};
const bim_type_t t_xs_DURATION = {NULL, 0, BIM_CONTENT_SIMPLE, BIM_DURATION, NULL};
const bim_type_t t_xs_DATETIME = {NULL, 0, BIM_CONTENT_SIMPLE, BIM_DATETIME, NULL};
static const bim_attr_t t_TitleType_attrs[] = {
  {"type", BIM_TERMREF, 0, NULL, 0},
  {"xml:lang", BIM_STRING, 0, NULL, 0},
};
const bim_type_t t_TitleType = {t_TitleType_attrs, 2, BIM_CONTENT_SIMPLE, BIM_STRING, NULL};

static const bim_attr_t t_InlineMediaType_attrs[] = {
  {"type", BIM_STRING, 1, NULL, 0},
};
static const bim_part_t t_InlineMediaType_k1[] = {
  {BIM_PART_ELEM, "MediaData16", &t_xs_UNSUPPORTED, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "MediaData64", &t_xs_UNSUPPORTED, 1, 1, NULL, 0},
};
static const bim_part_t t_InlineMediaType_k2[] = {
  {BIM_PART_CHOICE, NULL, NULL, 1, 1, t_InlineMediaType_k1, 2},
};
static const bim_part_t t_InlineMediaType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_InlineMediaType_k2, 1};
const bim_type_t t_InlineMediaType = {t_InlineMediaType_attrs, 1, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_InlineMediaType_top};

static const bim_part_t t_MediaLocatorType_k1[] = {
  {BIM_PART_ELEM, "MediaUri", &t_xs_LOCATOR, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "InlineMedia", &t_InlineMediaType, 1, 1, NULL, 0},
};
static const bim_part_t t_MediaLocatorType_k2[] = {
  {BIM_PART_CHOICE, NULL, NULL, 0, 1, t_MediaLocatorType_k1, 2},
  {BIM_PART_ELEM, "StreamID", &t_xs_UVLU8, 0, 1, NULL, 0},
};
static const bim_part_t t_MediaLocatorType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_MediaLocatorType_k2, 2};
const bim_type_t t_MediaLocatorType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_MediaLocatorType_top};

static const bim_attr_t t_MediaRelTimePointType_attrs[] = {
  {"mediaTimeBase", BIM_STRING, 0, NULL, 0},
};
const bim_type_t t_MediaRelTimePointType = {t_MediaRelTimePointType_attrs, 1, BIM_CONTENT_SIMPLE, BIM_STRING, NULL};

static const bim_attr_t t_MediaRelIncrTimePointType_attrs[] = {
  {"mediaTimeBase", BIM_STRING, 0, NULL, 0},
  {"mediaTimeUnit", BIM_STRING, 0, NULL, 0},
};
const bim_type_t t_MediaRelIncrTimePointType = {t_MediaRelIncrTimePointType_attrs, 2, BIM_CONTENT_SIMPLE, BIM_UNSUPPORTED, NULL};

static const bim_attr_t t_ImageLocatorType_BytePosition_attrs[] = {
  {"length", BIM_UVLU8, 0, NULL, 0},
  {"offset", BIM_UVLU8, 1, NULL, 0},
};
const bim_type_t t_ImageLocatorType_BytePosition = {t_ImageLocatorType_BytePosition_attrs, 2, BIM_CONTENT_NONE, BIM_UNSUPPORTED, NULL};

static const bim_part_t t_ImageLocatorType_k1[] = {
  {BIM_PART_ELEM, "MediaUri", &t_xs_LOCATOR, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "InlineMedia", &t_InlineMediaType, 1, 1, NULL, 0},
};
static const bim_part_t t_ImageLocatorType_k2[] = {
  {BIM_PART_CHOICE, NULL, NULL, 0, 1, t_ImageLocatorType_k1, 2},
  {BIM_PART_ELEM, "StreamID", &t_xs_UVLU8, 0, 1, NULL, 0},
};
static const bim_part_t t_ImageLocatorType_k3[] = {
  {BIM_PART_ELEM, "MediaTimePoint", &t_xs_STRING, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "MediaRelTimePoint", &t_MediaRelTimePointType, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "MediaRelIncrTimePoint", &t_MediaRelIncrTimePointType, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "BytePosition", &t_ImageLocatorType_BytePosition, 1, 1, NULL, 0},
};
static const bim_part_t t_ImageLocatorType_k4[] = {
  {BIM_PART_SEQ, NULL, NULL, 1, 1, t_ImageLocatorType_k2, 2},
  {BIM_PART_CHOICE, NULL, NULL, 0, 1, t_ImageLocatorType_k3, 4},
};
static const bim_part_t t_ImageLocatorType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_ImageLocatorType_k4, 2};
const bim_type_t t_ImageLocatorType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_ImageLocatorType_top};

static const bim_attr_t t_MediaIncrDurationType_attrs[] = {
  {"mediaTimeUnit", BIM_STRING, 0, NULL, 0},
};
const bim_type_t t_MediaIncrDurationType = {t_MediaIncrDurationType_attrs, 1, BIM_CONTENT_SIMPLE, BIM_UNSUPPORTED, NULL};

static const bim_part_t t_MediaTimeType_k1[] = {
  {BIM_PART_ELEM, "MediaTimePoint", &t_xs_STRING, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "MediaRelTimePoint", &t_MediaRelTimePointType, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "MediaRelIncrTimePoint", &t_MediaRelIncrTimePointType, 1, 1, NULL, 0},
};
static const bim_part_t t_MediaTimeType_k2[] = {
  {BIM_PART_ELEM, "MediaDuration", &t_xs_STRING, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "MediaIncrDuration", &t_MediaIncrDurationType, 1, 1, NULL, 0},
};
static const bim_part_t t_MediaTimeType_k3[] = {
  {BIM_PART_CHOICE, NULL, NULL, 1, 1, t_MediaTimeType_k1, 3},
  {BIM_PART_CHOICE, NULL, NULL, 0, 1, t_MediaTimeType_k2, 2},
};
static const bim_part_t t_MediaTimeType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_MediaTimeType_k3, 2};
const bim_type_t t_MediaTimeType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_MediaTimeType_top};

static const bim_attr_t t_TemporalSegmentLocatorType_BytePosition_attrs[] = {
  {"length", BIM_UVLU8, 0, NULL, 0},
  {"offset", BIM_UVLU8, 1, NULL, 0},
};
const bim_type_t t_TemporalSegmentLocatorType_BytePosition = {t_TemporalSegmentLocatorType_BytePosition_attrs, 2, BIM_CONTENT_NONE, BIM_UNSUPPORTED, NULL};

static const bim_part_t t_TemporalSegmentLocatorType_k1[] = {
  {BIM_PART_ELEM, "MediaUri", &t_xs_LOCATOR, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "InlineMedia", &t_InlineMediaType, 1, 1, NULL, 0},
};
static const bim_part_t t_TemporalSegmentLocatorType_k2[] = {
  {BIM_PART_CHOICE, NULL, NULL, 0, 1, t_TemporalSegmentLocatorType_k1, 2},
  {BIM_PART_ELEM, "StreamID", &t_xs_UVLU8, 0, 1, NULL, 0},
};
static const bim_part_t t_TemporalSegmentLocatorType_k3[] = {
  {BIM_PART_ELEM, "MediaTime", &t_MediaTimeType, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "BytePosition", &t_TemporalSegmentLocatorType_BytePosition, 1, 1, NULL, 0},
};
static const bim_part_t t_TemporalSegmentLocatorType_k4[] = {
  {BIM_PART_SEQ, NULL, NULL, 1, 1, t_TemporalSegmentLocatorType_k2, 2},
  {BIM_PART_CHOICE, NULL, NULL, 0, 1, t_TemporalSegmentLocatorType_k3, 2},
};
static const bim_part_t t_TemporalSegmentLocatorType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_TemporalSegmentLocatorType_k4, 2};
const bim_type_t t_TemporalSegmentLocatorType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_TemporalSegmentLocatorType_top};
