/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "lib/bim/fragment/fragment_int.h"

static const bim_attr_t t_BaseMemberOfType_attrs[] = {
  {"crid", BIM_LOCATOR, 1, NULL, 0},
  {"index", BIM_UNSUPPORTED, 0, NULL, 0},
};
const bim_type_t t_BaseMemberOfType = {t_BaseMemberOfType_attrs, 2, BIM_CONTENT_NONE, BIM_UNSUPPORTED, NULL};

const bim_type_t t_BaseDerivationReasonType = {NULL, 0, BIM_CONTENT_NONE, BIM_UNSUPPORTED, NULL};

static const bim_attr_t t_DerivedFromType_attrs[] = {
  {"crid", BIM_LOCATOR, 1, NULL, 0},
  {"index", BIM_UNSUPPORTED, 0, NULL, 0},
};
static const bim_part_t t_DerivedFromType_k1[] = {
  {BIM_PART_ELEM, "DerivationReason", &t_BaseDerivationReasonType, 0, BIM_UNBOUNDED, NULL, 0},
};
static const bim_part_t t_DerivedFromType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_DerivedFromType_k1, 1};
const bim_type_t t_DerivedFromType = {t_DerivedFromType_attrs, 2, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_DerivedFromType_top};

static const bim_attr_t t_EpisodeOfType_attrs[] = {
  {"crid", BIM_LOCATOR, 1, NULL, 0},
  {"index", BIM_UNSUPPORTED, 0, NULL, 0},
};
const bim_type_t t_EpisodeOfType = {t_EpisodeOfType_attrs, 2, BIM_CONTENT_NONE, BIM_UNSUPPORTED, NULL};

static const char *const t_AggregationOfType_e0[] = {"omnibus", "magazine"};
static const bim_attr_t t_AggregationOfType_attrs[] = {
  {"type", BIM_ENUM, 1, t_AggregationOfType_e0, 2},
};
static const bim_part_t t_AggregationOfType_k1[] = {
  {BIM_PART_ELEM, "AggregatedProgram", &t_CRIDRefType, 2, BIM_UNBOUNDED, NULL, 0},
};
static const bim_part_t t_AggregationOfType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_AggregationOfType_k1, 1};
const bim_type_t t_AggregationOfType = {t_AggregationOfType_attrs, 1, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_AggregationOfType_top};

static const bim_attr_t t_ProgramInformationType_attrs[] = {
  {"fragmentId", BIM_STRING, 0, NULL, 0},
  {"fragmentVersion", BIM_UVLU8, 0, NULL, 0},
  {"programId", BIM_LOCATOR, 1, NULL, 0},
};
static const bim_part_t t_ProgramInformationType_k1[] = {
  {BIM_PART_ELEM, "BasicDescription", &t_BasicContentDescriptionType, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "OtherIdentifier", &t_UniqueIDType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "AVAttributes", &t_AVAttributesType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "MemberOf", &t_BaseMemberOfType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "DerivedFrom", &t_DerivedFromType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "EpisodeOf", &t_EpisodeOfType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "PartOfAggregatedProgram", &t_xs_LOCATOR, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "AggregationOf", &t_AggregationOfType, 0, 1, NULL, 0},
};
static const bim_part_t t_ProgramInformationType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_ProgramInformationType_k1, 8};
const bim_type_t t_ProgramInformationType = {t_ProgramInformationType_attrs, 3, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_ProgramInformationType_top};

static const bim_part_t t_InstanceDescriptionType_k1[] = {
  {BIM_PART_ELEM, "Title", &t_TitleType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "Synopsis", &t_SynopsisType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "Genre", &t_GenreType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "PurchaseList", &t_PurchaseListType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "CaptionLanguage", &t_CaptionLanguageType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "SignLanguage", &t_SignLanguageType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "ParentalGuidance", &t_ParentalGuidanceType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "AVAttributes", &t_AVAttributesType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "MemberOf", &t_BaseMemberOfType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "OtherIdentifier", &t_UniqueIDType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "RelatedMaterial", &t_RelatedMaterialType, 0, BIM_UNBOUNDED, NULL, 0},
};
static const bim_part_t t_InstanceDescriptionType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_InstanceDescriptionType_k1, 11};
const bim_type_t t_InstanceDescriptionType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_InstanceDescriptionType_top};

static const bim_attr_t t_FlagType_attrs[] = {
  {"value", BIM_BOOLEAN, 1, NULL, 0},
};
const bim_type_t t_FlagType = {t_FlagType_attrs, 1, BIM_CONTENT_NONE, BIM_UNSUPPORTED, NULL};

static const bim_part_t t_ScheduleEventType_k1[] = {
  {BIM_PART_ELEM, "Program", &t_CRIDRefType, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "ProgramURL", &t_xs_LOCATOR, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "InstanceMetadataId", &t_xs_LOCATOR, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "InstanceDescription", &t_InstanceDescriptionType, 0, 1, NULL, 0},
};
static const bim_part_t t_ScheduleEventType_k2[] = {
  {BIM_PART_ELEM, "PublishedStartTime", &t_xs_DATETIME, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "PublishedEndTime", &t_xs_DATETIME, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "PublishedDuration", &t_xs_DURATION, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "Live", &t_FlagType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "Repeat", &t_FlagType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "FirstShowing", &t_FlagType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "LastShowing", &t_FlagType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "Free", &t_FlagType, 0, 1, NULL, 0},
};
static const bim_part_t t_ScheduleEventType_k3[] = {
  {BIM_PART_SEQ, NULL, NULL, 1, 1, t_ScheduleEventType_k1, 4},
  {BIM_PART_SEQ, NULL, NULL, 1, 1, t_ScheduleEventType_k2, 8},
};
static const bim_part_t t_ScheduleEventType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_ScheduleEventType_k3, 2};
const bim_type_t t_ScheduleEventType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_ScheduleEventType_top};

static const bim_attr_t t_ScheduleType_attrs[] = {
  {"end", BIM_DATETIME, 0, NULL, 0},
  {"fragmentId", BIM_STRING, 0, NULL, 0},
  {"fragmentVersion", BIM_UVLU8, 0, NULL, 0},
  {"serviceIDRef", BIM_STRING, 1, NULL, 0},
  {"start", BIM_DATETIME, 0, NULL, 0},
};
static const bim_part_t t_ScheduleType_k1[] = {
  {BIM_PART_ELEM, "ScheduleEvent", &t_ScheduleEventType, 1, BIM_UNBOUNDED, NULL, 0},
};
static const bim_part_t t_ScheduleType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_ScheduleType_k1, 1};
const bim_type_t t_ScheduleType = {t_ScheduleType_attrs, 5, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_ScheduleType_top};

static const char *const t_ServiceInformationNameType_e0[] = {"short", "medium", "long"};
static const bim_attr_t t_ServiceInformationNameType_attrs[] = {
  {"length", BIM_ENUM, 0, t_ServiceInformationNameType_e0, 3},
  {"xml:lang", BIM_STRING, 0, NULL, 0},
};
const bim_type_t t_ServiceInformationNameType = {t_ServiceInformationNameType_attrs, 2, BIM_CONTENT_SIMPLE, BIM_STRING, NULL};

static const bim_part_t t_ValidPeriodType_k1[] = {
  {BIM_PART_ELEM, "ValidFrom", &t_xs_DATETIME, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "ValidTo", &t_xs_DATETIME, 0, 1, NULL, 0},
};
static const bim_part_t t_ValidPeriodType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_ValidPeriodType_k1, 2};
const bim_type_t t_ValidPeriodType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_ValidPeriodType_top};

static const bim_attr_t t_ServiceRefType_attrs[] = {
  {"serviceIDRef", BIM_STRING, 1, NULL, 0},
};
static const bim_part_t t_ServiceRefType_k1[] = {
  {BIM_PART_ELEM, "ValidPeriod", &t_ValidPeriodType, 0, BIM_UNBOUNDED, NULL, 0},
};
static const bim_part_t t_ServiceRefType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_ServiceRefType_k1, 1};
const bim_type_t t_ServiceRefType = {t_ServiceRefType_attrs, 1, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_ServiceRefType_top};

static const bim_attr_t t_ServiceInformationType_attrs[] = {
  {"fragmentId", BIM_STRING, 0, NULL, 0},
  {"fragmentVersion", BIM_UVLU8, 0, NULL, 0},
  {"serviceId", BIM_STRING, 1, NULL, 0},
};
static const bim_part_t t_ServiceInformationType_k1[] = {
  {BIM_PART_ELEM, "Name", &t_ServiceInformationNameType, 1, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "Owner", &t_xs_STRING, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "ServiceURL", &t_xs_LOCATOR, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "Logo", &t_MediaLocatorType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "ServiceDescription", &t_SynopsisType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "ServiceGenre", &t_GenreType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "ServiceLanguage", &t_xs_STRING, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "ParentService", &t_ServiceRefType, 0, BIM_UNBOUNDED, NULL, 0},
};
static const bim_part_t t_ServiceInformationType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_ServiceInformationType_k1, 8};
const bim_type_t t_ServiceInformationType = {t_ServiceInformationType_attrs, 3, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_ServiceInformationType_top};


const bim_type_t *const tva_type_program_information = &t_ProgramInformationType;
const bim_type_t *const tva_type_schedule = &t_ScheduleType;
const bim_type_t *const tva_type_service_information = &t_ServiceInformationType;
